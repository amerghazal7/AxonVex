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
    std::lock_guard<std::mutex> lock(handleMutex_);
    if (handle_ && handle_->joinable()) {
        if (isOnThisThread()) {
            // Self-destruct-on-own-thread: joining here would be a
            // self-join (std::thread::join() on your own thread throws
            // system_error, unwinding into std::terminate from inside the
            // thread's own entry point -- the exact C33 shape). Detach
            // instead, same documented trade-off as
            // ~RealTimeScheduler()/~AxonVexSystem(): this avoids the crash,
            // NOT the underlying use-after-free if the detached loop keeps
            // running after members it captured are freed.
            handle_->detach();
        } else {
            handle_->join();
        }
    }
}

void WorkerThread::start(std::function<void()> loop, const std::function<void()>& beforeSpawn) {
    std::lock_guard<std::mutex> lock(handleMutex_);

    // C39: join a stale joinable handle before assigning a new thread over
    // it -- assigning over a joinable std::thread is std::terminate. The
    // caller is responsible for having already refused if
    // isOnThisThread() (see class comment); reaching here on the worker's
    // own thread attempts a self-join, which throws rather than deadlocks.
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
    std::lock_guard<std::mutex> lock(handleMutex_);

    if (isOnThisThread()) {
        // C33 shape: refuse rather than self-join. The loop must notice its
        // own stop flag and exit on its own once this call returns and
        // control unwinds back out through it; the handle stays joinable
        // for a later external join() (or this object's destructor).
        return false;
    }

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
