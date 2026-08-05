#pragma once

#include <axonvex_core/missionElement.hpp>
#include <axonvex_core/processingUnit.hpp>

#include <mutex>
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

    /**
     * @brief Register a mission element as a node in the transition graph.
     *
     * Ownership: @p element is NON-OWNED. The caller retains ownership and
     * must keep the object alive for the entire lifetime of this
     * MissionPipeline (stable-reference contract) — the pipeline stores the
     * raw pointer in `elements_`/`currentElement_` and dereferences it from
     * `processSync()` on the scheduler thread. The pipeline never deletes
     * it. The first element added (absent an explicit `setStartElement()`)
     * becomes the start element.
     */
    void addElement(MissionElement* element) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        elements_[element->name()] = element;
        if (!startElementName_.empty())
            return;
        startElementName_ = element->name();
    }

    void addSequentialTransition(const std::string& from, const std::string& to) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        transitions_[from].push_back({to, TransitionResult::Default});
    }

    void addConditionalTransition(const std::string& from, const std::string& to,
                                  TransitionResult condition) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        transitions_[from].push_back({to, condition});
    }

    void setStartElement(const std::string& name) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        startElementName_ = name;
    }

    // -----------------------------------------------------------------
    // Execution control (programmatic API)
    // -----------------------------------------------------------------

    // All control methods below follow the same shape: lock -> decide and
    // apply the state flip on a captured snapshot -> unlock -> invoke any
    // MissionElement hook (onEnter/onExit/reset) on the snapshot, never
    // under stateMutex_. Hooks are user code and may re-enter this API
    // (e.g. an onEnter() calling pause()); stateMutex_ is non-recursive and
    // is never held across a hook call, so re-entrancy cannot deadlock.

    void startPipeline() {
        std::string firstError;
        bool validationFailed = false;
        MissionElement* toEnter = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (status_ != PipelineStatus::Idle)
                return;
            if (!validateGraph(firstError)) {
                status_ = PipelineStatus::Failed;
                validationFailed = true;
            } else {
                currentElement_ = elements_.at(startElementName_);
                status_ = PipelineStatus::Executing;
                toEnter = currentElement_;
            }
        }
        if (validationFailed) {
            setError(firstError);
            return;
        }
        if (toEnter)
            toEnter->onEnter();
    }

    void abort() {
        MissionElement* toExit = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (status_ == PipelineStatus::Executing || status_ == PipelineStatus::Paused) {
                toExit = currentElement_;
                status_ = PipelineStatus::Aborted;
                currentElement_ = nullptr;
            }
        }
        if (toExit)
            toExit->onExit();
    }

    void restart() {
        MissionElement* toExit = nullptr;
        std::vector<MissionElement*> toReset;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            toExit = currentElement_;
            currentElement_ = nullptr;
            toReset.reserve(elements_.size());
            for (auto& kv : elements_)
                toReset.push_back(kv.second);
            status_ = PipelineStatus::Idle;
        }
        if (toExit)
            toExit->onExit();
        for (auto* e : toReset)
            e->reset();
        startPipeline(); // re-acquires stateMutex_ itself; not held here.
    }

    void pause() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (status_ == PipelineStatus::Executing) {
            status_ = PipelineStatus::Paused;
        }
    }

    void resume() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (status_ == PipelineStatus::Paused) {
            status_ = PipelineStatus::Executing;
        }
    }

    // -----------------------------------------------------------------
    // State
    // -----------------------------------------------------------------

    PipelineStatus status() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return status_;
    }

    const std::string& currentElementName() const {
        static const std::string empty;
        std::lock_guard<std::mutex> lock(stateMutex_);
        // Safe to return a reference past the unlock: MissionElement::name()
        // is an immutable member of the caller-owned, stable-reference
        // object (see addElement()'s ownership contract) — its address and
        // contents don't depend on stateMutex_.
        return currentElement_ ? currentElement_->name() : empty;
    }

    // -----------------------------------------------------------------
    // ProcessingUnit overrides
    // -----------------------------------------------------------------

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    // RT-path note: called from the scheduler thread every tick at
    // mission-rate frequency. stateMutex_ is taken up to three times per
    // tick, each time held only for a map lookup/state-flip — never across
    // execute()/onEnter()/onExit(), which are user code. Cost: one
    // uncontended mutex lock/unlock pair is tens of nanoseconds, negligible
    // next to a mission tick period; this is not lock-free and not claimed
    // to be. Upgrade path if a benchmark ever shows contention here:
    // replace status_/currentElement_ with atomics and move addElement/
    // transition mutation to construction-time-only (already the
    // documented usage), or move control commands onto an SPSC queue
    // drained at tick start instead of locking from arbitrary threads.
    void processSync() override {
        setState(ExecutionState::RUNNING);

        PipelineStatus statusSnapshot;
        MissionElement* element = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            statusSnapshot = status_;
            element = currentElement_;
        }

        if (statusSnapshot != PipelineStatus::Executing || !element) {
            statusPort_->write(static_cast<int>(statusSnapshot));
            return;
        }

        TransitionResult result = element->execute(); // user code, unlocked

        if (result == TransitionResult::Awaiting) {
            statusPort_->write(static_cast<int>(statusSnapshot));
            return;
        }

        if (result == TransitionResult::Failed) {
            bool applied = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                // Only apply if nothing else (a concurrent abort/pause/
                // restart) changed state while execute() ran unlocked; if
                // something did, that call already ran its own onExit().
                if (currentElement_ == element && status_ == PipelineStatus::Executing) {
                    currentElement_ = nullptr;
                    status_ = PipelineStatus::Failed;
                    applied = true;
                }
                statusSnapshot = status_;
            }
            if (applied)
                element->onExit(); // user code, unlocked
            statusPort_->write(static_cast<int>(statusSnapshot));
            return;
        }

        MissionElement* next = nullptr;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = transitions_.find(element->name());
            if (it != transitions_.end()) {
                for (const auto& t : it->second) {
                    if (t.condition == result) {
                        auto eit = elements_.find(t.targetName);
                        if (eit != elements_.end()) {
                            next = eit->second;
                        }
                        break;
                    }
                }
            }
        }

        if (!next) {
            bool applied = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (currentElement_ == element && status_ == PipelineStatus::Executing) {
                    currentElement_ = nullptr;
                    status_ = PipelineStatus::Finished;
                    applied = true;
                }
                statusSnapshot = status_;
            }
            if (applied)
                element->onExit();
            statusPort_->write(static_cast<int>(statusSnapshot));
            return;
        }

        bool applied = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (currentElement_ == element && status_ == PipelineStatus::Executing) {
                currentElement_ = next;
                applied = true;
            }
            statusSnapshot = status_;
        }
        if (applied) {
            element->onExit();
            next->onEnter();
        }

        statusPort_->write(static_cast<int>(statusSnapshot));
    }

    void processAsync() override {
        if (!controlPort_->wasUpdated())
            return;
        int cmd = controlPort_->read();
        switch (cmd) {
            case PipelineControl::ABORT:
                abort();
                break;
            case PipelineControl::RESTART:
                restart();
                break;
            case PipelineControl::PAUSE:
                pause();
                break;
            case PipelineControl::RESUME:
                resume();
                break;
            default:
                break;
        }
    }

    void reset() override {
        MissionElement* toExit = nullptr;
        std::vector<MissionElement*> toReset;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            toExit = currentElement_;
            currentElement_ = nullptr;
            toReset.reserve(elements_.size());
            for (auto& kv : elements_)
                toReset.push_back(kv.second);
            status_ = PipelineStatus::Idle;
        }
        if (toExit)
            toExit->onExit();
        for (auto* e : toReset)
            e->reset();
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

    // Precondition: caller holds stateMutex_. Checks that startElementName_
    // and every transitions_ from/to key name an element present in
    // elements_; returns false and sets firstError to the first dangling
    // reference found (start element checked first, then transitions_ in
    // iteration order) otherwise leaves firstError untouched.
    bool validateGraph(std::string& firstError) const {
        if (elements_.find(startElementName_) == elements_.end()) {
            firstError = "MissionPipeline '" + getName() + "': start element '" +
                         startElementName_ + "' was never added via addElement()";
            return false;
        }
        for (const auto& fromKv : transitions_) {
            if (elements_.find(fromKv.first) == elements_.end()) {
                firstError = "MissionPipeline '" + getName() + "': transition source '" +
                             fromKv.first + "' was never added via addElement()";
                return false;
            }
            for (const auto& t : fromKv.second) {
                if (elements_.find(t.targetName) == elements_.end()) {
                    firstError = "MissionPipeline '" + getName() + "': transition '" +
                                 fromKv.first + "' -> '" + t.targetName +
                                 "' targets an element that was never added via addElement()";
                    return false;
                }
            }
        }
        return true;
    }

    // Guards status_, currentElement_, elements_, transitions_, and
    // startElementName_. Non-recursive: never held across a MissionElement
    // hook call (onEnter/onExit/execute/reset are user code).
    mutable std::mutex stateMutex_;

    std::unordered_map<std::string, MissionElement*> elements_;
    std::unordered_map<std::string, std::vector<Transition>> transitions_;
    std::string startElementName_;

    MissionElement* currentElement_{nullptr};
    PipelineStatus status_{PipelineStatus::Idle};

    AsyncInputPort<int>* controlPort_;
    OutputPort<int>* statusPort_;
};

} // namespace axonvex::core
