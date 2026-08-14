/**
 * @file eventBus.hpp
 * @brief Event pool + queue + dispatch thread + callback registry
 * @author AxonVex Development Team
 *
 * Phase 2 core decomposition, migration step 4 (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.4). Extracted from AxonVexSystem's
 * eventPool_/eventQueue_/eventWorker_/eventProcessingRunning_/eventCallbacks_/
 * publishEvent()/eventProcessingLoop()/drainEventQueue().
 *
 * LifecycleController (migration step 6, last) will eventually own the
 * shutdown-latch/state-guard predicates this class is handed at construction;
 * until then AxonVexSystem supplies them as callables so EventBus never reads
 * AxonVexSystem's private state directly (see the constructor doc).
 */

#pragma once

#include <atomic>
#include <axonvex_core/detail/workerThread.hpp>
#include <axonvex_core/systemEvent.hpp>
#include <axonvex_core/utils/containers/memoryPool.hpp>
#include <axonvex_core/utils/containers/threadSafeQueue.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace axonvex::core {

using axonvex::utils::containers::MemoryPool;
using axonvex::utils::containers::ThreadSafeQueue;

class Logger; // non-owned; see the constructor's `logger` parameter doc.

/**
 * @brief Owns the event pool, event queue, dispatch thread, and event
 * callback registry for one AxonVexSystem.
 *
 * Threading contract: publish() is RT-legal by construction -- pool alloc +
 * bounded queue push, no locks beyond ThreadSafeQueue's internals, no user
 * callbacks. registerCallback()/unregisterCallback() are callable from any
 * thread. The dispatch loop's callback dispatch is C18 snapshot-then-invoke:
 * callbacks_ is copied under callbacksMutex_, the lock is released, then each
 * callback runs -- never under the lock.
 *
 * Inherited invariants (do not re-derive; carried verbatim from
 * AxonVexSystem's pre-extraction publishEvent()/eventProcessingLoop()/
 * drainEventQueue()):
 *  - C25: publish() drops the event once isShuttingDown() is true (the
 *    dispatch loop is stopping/stopped and will not drain it -- allocating
 *    anyway would leak). drain() returns every still-queued event to the
 *    pool WITHOUT dispatching it; call sites (façade): stop() (via
 *    stopAndJoin(), conditioned on a successful join), emergencyShutdown()
 *    (unconditionally -- events can be queued before the dispatch thread
 *    ever starts), cleanupComponents() (unconditionally, same reason).
 *  - C25 cross-pool hazard: reinitialize() replaces the pool/queue outright.
 *    Callers must have already joined the dispatch thread (join()) first --
 *    not self-enforced here, same "orchestration stays at the call site"
 *    split as UnitRegistry/SystemPortRegistry -- so no live loop can still be
 *    deallocating into the pool being replaced.
 *  - C18: the dispatch loop snapshots callbacks_ under callbacksMutex_,
 *    releases, then invokes -- never under the lock.
 *  - C38: the dispatch loop polls `shutdownRequested()` (merges the old
 *    `!isShuttingDown_.load()` while-condition and the loop-body's
 *    `currentState_.load() >= SystemState::STOPPING` break into one
 *    predicate call -- same net effect, restated as a single call per the
 *    design spec).
 *  - C41/C39: id publish/clear and join-before-assign are
 *    detail::WorkerThread's job (dispatchThread_ below); this class never
 *    duplicates that discipline.
 */
class EventBus {
  public:
    using EventCallback = std::function<void(const SystemEvent&)>;
    using Predicate = std::function<bool()>;
    using LoggerAccessor = std::function<Logger*()>;

    /**
     * @param logger Non-owned accessor into the façade's Logger (e.g.
     * `[this] { return logger_.get(); }`) -- logger_ is a `unique_ptr`
     * replaced/reset across initialize()/cleanupComponents(), so this class
     * calls `logger()` at each use rather than caching a Logger* that could
     * go stale. May return null (before initializeComponents() has run, or
     * after cleanupComponents()) -- every use null-checks the result.
     * @param isShuttingDown Mirrors AxonVexSystem's isShuttingDown_ latch;
     * publish() drops events once this returns true (C25).
     * @param shutdownRequested Mirrors "isShuttingDown_ || currentState_ >=
     * SystemState::STOPPING" (C38); the dispatch loop polls this, not
     * isShuttingDown alone.
     * @param onEventDropped Mirrors statistics_.errorCount.fetch_add(1) --
     * called once per dropped event (queue-full or pool-exhausted). May be
     * empty (no-op).
     */
    EventBus(LoggerAccessor logger, Predicate isShuttingDown, Predicate shutdownRequested,
             std::function<void()> onEventDropped);
    ~EventBus() = default;

    // Holds a mutex_ (via callbacksMutex_) and a detail::WorkerThread:
    // non-copyable, non-movable (delete, never =default).
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /// (Re)creates the pool and queue, discarding any previous ones.
    /// Precondition (caller's job, not self-enforced -- see class doc):
    /// join() has already reaped any stale dispatch thread. Mirrors
    /// AxonVexSystem::initializeComponents()'s former
    /// eventPool_/eventQueue_ construction verbatim.
    void reinitialize(size_t poolSize, size_t queueSize);

    /// RT-legal by construction. No-op if reinitialize() has not run yet
    /// (publish on a never-initialized system, e.g. emergencyShutdown()
    /// before initialize()) or once isShuttingDown() is true (C25).
    void publish(const SystemEvent& event);

    /// Returns 0 (without touching the id counter) for a null callback --
    /// matches the façade's former registerEventCallback() guard.
    uint32_t registerCallback(EventCallback cb);
    void unregisterCallback(uint32_t id);

    /// Clears every registered callback and resets the id counter to 1 --
    /// the event-callback half of AxonVexSystem::reset()'s former
    /// callbacksMutex_ block.
    void resetCallbacks() noexcept;

    /// Starts the dispatch thread via detail::WorkerThread. running_ is
    /// flipped true from INSIDE WorkerThread::start()'s beforeSpawn hook --
    /// after the stale-handle join, before the new thread exists -- so this
    /// call is self-contained and correct even if the caller has not
    /// separately joined a stale handle first (see workerThread.hpp's
    /// beforeSpawn doc for why that ordering matters, C46).
    void start();

    /// Bare join: no flag touch, no drain. For callers (initialize()'s C39
    /// pre-join) that only need a stale handle out of the way before
    /// reinitialize() replaces the pool/queue wholesale -- draining into
    /// containers about to be discarded is pointless work, not a
    /// correctness requirement. Refuses (returns false) on self-join, same
    /// as detail::WorkerThread::join().
    bool join();

    /// Non-blocking, callable from any thread including the dispatch thread
    /// itself: sets running_ = false. Kept separate from stopAndJoin() so a
    /// caller can flip it BEFORE taking a teardown lock a concurrent
    /// emergencyShutdown() running on the dispatch thread might be
    /// try_locking (C2) -- that racing thread's own loop must see running_
    /// go false promptly, without waiting for this call's join.
    void requestStop() noexcept;

    /// requestStop() + join(); if the join actually reaped a thread, also
    /// drain()s (matches the façade's former "if (eventWorker_.join())
    /// drainEventQueue()" shape in stop()). Callers needing drain()
    /// unconditionally regardless of the join's result (C25 -- events can
    /// be queued before the thread ever starts, e.g. emergencyShutdown())
    /// call drain() again themselves; it is idempotent on an already-empty
    /// queue.
    bool stopAndJoin();

    /// C25 drain point: returns every still-queued event to the pool
    /// WITHOUT dispatching it. No-op if reinitialize() has not run yet.
    void drain() noexcept;

    bool isOnDispatchThread() const noexcept;

  private:
    void dispatchLoop();

    LoggerAccessor logger_;
    Predicate isShuttingDown_;
    Predicate shutdownRequested_;
    std::function<void()> onEventDropped_;

    std::unique_ptr<MemoryPool<SystemEvent>> eventPool_;
    std::unique_ptr<ThreadSafeQueue<SystemEvent*>> eventQueue_;
    detail::WorkerThread dispatchThread_;
    std::atomic<bool> running_{false};

    mutable std::mutex callbacksMutex_;
    std::vector<EventCallback> callbacks_;
    std::atomic<uint32_t> nextCallbackId_{1};
};

} // namespace axonvex::core
