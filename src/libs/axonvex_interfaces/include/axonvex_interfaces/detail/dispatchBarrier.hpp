/**
 * @file dispatchBarrier.hpp
 * @brief Run callback dispatch outside the registry lock without dangling.
 *
 * The transports used to invoke user callbacks while holding `cbMutex_` (C34) —
 * the same class of defect as C12 and C18: a callback that takes its own lock in
 * the opposite order, or that calls back into the transport's own registration
 * API, deadlocks. The obvious fix, snapshotting the handler list and dispatching
 * unlocked, trades that deadlock for a worse bug: the snapshot holds raw
 * `Callback*` pointers the transport does not own, so an unregister racing an
 * in-flight dispatch leaves them dangling. That is precisely how C18 corrupted a
 * return address.
 *
 * This barrier keeps both properties. Dispatch snapshots under the lock, marks
 * itself in flight, and releases the lock before running user code; an
 * unregister from another thread waits until every in-flight dispatch has
 * drained, so a handler cannot be torn down while it is being called.
 *
 * An unregister issued *from inside a callback* must not wait — it would be
 * waiting on itself, and the dispatch it is waiting to drain is its own. Each
 * thread's dispatch depth is tracked so that case returns immediately. The
 * consequence, which callers must know: a handler that unregisters itself may
 * still be invoked for the remainder of the dispatch already under way.
 *
 * The tracking is per thread, not a single "current dispatcher" id. One
 * transport shares this barrier between the message path (receive thread) and
 * the error path (reachable from send() on any caller thread), so two threads
 * can be dispatching simultaneously and a single field would only remember the
 * later one — leaving the earlier thread unable to recognise itself and
 * deadlocked waiting on its own dispatch. `SafetyManager`'s `evaluatingThread_`
 * from C12 can be a single id only because it serialises evaluation cycles
 * first; this barrier deliberately does not serialise dispatch.
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <thread>

namespace axonvex {
namespace interfaces {
namespace detail {

class DispatchBarrier {
  public:
    /// Mark a dispatch as starting. Must be called with the registry mutex held.
    ///
    /// The map insert is what makes this able to throw, so it happens before
    /// inFlight_ moves: a bad_alloc then leaves the barrier exactly as it was
    /// rather than stranding a count that no end() will ever balance.
    void begin() {
        ++depthByThread_[std::this_thread::get_id()];
        ++inFlight_;
    }

    /// Mark a dispatch as finished. Must be called with the registry mutex held.
    void end() noexcept {
        auto it = depthByThread_.find(std::this_thread::get_id());
        if (it != depthByThread_.end() && --it->second == 0) {
            depthByThread_.erase(it);
        }
        if (inFlight_ > 0 && --inFlight_ == 0) {
            quiescent_.notify_all();
        }
    }

    /// Block until no dispatch is in flight. Must be called with @p lock held on
    /// the registry mutex; returns immediately when the calling thread is itself
    /// inside a dispatch, because it cannot wait for itself.
    ///
    /// Ceiling: this waits for *global* quiescence, not for the one handler
    /// being removed. On a transport saturated enough that a dispatch is always
    /// in flight, an unregister from an outside thread can be delayed. Bounded
    /// in practice because a dispatch is one user callback and inFlight_ hits
    /// zero between messages; if that stops being true, track liveness per
    /// handler instead of per barrier.
    void waitQuiescent(std::unique_lock<std::mutex>& lock) {
        if (depthByThread_.find(std::this_thread::get_id()) != depthByThread_.end()) {
            return;
        }
        quiescent_.wait(lock, [this]() { return inFlight_ == 0; });
    }

  private:
    /// Per-thread dispatch depth, NOT a single "current dispatcher".
    ///
    /// A lone thread::id was unsound: both transports share one barrier between
    /// the message path (receive thread) and the error path (reachable from
    /// send() on any caller thread), so two threads can be dispatching at once
    /// and a single field only remembers whichever called begin() last. A thread
    /// that entered dispatch first would then fail its own self-check, wait for
    /// inFlight_ to reach zero, and block forever — its own in-flight dispatch
    /// is the one it is stuck inside, so the count can never drain. The map
    /// answers "am I dispatching" per thread, and doubles as a recursion depth
    /// so nested same-thread dispatch still balances.
    std::size_t inFlight_{0};
    std::map<std::thread::id, std::size_t> depthByThread_;
    std::condition_variable quiescent_;
};

/// Calls DispatchBarrier::end() on scope exit, re-taking @p mutex to do so.
/// Scoped so a throwing user callback cannot leave a dispatch permanently in
/// flight, which would wedge every future unregister.
class ScopedDispatch {
  public:
    ScopedDispatch(DispatchBarrier& barrier, std::mutex& mutex) noexcept
        : barrier_(barrier), mutex_(mutex) {}

    ~ScopedDispatch() {
        std::lock_guard<std::mutex> lock(mutex_);
        barrier_.end();
    }

    ScopedDispatch(const ScopedDispatch&) = delete;
    ScopedDispatch& operator=(const ScopedDispatch&) = delete;

  private:
    DispatchBarrier& barrier_;
    std::mutex& mutex_;
};

} // namespace detail
} // namespace interfaces
} // namespace axonvex
