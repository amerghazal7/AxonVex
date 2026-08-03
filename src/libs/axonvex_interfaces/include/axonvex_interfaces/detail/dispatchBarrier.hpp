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
 * waiting on itself. The dispatching thread is recorded so that case returns
 * immediately. The consequence, which callers must know: a handler that
 * unregisters itself may still be invoked for the remainder of the dispatch
 * already under way. This mirrors `SafetyManager`'s `evaluatingThread_` guard
 * from C12.
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

namespace axonvex {
namespace interfaces {
namespace detail {

class DispatchBarrier {
  public:
    /// Mark a dispatch as starting. Must be called with the registry mutex held.
    void begin() noexcept {
        ++inFlight_;
        dispatcher_ = std::this_thread::get_id();
    }

    /// Mark a dispatch as finished. Must be called with the registry mutex held.
    void end() noexcept {
        if (inFlight_ > 0 && --inFlight_ == 0) {
            dispatcher_ = std::thread::id();
            quiescent_.notify_all();
        }
    }

    /// Block until no dispatch is in flight. Must be called with @p lock held on
    /// the registry mutex; returns immediately when called from the dispatching
    /// thread, because that thread cannot wait for itself.
    void waitQuiescent(std::unique_lock<std::mutex>& lock) {
        if (dispatcher_ == std::this_thread::get_id()) {
            return;
        }
        quiescent_.wait(lock, [this]() { return inFlight_ == 0; });
    }

  private:
    std::size_t inFlight_{0};
    std::thread::id dispatcher_{};
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
