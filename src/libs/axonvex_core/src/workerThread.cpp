#include <axonvex_core/detail/workerThread.hpp>

namespace axonvex::core::detail {

namespace {
/**
 * Publishes the calling thread's id into `slot` on construction and clears
 * it (back to std::thread::id{}, the "no worker" sentinel isOnThisThread()
 * checks against) on destruction -- every exit path of the loop that owns
 * the guard, including an exception escaping the loop body, runs the
 * clear. Ids are reusable once a thread exits, so a stale id left behind
 * after a loop ends could later alias an unrelated thread; the clear is
 * not optional cleanup, it is the correctness condition. (C41/C42 -- this
 * is the one guard shape system.cpp and timingController.cpp each
 * hand-wrote a private copy of; WorkerThread is the single place it now
 * lives.)
 */
class ThreadIdGuard {
  public:
    explicit ThreadIdGuard(std::atomic<std::thread::id>& slot) : slot_(slot) {
        slot_.store(std::this_thread::get_id());
    }
    ~ThreadIdGuard() {
        slot_.store(std::thread::id{});
    }
    ThreadIdGuard(const ThreadIdGuard&) = delete;
    ThreadIdGuard& operator=(const ThreadIdGuard&) = delete;

  private:
    std::atomic<std::thread::id>& slot_;
};
} // namespace

WorkerThread::~WorkerThread() {
    // Self-check BEFORE acquiring handleMutex_ (widened-critical-section
    // fix, review finding on the Part 2 migration): an external join() may
    // this instant hold handleMutex_ blocked inside a genuine
    // handle_->join() waiting for exactly this thread to return. Taking the
    // lock here first would block this thread on that same mutex, and it
    // can never be released -- the external join() is waiting for THIS
    // thread to unwind, which it cannot do while stuck acquiring the lock.
    // Same shape as join() below; isOnThisThread() is lock-free (reads only
    // the atomic id, never handle_) so this check itself never contends.
    if (isOnThisThread()) {
        // Self-destruct-on-own-thread: joining here would be a self-join
        // (std::thread::join() on your own thread throws system_error,
        // unwinding into std::terminate from inside the thread's own entry
        // point -- the exact C33 shape). Detach instead, same documented
        // trade-off as ~RealTimeScheduler()/~AxonVexSystem(): this avoids
        // the crash, NOT the underlying use-after-free if the detached loop
        // keeps running after members it captured are freed.
        if (handle_ && handle_->joinable()) {
            handle_->detach();
        }
        return;
    }

    std::lock_guard<std::mutex> lock(handleMutex_);
    if (handle_ && handle_->joinable()) {
        handle_->join();
    }
}

void WorkerThread::start(std::function<void()> loop, const std::function<void()>& beforeSpawn) {
    // Self-check BEFORE acquiring handleMutex_, same reasoning as join()/
    // the destructor below: a caller invoking start() from inside this
    // worker's own running loop (the class doc calls this undefined) must
    // not be able to widen into a deadlock against a concurrent external
    // join() holding the lock. Refusing here is a strictly safer failure
    // mode than the old pre-lock-hoist behavior (throw synchronously on an
    // uncontended mutex, or hang if contended) -- it is still the caller's
    // contract to never do this (starting a second loop over a live handle
    // would run two loop bodies concurrently), this just makes discovering
    // the bug survivable instead of fatal.
    if (isOnThisThread()) {
        return;
    }

    std::lock_guard<std::mutex> lock(handleMutex_);

    // C39: join a stale joinable handle before assigning a new thread over
    // it -- assigning over a joinable std::thread is std::terminate. The
    // isOnThisThread() refusal above already ruled out a self-join here.
    if (handle_ && handle_->joinable()) {
        handle_->join();
    }

    // C46 follow-up: strictly after the join above, strictly before the new
    // thread exists -- see the declaration comment for why this ordering is
    // load-bearing.
    if (beforeSpawn) {
        beforeSpawn();
    }

    handle_ = std::make_unique<std::thread>([this, loop = std::move(loop)]() mutable {
        ThreadIdGuard idGuard(threadId_);
        loop();
    });
}

bool WorkerThread::join() {
    // C33 shape, checked BEFORE acquiring handleMutex_ (review fix: the
    // Part 2 migration widened this critical section by moving the check
    // after the lock, which reintroduced the exact deadlock this refusal
    // exists to prevent -- an external join() already holding handleMutex_
    // blocked in a real handle_->join() waiting for this very thread; if
    // this call then blocked on the same mutex instead of returning
    // immediately, neither side could ever make progress). isOnThisThread()
    // is lock-free by construction (reads only the published atomic id),
    // so this check never itself contends for the lock it is trying to
    // avoid.
    if (isOnThisThread()) {
        // Refuse rather than self-join. The loop must notice its own stop
        // flag and exit on its own once this call returns and control
        // unwinds back out through it; the handle stays joinable for a
        // later external join() (or this object's destructor).
        return false;
    }

    std::lock_guard<std::mutex> lock(handleMutex_);
    if (handle_ && handle_->joinable()) {
        handle_->join();
        handle_.reset();
        return true;
    }
    return false;
}

void WorkerThread::withHandle(const std::function<void(std::thread&)>& fn) {
    std::lock_guard<std::mutex> lock(handleMutex_);
    if (handle_ && fn) {
        fn(*handle_);
    }
}

bool WorkerThread::isOnThisThread() const noexcept {
    return std::this_thread::get_id() == threadId_.load();
}

bool WorkerThread::joinable() const noexcept {
    std::lock_guard<std::mutex> lock(handleMutex_);
    return handle_ && handle_->joinable();
}

} // namespace axonvex::core::detail
