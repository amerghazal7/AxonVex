/**
 * @file workerThread.hpp
 * @brief Owns one worker thread plus the published-thread-id protocol.
 *
 * Phase 2 core decomposition, migration step 3 (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.2). Replaces three separate hand-written copies of the same
 * discipline: system.cpp's eventProcessingThread_/eventThreadId_ and
 * monitoringThread_/monitoringThreadId_, and timingController.cpp's
 * schedulerThread_/schedulerThreadId_ -- each paired with its own
 * anonymous-namespace WorkerThreadIdGuard and join/refusal choreography.
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace axonvex::core::detail {

/**
 * @brief Owns one worker thread plus the published-thread-id protocol used
 * throughout AxonVexSystem/RealTimeScheduler's lifecycle.
 *
 * Invariants carried from the three originals this replaces -- do not
 * re-derive; tests reference these by name:
 *  - C41/C42: the loop publishes its own id as the first act inside the
 *    thread entry point and clears it on every exit path (RAII guard),
 *    including an exception escaping the loop -- isOnThisThread() reads
 *    ONLY that atomic id, never the std::thread handle (handle reads race
 *    start()'s move-assignment, C36).
 *  - C39: start() joins a stale joinable handle BEFORE assigning a new
 *    std::thread over it (assigning over a joinable std::thread is an
 *    uncatchable std::terminate).
 *  - C33-shape: join() self-checks isOnThisThread() and refuses (returns
 *    false) rather than self-joining -- std::thread::join() on your own
 *    thread throws system_error, unwinding into std::terminate from inside
 *    the thread's own entry function.
 *  - C46 (Part 1 lifecycle-hang fix, RealTimeScheduler::start()/stop()):
 *    start()/join()/withHandle() are all serialized against each other by
 *    the SAME mutex that owns the handle, so an external thread's start()
 *    can never race a concurrent join() (e.g. an event-callback-triggered
 *    teardown) on the handle itself -- the exact mechanism that produced
 *    the reported hang (join() blocking on a torn/stale thread id nothing
 *    will ever signal). This is the one thing the three original copies
 *    did NOT share; every caller of this class gets it for free.
 *
 * Threading contract: start()/join()/withHandle() are safe to call
 * concurrently from multiple external threads. The caller-supplied `loop`
 * must poll its own stop condition -- this class deliberately does not own
 * one, so callers keep their existing running_/isShuttingDown_-style flags
 * exactly as before this extraction; ordering that flag relative to
 * start()/join() (e.g. "whichever finishes its critical section last
 * wins") remains the caller's responsibility, same as C46's fix at the
 * RealTimeScheduler level.
 *
 * start()'s precondition: the caller has already refused if
 * isOnThisThread() -- this class does not know the caller's refusal
 * policy (system.cpp/timingController.cpp each log a different message),
 * so that check and its error handling stay at the call site. Calling
 * start() from the worker's own thread is undefined by this class (it
 * will attempt to join its own running thread, which throws
 * system_error synchronously rather than deadlocking, but that exception
 * is the caller's to handle or avoid).
 */
class WorkerThread {
  public:
    WorkerThread() = default;

    /// Non-owning cleanup contract: if a joinable handle remains, detach
    /// rather than join when destruction is itself happening on the worker
    /// thread (the same "a destructor cannot refuse" shape documented on
    /// ~AxonVexSystem -- reachable when the owning object is destroyed
    /// from inside the very loop this class runs). Detaching avoids
    /// std::terminate but does NOT fix the underlying use-after-free
    /// hazard of a live loop outliving members it captured by reference or
    /// pointer -- callers own that contract exactly as before this
    /// extraction; this destructor is deliberately not virtual (not a base
    /// class).
    ~WorkerThread();

    // Holds a mutex_: non-copyable, non-movable (delete, never =default).
    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    /// Joins any stale joinable handle (C39), then runs `beforeSpawn` (if
    /// non-null, still holding the same lock), then starts `loop` on a
    /// freshly constructed std::thread. The thread-id publish/clear
    /// (C41/C42) happens automatically around `loop` -- callers must not
    /// wrap it with their own guard.
    ///
    /// `beforeSpawn` exists so a caller can flip its own running-flag (or
    /// similar) to the "started" state -- e.g.
    /// RealTimeScheduler::running_ -- from INSIDE this lock, in between the
    /// stale-handle join and the new thread's creation. That ordering is
    /// load-bearing, not stylistic: a stale loop reads that very flag to
    /// decide whether to exit, so setting it before the stale handle has
    /// actually been joined lets the stale loop observe a "revived" flag
    /// and never exit -- turning the join above into an unbounded hang.
    /// (C46 follow-up: this is exactly the defect a first version of this
    /// migration reintroduced by setting the flag at the call site before
    /// calling start() -- confirmed via gdb: the join above stuck forever
    /// on a scheduler thread that could never see running_ go false again,
    /// while the external stop() that would have cleared it first was
    /// blocked on the SAME caller-side lock this call already holds.
    /// Matches the addAndRun()/withHandle() shape elsewhere in this
    /// decomposition: caller code runs INSIDE the lock instead of two
    /// round trips that can be reordered by whoever calls this.)
    void start(std::function<void()> loop, const std::function<void()>& beforeSpawn = nullptr);

    /// Joins the handle if joinable, UNLESS this would be a self-join
    /// (returns false and does nothing in that case -- C33 shape; the loop
    /// must notice its own stop flag and exit on its own instead, and a
    /// later call to join() from a real external thread reaps it). Safe to
    /// call concurrently with start()/join() from any thread -- all three
    /// take the same internal lock (C46).
    bool join();

    /// Runs `fn(*handle)` while holding the same internal lock as
    /// start()/join(), iff a handle currently exists -- e.g. tuning OS
    /// thread priority/affinity on the live thread. No-op if no handle
    /// exists yet. Matches the addAndRun()/forEach() shape elsewhere in
    /// this decomposition (see UnitRegistry): the caller's code runs
    /// INSIDE the lock so a concurrent start()/join() cannot free or
    /// replace the handle out from under it. `fn` must not call back into
    /// this WorkerThread (the lock is not recursive) and must not block.
    void withHandle(const std::function<void(std::thread&)>& fn);

    /// Reads only the published atomic id -- never the thread handle
    /// itself (C36).
    bool isOnThisThread() const noexcept;

    /// True if a handle exists and has not yet been joined. Racy by
    /// nature against a concurrent start()/join() from another thread;
    /// callers needing an atomic decision must add their own
    /// synchronization on top (as every caller of this class already does
    /// via its own lifecycle refusal ordering).
    bool joinable() const noexcept;

  private:
    mutable std::mutex handleMutex_;
    std::unique_ptr<std::thread> handle_;
    // Published by the loop itself as its first act, cleared as its last
    // (every exit path, including an escaping exception) -- never read
    // from handle_ (which start()/join() may be busy tearing down).
    std::atomic<std::thread::id> threadId_{};
};

} // namespace axonvex::core::detail
