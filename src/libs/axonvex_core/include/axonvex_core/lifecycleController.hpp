/**
 * @file lifecycleController.hpp
 * @brief FSM, shutdown latch, refusal matrix, teardown ownership token
 * @author AxonVex Development Team
 *
 * Phase 2 core decomposition, migration step 6 -- LAST (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.3). Extracted from AxonVexSystem's
 * currentState_/stateMutex_/isShuttingDown_/shutdownMutex_/transitionState()/
 * isOnWorkerThread(). By the time this step runs, EventBus and HealthMonitor
 * already own their own worker threads -- this class aggregates their
 * published thread-ids (plus the scheduler's) through injected predicates
 * rather than holding any thread handle itself.
 */

#pragma once

#include <atomic>
#include <axonvex_core/systemEvent.hpp>
#include <functional>
#include <mutex>

namespace axonvex::core {

/**
 * @brief Owns the lifecycle FSM, the shutdown latch, the worker-thread
 * refusal matrix, and the teardown-ownership mutex for one AxonVexSystem.
 *
 * Threading contract: transition() is callable from any thread (today's
 * contract -- emergencyShutdown() fires from any thread via the SafetyHook).
 * clearShutdownLatch() must only be called from the (refusal-checked)
 * initialize() path -- single-owner clear, see C38 below. shutdownRequested()
 * is lock-free, polled by EventBus's and HealthMonitor's worker loops every
 * iteration.
 *
 * Inherited invariants (do not re-derive; carried verbatim from
 * AxonVexSystem's pre-extraction transitionState()/isOnWorkerThread()):
 *  - C7: FATAL_ERROR is a legal transition from ANY state and is never
 *    refused. FATAL_ERROR only accepts a transition to UNINITIALIZED. The
 *    façade's emergencyShutdown() calls transition(FATAL_ERROR) BEFORE
 *    requestShutdown() so the STATE_CHANGE event isn't dropped by EventBus's
 *    shutdown guard -- that ordering lives in emergencyShutdown(), not here,
 *    but this class's transition()/requestShutdown() split is what makes it
 *    expressible.
 *  - C38: clearShutdownLatch() is single-owner -- ONLY initialize() calls
 *    it; stop()/emergencyShutdown() call requestShutdown() (never clear).
 *    Two clearers would let a racing stop() be un-done. shutdownRequested()
 *    folds isShuttingDown_ OR'd with the ">= STOPPING" state guard into one
 *    predicate -- EventBus's dispatch loop and HealthMonitor's monitor loop
 *    poll THIS, never the raw state, and that guard is load-bearing: do not
 *    remove or weaken it.
 *  - C40/C41/C42: isOnWorkerThread() is the refusal-matrix aggregation.
 *    initialize()/stop()/reset() call it as their FIRST statement and refuse
 *    (return false/void) on true. It reads only the three injected
 *    predicates (each backed by a WorkerThread's published atomic id, or the
 *    scheduler's null-checked accessor) -- never a thread handle.
 *  - C2: acquireTeardown()/tryAcquireTeardown() expose the try_lock-vs-lock
 *    distinction in the type rather than at call sites. emergencyShutdown()
 *    try_locks (a blocking lock would deadlock stop()'s join of a system
 *    thread entering emergencyShutdown concurrently); stop()/the destructor
 *    lock (block until any concurrent teardown finishes).
 */
class LifecycleController {
  public:
    using StateChangeHook = std::function<void(SystemState oldState, SystemState newState)>;
    using Predicate = std::function<bool()>;

    /**
     * @param onTransitioned Invoked AFTER the state store, still under
     * stateMutex_ (no other lock is held) -- mirrors the façade's former
     * transitionState() tail (stats increment, log, notify/publish the
     * STATE_CHANGE event). Must not call back into transition() itself
     * (same non-reentrancy contract as today's notifyStateChange, which
     * only enqueues onto EventBus).
     * @param onInvalidTransition Invoked (oldState, attemptedNewState) when
     * a transition is refused by the table -- mirrors the façade's former
     * inline warning log in transitionState().
     * @param isOnEventDispatchThread Wired to
     * `[this]{ return eventBus_.isOnDispatchThread(); }`.
     * @param isOnHealthMonitorThread Wired to
     * `[this]{ return healthMonitor_.isOnMonitorThread(); }`.
     * @param isOnSchedulerThread Wired to `[this]{ return timingController_
     * && timingController_->isOnSchedulerThread(); }` -- the null check
     * stays at the façade call site (same "unlocked read of a unique_ptr
     * lifecycle methods replace/reset is safe because every caller either
     * checks this on entry before mutating components, or arrives from
     * inside a live scheduler-thread call stack" argument the pre-
     * extraction isOnWorkerThread() body documented; that comment moves
     * with the lambda, not into this class).
     */
    LifecycleController(StateChangeHook onTransitioned, StateChangeHook onInvalidTransition,
                        Predicate isOnEventDispatchThread, Predicate isOnHealthMonitorThread,
                        Predicate isOnSchedulerThread);
    ~LifecycleController() = default;

    // Holds two mutexes (stateMutex_, shutdownMutex_): non-copyable,
    // non-movable (delete, never =default).
    LifecycleController(const LifecycleController&) = delete;
    LifecycleController& operator=(const LifecycleController&) = delete;
    LifecycleController(LifecycleController&&) = delete;
    LifecycleController& operator=(LifecycleController&&) = delete;

    /// Validated FSM transition (see class doc for C7/C38). Returns false
    /// (without storing) if the transition is not in the table.
    bool transition(SystemState newState);

    SystemState state() const noexcept;

    /// C38: single-owner latch clear. Callable from any thread mechanically,
    /// but the façade contract is that only initialize() (after its own
    /// worker-thread refusal check and the C39 stale-handle joins) may call
    /// this.
    void clearShutdownLatch() noexcept;

    /// Sets the shutdown latch. stop()/emergencyShutdown() call this; never
    /// a clearer.
    void requestShutdown() noexcept;

    bool isShuttingDown() const noexcept;

    /// C38: isShuttingDown() OR'd with "state() >= SystemState::STOPPING" --
    /// the load-bearing predicate EventBus's and HealthMonitor's worker
    /// loops poll every iteration instead of the raw state.
    bool shutdownRequested() const noexcept;

    /// C40/C41/C42 refusal-matrix aggregation (see class doc).
    bool isOnWorkerThread() const noexcept;

    /// Blocking: waits for any concurrent teardown (a try_lock owner) to
    /// finish, then owns the token itself.
    std::unique_lock<std::mutex> acquireTeardown();

    /// Non-blocking: does not own the lock if another teardown already
    /// does (check `.owns_lock()`). C2: this is the shape emergencyShutdown()
    /// must use -- a blocking lock here would deadlock stop()'s join of a
    /// system thread that entered emergencyShutdown() concurrently.
    std::unique_lock<std::mutex> tryAcquireTeardown();

  private:
    StateChangeHook onTransitioned_;
    StateChangeHook onInvalidTransition_;
    Predicate isOnEventDispatchThread_;
    Predicate isOnHealthMonitorThread_;
    Predicate isOnSchedulerThread_;

    mutable std::atomic<SystemState> currentState_{SystemState::UNINITIALIZED};
    mutable std::mutex stateMutex_;
    std::atomic<bool> isShuttingDown_{false};
    // Serializes thread-handle teardown between stop() and
    // emergencyShutdown(), which can fire from any thread via the SafetyHook
    // (C2).
    std::mutex shutdownMutex_;
};

} // namespace axonvex::core
