#pragma once

#include <axonvex_core/missionElement.hpp>
#include <axonvex_core/processingUnit.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

enum class PipelineStatus {
    Idle,
    Executing,
    Paused,
    Finished,
    Aborted,
    Failed
};

inline std::string pipelineStatusToString(PipelineStatus s) {
    switch (s) {
        case PipelineStatus::Idle:      return "Idle";
        case PipelineStatus::Executing: return "Executing";
        case PipelineStatus::Paused:    return "Paused";
        case PipelineStatus::Finished:  return "Finished";
        case PipelineStatus::Aborted:   return "Aborted";
        case PipelineStatus::Failed:    return "Failed";
    }
    return "Unknown";
}

namespace PipelineControl {
constexpr int ABORT   = 1;
constexpr int RESTART = 2;
constexpr int PAUSE   = 3;
constexpr int RESUME  = 4;
} // namespace PipelineControl

/**
 * @brief Directed-graph mission executor as a ProcessingUnit
 *
 * The pipeline manages a graph of MissionElements with labeled transitions.
 * On each processSync() tick it calls execute() on the current element and
 * advances the graph pointer based on the returned TransitionResult.
 *
 * Ports:
 *   AsyncInputPort<int>  port 0 ("control") — send PipelineControl commands
 *   OutputPort<int>      port 0 ("status")  — current PipelineStatus each tick
 */
class MissionPipeline : public ProcessingUnit {
  public:
    explicit MissionPipeline(const std::string& name) : ProcessingUnit(name) {
        controlPort_ = createAsyncInputPort<int>(0, "control");
        statusPort_  = createOutputPort<int>(0, "status");
    }

    // -----------------------------------------------------------------
    // Graph construction (call before system.initialize())
    // -----------------------------------------------------------------

    void addElement(MissionElement* element) {
        elements_[element->name()] = element;
        if (!startElementName_.empty()) return;
        startElementName_ = element->name();
    }

    void addSequentialTransition(const std::string& from, const std::string& to) {
        transitions_[from].push_back({to, TransitionResult::Default});
    }

    void addConditionalTransition(const std::string& from, const std::string& to,
                                   TransitionResult condition) {
        transitions_[from].push_back({to, condition});
    }

    void setStartElement(const std::string& name) {
        startElementName_ = name;
    }

    // -----------------------------------------------------------------
    // Execution control (programmatic API)
    // -----------------------------------------------------------------

    void startPipeline() {
        if (status_ != PipelineStatus::Idle) return;
        auto it = elements_.find(startElementName_);
        if (it == elements_.end()) {
            status_ = PipelineStatus::Failed;
            return;
        }
        currentElement_ = it->second;
        status_ = PipelineStatus::Executing;
        currentElement_->onEnter();
    }

    void abort() {
        if (status_ == PipelineStatus::Executing || status_ == PipelineStatus::Paused) {
            if (currentElement_) currentElement_->onExit();
            status_ = PipelineStatus::Aborted;
            currentElement_ = nullptr;
        }
    }

    void restart() {
        if (currentElement_) currentElement_->onExit();
        currentElement_ = nullptr;
        for (auto& [_, elem] : elements_) {
            elem->reset();
        }
        status_ = PipelineStatus::Idle;
        startPipeline();
    }

    void pause() {
        if (status_ == PipelineStatus::Executing) {
            status_ = PipelineStatus::Paused;
        }
    }

    void resume() {
        if (status_ == PipelineStatus::Paused) {
            status_ = PipelineStatus::Executing;
        }
    }

    // -----------------------------------------------------------------
    // State
    // -----------------------------------------------------------------

    PipelineStatus status() const { return status_; }

    const std::string& currentElementName() const {
        static const std::string empty;
        return currentElement_ ? currentElement_->name() : empty;
    }

    // -----------------------------------------------------------------
    // ProcessingUnit overrides
    // -----------------------------------------------------------------

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        if (status_ != PipelineStatus::Executing || !currentElement_) {
            statusPort_->write(static_cast<int>(status_));
            return;
        }

        auto result = currentElement_->execute();

        if (result == TransitionResult::Awaiting) {
            statusPort_->write(static_cast<int>(status_));
            return;
        }

        if (result == TransitionResult::Failed) {
            currentElement_->onExit();
            currentElement_ = nullptr;
            status_ = PipelineStatus::Failed;
            statusPort_->write(static_cast<int>(status_));
            return;
        }

        auto it = transitions_.find(currentElement_->name());
        if (it == transitions_.end()) {
            currentElement_->onExit();
            currentElement_ = nullptr;
            status_ = PipelineStatus::Finished;
            statusPort_->write(static_cast<int>(status_));
            return;
        }

        MissionElement* next = nullptr;
        for (const auto& t : it->second) {
            if (t.condition == result) {
                auto eit = elements_.find(t.targetName);
                if (eit != elements_.end()) {
                    next = eit->second;
                }
                break;
            }
        }

        if (!next) {
            currentElement_->onExit();
            currentElement_ = nullptr;
            status_ = PipelineStatus::Finished;
            statusPort_->write(static_cast<int>(status_));
            return;
        }

        currentElement_->onExit();
        currentElement_ = next;
        currentElement_->onEnter();

        statusPort_->write(static_cast<int>(status_));
    }

    void processAsync() override {
        if (!controlPort_->wasUpdated()) return;
        int cmd = controlPort_->read();
        switch (cmd) {
            case PipelineControl::ABORT:   abort();   break;
            case PipelineControl::RESTART: restart(); break;
            case PipelineControl::PAUSE:   pause();   break;
            case PipelineControl::RESUME:  resume();  break;
            default: break;
        }
    }

    void reset() override {
        if (currentElement_) currentElement_->onExit();
        currentElement_ = nullptr;
        for (auto& [_, elem] : elements_) {
            elem->reset();
        }
        status_ = PipelineStatus::Idle;
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override { return "MissionPipeline"; }

    // -----------------------------------------------------------------
    // Port accessors
    // -----------------------------------------------------------------

    AsyncInputPort<int>* getControlPort() { return controlPort_; }
    OutputPort<int>* getStatusPort() { return statusPort_; }

  private:
    struct Transition {
        std::string targetName;
        TransitionResult condition;
    };

    std::unordered_map<std::string, MissionElement*> elements_;
    std::unordered_map<std::string, std::vector<Transition>> transitions_;
    std::string startElementName_;

    MissionElement* currentElement_{nullptr};
    PipelineStatus status_{PipelineStatus::Idle};

    AsyncInputPort<int>* controlPort_;
    OutputPort<int>* statusPort_;
};

} // namespace axonvex::core
