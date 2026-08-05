#pragma once

#include <axonvex_core/missionElement.hpp>
#include <axonvex_core/processingUnit.hpp>

#include <atomic>
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
 *
 * A command issued while the pipeline is not being ticked (no processAsync()
 * calls arriving) is held, not dropped: the underlying slot is last-write-
 * wins, and the held command takes effect only once ticking resumes and a
 * processAsync() call drains it — never silently, and never without a tick.
 */
class MissionPipeline : public ProcessingUnit {
  public:
    explicit MissionPipeline(const std::string& name) : ProcessingUnit(name) {
        // threadSafe=true: post-finding-1, abort()/restart()/pause()/resume()
        // write to this port directly from whatever thread calls them, while
        // processAsync() reads it from the scheduler thread — AsyncInputPort's
        // data_/wasUpdated_ are only mutex-guarded when threadSafe is set
        // (C31: fixed for the port's lifetime, chosen at construction).
        // Without this, update()/read() race on the plain (non-threadSafe)
        // path (TSan-confirmed while building this fix).
        controlPort_ = createAsyncInputPort<int>(0, "control", /*threadSafe=*/true);
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

    // C22 gave every one of these methods its own lock-decide-unlock-then-
    // dispatch-the-hook shape, which kept `stateMutex_` safe but missed a
    // second hazard: the *dispatch* still ran on the CALLER's thread. A
    // MissionElement's execute() runs unlocked on the scheduler thread
    // (see processSync()); abort()/restart()/reset() calling onExit()/
    // reset() on that same element from an arbitrary caller thread races
    // it — same object, unsynchronized writes, TSan-confirmed
    // (MissionPipelineTsanRegressionTest below).
    //
    // Fix: abort()/restart()/pause()/resume()/reset() no longer touch
    // MissionPipeline or MissionElement state at all when called directly.
    // They only enqueue a command (the *existing* single-slot controlPort_
    // for the four that already had a wire command, or the dedicated
    // pendingReset_ flag for reset() — see its comment for why it can't
    // share controlPort_) and return. The command is drained — and the
    // state flip and every MissionElement hook it triggers actually run —
    // on the scheduler thread only, at the top of the next processAsync()
    // (called by processAsyncBase() immediately before processSyncBase()
    // each tick — see timingController.cpp's executeTask()). This is true
    // even if the caller IS the scheduler thread already (e.g. a hook
    // re-entering pause()): every direct-call site funnels through the
    // same drain, so there is exactly one thread that ever calls a
    // MissionElement hook.
    //
    // Honest contract for every method below: it takes effect at the next
    // tick, not synchronously. status()/currentElementName() reflect the
    // state as of the most recently drained command, not the most
    // recently issued one. Like controlPort_ already did, the underlying
    // slot is single-entry / last-write-wins: issuing a second command
    // before the first has been drained discards the first (documented
    // pre-existing simplification, not new here).
    //
    // Also: a hook dispatched from the drain that calls
    // AxonVexSystem::stop()/reset()/initialize() is refused by the
    // worker-refusal mechanism (isOnWorkerThread(), C40/C41/C42) once this
    // pipeline is ticked by a real scheduler thread — those calls would
    // otherwise try to join/destroy the thread they're running on.

    /**
     * @brief Validate the graph and (if Idle) request that execution begin.
     *
     * Graph validation happens synchronously on the calling thread and is
     * fail-fast: a dangling transition reference sets status()==Failed and
     * getLastError() immediately, no tick required (there is no
     * MissionElement hook to race in the failure path). On success, the
     * Idle->Executing bookkeeping itself is deferred to the next tick's
     * drain, together with the start element's onEnter() — batch-6 review
     * found that flipping status_/currentElement_ here while onEnter() ran
     * later let a processSync() (or a queued reset()) land in the gap and
     * call execute()/onExit() on an element that had never been entered.
     * Deferring both together means status() still reads Idle immediately
     * after a successful call — it only reads Executing once a
     * processAsync() has actually drained the start.
     */
    void startPipeline() {
        std::string firstError;
        bool validationFailed = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (status_ != PipelineStatus::Idle)
                return;
            if (!validateGraph(firstError)) {
                status_ = PipelineStatus::Failed;
                validationFailed = true;
            }
        }
        if (validationFailed) {
            setError(firstError);
            return;
        }
        pendingStart_.store(true);
    }

    // Takes effect at the next tick; onExit() (if any) runs on the
    // scheduler thread, not this call's thread.
    void abort() {
        controlPort_->update(PipelineControl::ABORT);
    }

    // Takes effect at the next tick; onExit()/reset()/onEnter() (if any)
    // run on the scheduler thread, not this call's thread.
    void restart() {
        controlPort_->update(PipelineControl::RESTART);
    }

    // Takes effect at the next tick. pause()/resume() dispatch no
    // MissionElement hook themselves, but are still deferred for the same
    // reason as the others: a caller re-entering pause() from a hook
    // (HookReenteringControlApiDoesNotDeadlock) must not touch status_
    // opportunistically from whatever thread that hook happens to run on.
    void pause() {
        controlPort_->update(PipelineControl::PAUSE);
    }

    // Takes effect at the next tick.
    void resume() {
        controlPort_->update(PipelineControl::RESUME);
    }

    // -----------------------------------------------------------------
    // State
    // -----------------------------------------------------------------

    // Reflects the most recently *drained* command, not the most recently
    // issued one (see the "Execution control" comment above): right after
    // a successful startPipeline(), this still reads Idle until the next
    // tick's processAsync() performs the Idle->Executing flip.
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

    // Drain point for every deferred command (see the "Execution control"
    // comment above): called every tick, before processSync(), from the
    // scheduler thread only (processAsyncBase() -> processAsync() ->
    // processSyncBase(), see timingController.cpp's executeTask()). This
    // is the ONE place MissionElement hooks triggered by the direct
    // control API run.
    void processAsync() override {
        if (pendingReset_.exchange(false)) {
            resetImpl();
        }
        if (pendingStart_.exchange(false)) {
            dispatchDeferredEnter();
        }
        if (!controlPort_->wasUpdated())
            return;
        int cmd = controlPort_->read();
        switch (cmd) {
            case PipelineControl::ABORT:
                abortImpl();
                break;
            case PipelineControl::RESTART:
                restartImpl();
                break;
            case PipelineControl::PAUSE:
                pauseImpl();
                break;
            case PipelineControl::RESUME:
                resumeImpl();
                break;
            default:
                break;
        }
    }

    // Takes effect at the next tick (see the "Execution control" comment
    // above); onExit()/reset() (if any) run on the scheduler thread, not
    // this call's thread. Can't reuse controlPort_ like abort/restart/
    // pause/resume do: when this is invoked via the built-in resetPort_ ->
    // ProcessingUnit::resetBlock() path, resetBlock() calls resetPorts()
    // (which clears every AsyncInputPort's pending data, controlPort_
    // included) right after this returns — a controlPort_-based signal
    // would be wiped before processAsync() ever drained it. pendingReset_
    // isn't a port, so resetPorts() doesn't touch it.
    void reset() override {
        pendingReset_.store(true);
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

    // -----------------------------------------------------------------
    // Deferred-command implementations. Precondition for every method
    // below: called only from the scheduler thread, from processAsync()'s
    // drain (or, for startImmediate(), from dispatchDeferredEnter() or
    // restartImpl(), both themselves only ever reached the same way).
    // None of them may be called directly from a public entry point —
    // that's exactly the bug this fix closes.
    // -----------------------------------------------------------------

    // Bookkeeping + onEnter() dispatch, both synchronous — the two happen
    // together so an element can never be observed with one but not the
    // other. Safe to run inline (no further deferral) because the caller
    // is already guaranteed to be the scheduler thread. Shared by
    // dispatchDeferredEnter() (startPipeline()'s deferred start) and
    // restartImpl() (re-start after reset()).
    void startImmediate() {
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

    // Drains startPipeline()'s deferred start: performs the Idle->Executing
    // flip and onEnter() together, via startImmediate() (the same routine
    // restartImpl() uses to re-start after a reset), so an element can
    // never be seen with execute()/onExit() run against it but onEnter()
    // never having run. Drain order within a tick (processAsync()):
    // pendingReset_ runs before this; controlPort_ commands (abort/
    // restart/pause/resume) run after it, not before.
    void dispatchDeferredEnter() {
        startImmediate();
    }

    void abortImpl() {
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

    void restartImpl() {
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
        startImmediate(); // already on the scheduler thread; no further defer.
    }

    void pauseImpl() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (status_ == PipelineStatus::Executing) {
            status_ = PipelineStatus::Paused;
        }
    }

    void resumeImpl() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (status_ == PipelineStatus::Paused) {
            status_ = PipelineStatus::Executing;
        }
    }

    void resetImpl() {
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

    // Single-slot deferred commands that can't travel over controlPort_
    // (see reset()'s comment for why) or that predate it (startPipeline()
    // has no wire command of its own). pendingStart_ is a full pending
    // start, not just a pending onEnter(): the Idle->Executing flip AND
    // onEnter() both happen at the drain (dispatchDeferredEnter() ->
    // startImmediate()), never before it. Drained in processAsync(), on
    // the scheduler thread only.
    std::atomic<bool> pendingReset_{false};
    std::atomic<bool> pendingStart_{false};
};

} // namespace axonvex::core
