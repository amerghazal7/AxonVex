#include <algorithm>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/timingController.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#elif _WIN32
#include <timeapi.h>
#include <windows.h>
#endif

// Usage documentation for core utilities:
// - Use schedulerTimer_ to measure each scheduling cycle and track jitter/latency.
// - Use ThreadSafeQueue (e.g., commandQueue_) for thread-safe command/event scheduling.
// - Use MemoryPool (e.g., eventPool_) for real-time safe allocation of timing events/statistics.
// These members are available in the TimingController base class for use in implementation and
// extensions.

namespace axonvex::core {

// RealTimeScheduler Implementation
RealTimeScheduler::RealTimeScheduler(SchedulingPolicy policy) : policy_(policy) {
    // Initialize the task pool with a default capacity
    taskPool_ = std::make_unique<MemoryPool<SchedulerTask>>(128);
}

RealTimeScheduler::~RealTimeScheduler() {
    // C42: unconditional — stop() no longer early-returns on running_ (see
    // its definition below), so calling it here also reclaims a handle left
    // behind by an earlier deferred self-stop (running_ already false while
    // schedulerThread_ is still joinable).
    stop();

    if (schedulerThread_ && schedulerThread_->joinable()) {
        // Only reachable when ~RealTimeScheduler itself runs on the
        // scheduler thread: stop() above just deferred its own join for the
        // same reason (isOnSchedulerThread() true). That happens when the
        // owning TimingController/AxonVexSystem is destroyed from inside a
        // ProcessingUnit task's own call stack — the same contract
        // violation documented on ~AxonVexSystem (a destructor cannot
        // refuse), inherited here through TimingController's owning
        // unique_ptr. Joining would still be a self-join (C33/C36 shape,
        // std::terminate); detaching avoids the crash, NOT the underlying
        // use-after-free (a detached schedulerLoop can keep executing after
        // we free the members it uses). The self-destruction scenario remains
        // documented UB per the class @warning.
        schedulerThread_->detach();
    }
}

uint32_t RealTimeScheduler::addTask(ProcessingUnit* unit, const TimingConstraints& constraints) {
    if (!unit) {
        return 0; // Invalid task ID
    }

    std::unique_lock<std::mutex> lock(tasksMutex_);

    // Allocate a new task from the memory pool
    SchedulerTask* task = taskPool_->allocateObject();
    if (!task) {
        // Handle pool exhaustion — user callback runs outside the lock (C18 rule)
        lock.unlock();
        if (errorCallback_) {
            errorCallback_(nullptr, "Task pool exhausted");
        }
        return 0;
    }

    uint32_t taskId = nextTaskId_.fetch_add(1);
    task->unit = unit;
    task->constraints = constraints;
    task->taskId = taskId;
    task->name = unit->getName();
    task->nextExecution = std::chrono::steady_clock::now() + constraints.period;
    task->lastExecution = std::chrono::steady_clock::now();

    tasks_[taskId] = task;

    // Wake up scheduler thread if running
    if (running_.load()) {
        schedulerCondition_.notify_one();
    }

    return taskId;
}

bool RealTimeScheduler::removeTask(uint32_t taskId) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        SchedulerTask* task = it->second;
        task->active.store(false);
        tasks_.erase(it);
        if (task->executing.load()) {
            // Mid-execution on the scheduler thread: defer deallocation to the
            // scheduler loop, which checks pendingRemoval under tasksMutex_ (C1)
            task->pendingRemoval.store(true);
        } else {
            taskPool_->deallocateObject(task); // Deallocate back to the pool
        }
        return true;
    }
    return false;
}

void RealTimeScheduler::removeAllTasks() {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    for (auto& pair : tasks_) {
        SchedulerTask* task = pair.second;
        task->active.store(false);
        if (task->executing.load()) {
            // Same deferral as removeTask: the scheduler loop deallocates (C1)
            task->pendingRemoval.store(true);
        } else {
            taskPool_->deallocateObject(task);
        }
    }
    tasks_.clear();
}

bool RealTimeScheduler::updateTaskConstraints(uint32_t taskId,
                                              const TimingConstraints& constraints) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        it->second->constraints = constraints;
        // Recalculate next execution time
        it->second->nextExecution = calculateNextExecution(*it->second);
        return true;
    }
    return false;
}

void RealTimeScheduler::start() {
    // C44: latch first, before any early return, so the setup-only window
    // for setErrorCallback() closes the moment start() is invoked rather
    // than only once a new thread is actually spawned below.
    hasStarted_.store(true, std::memory_order_relaxed);

    // C46: serializes the running_ check and the schedulerThread_ handle
    // manipulation below against a concurrent stop() -- see lifecycleMutex_'s
    // declaration comment. Must wrap the running_.load() early-return too,
    // not just the thread creation: without that, this check and stop()'s
    // own handle teardown can interleave (check-then-act race across two
    // functions), which is exactly how the reported hang happened.
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (running_.load()) {
        return; // Already running
    }

    // C42 follow-up: read the published id before touching schedulerThread_
    // at all -- same discipline as stop(). Two hazards share this guard:
    //  1. Restart from the scheduler thread itself (a task, still inside
    //     this very call stack, calling start() again after its own
    //     deferred stop()): that thread has not unwound out of
    //     schedulerLoop() yet, so starting a second thread here would run
    //     two schedulerLoop()s over the same tasks_/schedulerThreadId_
    //     concurrently. Refuse outright -- mirrors the system-layer
    //     refusals (initialize()/stop()/reset() on isOnWorkerThread());
    //     detaching the old (still-live) loop instead would recreate the
    //     C41 hazard (its own stack racing what start() hands to a new
    //     thread).
    //  2. Restart from a different, external thread after another thread's
    //     deferred self-stop left schedulerThread_ joinable (running_
    //     already false, see stop()): assigning a new std::thread over a
    //     still-joinable one below is std::terminate -- the C39 shape one
    //     layer down. Join it first.
    if (isOnSchedulerThread()) {
        return;
    }
    if (schedulerThread_ && schedulerThread_->joinable()) {
        schedulerThread_->join();
    }

    running_.store(true);
    paused_.store(false);

    schedulerThread_ = std::make_unique<std::thread>(&RealTimeScheduler::schedulerLoop, this);

    // Set thread priority and affinity for real-time performance
    if (realTimeMode_.load()) {
        setThreadPriority(*schedulerThread_, SchedulerPriority::REAL_TIME);
        if (affinitySet_) {
            setThreadAffinity(*schedulerThread_, cpuCore_);
        }
    }
}

void RealTimeScheduler::stop() {
    // C42: no early return on running_ — after a deferred self-stop below,
    // the flag is already false while schedulerThread_ still needs
    // reclaiming, so every step here must be individually idempotent
    // instead of gated by one flag (SafetyManager/Watchdog C33/C36
    // precedent).
    //
    // Called unlocked, before lifecycleMutex_ below: halt() is the
    // documented non-blocking half (design spec Sec6.3) that
    // TimingController::emergencyStop() depends on staying lock-free for
    // its hot e-stop path, and calling it early also wakes a scheduler
    // thread already waiting on schedulerCondition_ with the lowest
    // possible latency in the common (uncontended) case.
    halt();

    // C46: everything from here down re-touches schedulerThread_/running_ and
    // must be mutually exclusive with a concurrent start() -- see
    // lifecycleMutex_'s declaration comment. running_ is re-cleared under the
    // lock (redundant with halt() above in the common case, but the
    // authoritative write when a concurrent start() raced ahead of halt()
    // and set it back to true from inside ITS locked section): whichever of
    // start()/stop() finishes its critical section last now determines the
    // final state, instead of a stale schedulerThread_ read producing a
    // join() on a thread id nothing will ever signal (the reported hang).
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    running_.store(false);
    schedulerCondition_.notify_all();

    // C42: checked before touching schedulerThread_. A ProcessingUnit task
    // body (or the error callback) runs ON this thread via executeTask(), so
    // self-detection must read only the published atomic id, never the
    // std::thread object itself (start() move-assigns it with no
    // happens-before edge to a concurrent reader).
    if (isOnSchedulerThread()) {
        // Self-stop: running_ is now false, so schedulerLoop() exits on its
        // own once this call returns and control unwinds back out through
        // executeTask()/processSync(). Joining here would be a self-join —
        // std::thread::join throws system_error, unwinding out of the
        // scheduler's own thread-entry lambda, i.e. std::terminate (C33's
        // exact shape; this is that defect's scheduler-thread instance,
        // C42). Defer the join and the handle reset: absorbed by the next
        // stop() call from a real external thread (the joinable() check
        // below runs there instead), or by ~RealTimeScheduler() if stop()
        // is never called again.
        return;
    }

    if (schedulerThread_ && schedulerThread_->joinable()) {
        schedulerThread_->join();
    }
    schedulerThread_.reset();
}

void RealTimeScheduler::halt() noexcept {
    // Design spec §6.3: the non-blocking half of what stop() does. Storing
    // haltRequestedAt_ before running_ costs nothing extra (both are single
    // relaxed/atomic ops) and means a reader can never observe running_
    // already false with haltRequestedAt_ still unset.
    auto now = std::chrono::steady_clock::now();
    haltRequestedAtNs_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),
        std::memory_order_relaxed);
    running_.store(false);
    schedulerCondition_.notify_all();
}

std::chrono::microseconds RealTimeScheduler::getHaltLatency() const noexcept {
    int64_t haltNs = haltRequestedAtNs_.load(std::memory_order_relaxed);
    int64_t returnNs = lastTaskReturnAtNs_.load(std::memory_order_relaxed);
    if (haltNs == 0 || returnNs == 0 || returnNs < haltNs) {
        return std::chrono::microseconds{0};
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::nanoseconds(returnNs - haltNs));
}

bool RealTimeScheduler::isOnSchedulerThread() const noexcept {
    return std::this_thread::get_id() == schedulerThreadId_.load();
}

void RealTimeScheduler::pause() {
    paused_.store(true);
}

void RealTimeScheduler::resume() {
    paused_.store(false);
    schedulerCondition_.notify_all();
}

void RealTimeScheduler::setSchedulingPolicy(SchedulingPolicy policy) {
    policy_ = policy;
}

void RealTimeScheduler::setThreadAffinity(uint32_t cpuCore) {
    cpuCore_ = cpuCore;
    affinitySet_ = true;

    if (schedulerThread_) {
        setThreadAffinity(*schedulerThread_, cpuCore_);
    }
}

void RealTimeScheduler::enableRealTimeMode(bool enable) {
    realTimeMode_.store(enable);

    if (enable && schedulerThread_) {
        setThreadPriority(*schedulerThread_, SchedulerPriority::REAL_TIME);
    }
}

void RealTimeScheduler::setTimerResolution(std::chrono::microseconds resolution) {
    timerResolution_ = resolution;

#ifdef _WIN32
    // Set Windows multimedia timer resolution
    if (realTimeMode_.load()) {
        timeBeginPeriod(static_cast<UINT>(resolution.count() / 1000));
    }
#endif
}

SchedulerStatistics RealTimeScheduler::getStatistics() const {
    // V5: no lock -- assembles the public DTO from independent relaxed
    // atomic loads. See AtomicStatistics's header comment for why no
    // stronger ordering/consistency is needed or was ever promised.
    SchedulerStatistics snapshot;
    snapshot.totalExecutions = statistics_.totalExecutions.load(std::memory_order_relaxed);
    snapshot.successfulExecutions =
        statistics_.successfulExecutions.load(std::memory_order_relaxed);
    snapshot.failedExecutions = statistics_.failedExecutions.load(std::memory_order_relaxed);
    snapshot.missedDeadlines = statistics_.missedDeadlines.load(std::memory_order_relaxed);
    snapshot.preemptions = statistics_.preemptions.load(std::memory_order_relaxed);
    snapshot.totalExecutionTime =
        std::chrono::microseconds(statistics_.totalExecutionTimeUs.load(std::memory_order_relaxed));
    snapshot.averageExecutionTime = std::chrono::microseconds(
        statistics_.averageExecutionTimeUs.load(std::memory_order_relaxed));
    snapshot.maxExecutionTime =
        std::chrono::microseconds(statistics_.maxExecutionTimeUs.load(std::memory_order_relaxed));
    snapshot.minExecutionTime =
        std::chrono::microseconds(statistics_.minExecutionTimeUs.load(std::memory_order_relaxed));
    snapshot.schedulingJitter =
        std::chrono::microseconds(statistics_.schedulingJitterUs.load(std::memory_order_relaxed));
    snapshot.averageSchedulingJitter = std::chrono::microseconds(
        statistics_.averageSchedulingJitterUs.load(std::memory_order_relaxed));
    // cpuUtilization / schedulabilityRatio: never written anywhere in this
    // class (pre-existing -- TimingController::validateSchedulability()
    // computes its own local utilization, never feeds this struct); default
    // 0.0 matches today's always-unset behavior exactly.
    return snapshot;
}

void RealTimeScheduler::resetStatistics() {
    // V5: no lock. A reset racing an in-flight scheduler-thread write is a
    // "last write wins" administrative op, not a per-tick contended path --
    // resetStatistics() is never called from schedulerLoop.
    statistics_.totalExecutions.store(0, std::memory_order_relaxed);
    statistics_.successfulExecutions.store(0, std::memory_order_relaxed);
    statistics_.failedExecutions.store(0, std::memory_order_relaxed);
    statistics_.missedDeadlines.store(0, std::memory_order_relaxed);
    statistics_.preemptions.store(0, std::memory_order_relaxed);
    statistics_.totalExecutionTimeUs.store(0, std::memory_order_relaxed);
    statistics_.averageExecutionTimeUs.store(0, std::memory_order_relaxed);
    statistics_.maxExecutionTimeUs.store(0, std::memory_order_relaxed);
    statistics_.minExecutionTimeUs.store(std::chrono::microseconds::max().count(),
                                         std::memory_order_relaxed);
    statistics_.schedulingJitterUs.store(0, std::memory_order_relaxed);
    statistics_.averageSchedulingJitterUs.store(0, std::memory_order_relaxed);
}

std::vector<uint32_t> RealTimeScheduler::getActiveTaskIds() const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    std::vector<uint32_t> taskIds;
    taskIds.reserve(tasks_.size());

    for (const auto& pair : tasks_) {
        if (pair.second->active.load()) {
            taskIds.push_back(pair.first);
        }
    }

    return taskIds;
}

TimingConstraints RealTimeScheduler::getTaskConstraints(uint32_t taskId) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        return it->second->constraints;
    }
    return TimingConstraints{};
}

bool RealTimeScheduler::isTaskActive(uint32_t taskId) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    return (it != tasks_.end()) && it->second->active.load();
}

void RealTimeScheduler::setCustomScheduler(CustomSchedulerCallback callback) {
    // C44-sibling fix: setup-only, no lock -- same shape as setErrorCallback,
    // reusing its hasStarted_ latch / requireNotStarted() helper rather than
    // a second mechanism. scheduleCustom() reads customScheduler_ unlocked on
    // the scheduler thread; this call used to write it under schedulerMutex_
    // while running, which is exactly the torn-std::function-read hazard C44
    // closed for errorCallback_.
    requireNotStarted("setCustomScheduler");
    customScheduler_ = callback;
}

void RealTimeScheduler::requireNotStarted(const char* what) const {
    if (hasStarted_.load(std::memory_order_relaxed)) {
        throw std::logic_error(
            std::string(what) +
            " must be called before the scheduler starts: executeTask() reads the error "
            "callback unlocked on the scheduler thread, so late registration would race it "
            "(C44)");
    }
}

void RealTimeScheduler::setErrorCallback(ErrorCallback callback) {
    // C44: setup-only, no lock — mirrors ports.hpp's requireNoTrafficYet
    // (C32). This does not close every window: a setErrorCallback() racing
    // the very first start() on another thread can still slip past the
    // check before hasStarted_ becomes visible, same caveat C32 documents.
    // That is a caller-contract violation (register before starting, from
    // one thread), not something a runtime check can fully repair.
    requireNotStarted("setErrorCallback");
    errorCallback_ = callback;
}

namespace {
/**
 * C42: publishes the calling thread's id into `slot` on construction and
 * clears it (back to std::thread::id{}, the "no scheduler thread" sentinel
 * isOnSchedulerThread() checks against) on destruction — every exit path of
 * the loop that owns the guard, including an exception escaping the loop
 * body, runs the clear. Copied from system.cpp's WorkerThreadIdGuard shape
 * (C41) rather than shared across translation units: same class name is
 * fine since both are anonymous-namespace (internal linkage), no ODR
 * conflict. Ids are reusable once a thread exits, so a stale id left behind
 * after the loop ends could later alias an unrelated thread; the clear is
 * not optional cleanup, it is the correctness condition.
 */
class WorkerThreadIdGuard {
  public:
    explicit WorkerThreadIdGuard(std::atomic<std::thread::id>& slot) : slot_(slot) {
        slot_.store(std::this_thread::get_id());
    }
    ~WorkerThreadIdGuard() {
        slot_.store(std::thread::id{});
    }
    WorkerThreadIdGuard(const WorkerThreadIdGuard&) = delete;
    WorkerThreadIdGuard& operator=(const WorkerThreadIdGuard&) = delete;

  private:
    std::atomic<std::thread::id>& slot_;
};
} // namespace

void RealTimeScheduler::schedulerLoop() {
    // C42: published first, cleared last (by the guard's destructor, on
    // every exit path) so isOnSchedulerThread() can identify this thread.
    WorkerThreadIdGuard idGuard(schedulerThreadId_);

    auto nextWakeup = std::chrono::steady_clock::now() + timerResolution_;
    cycleTimer_.start(); // Start the cycle timer

    // Lock memory pages for real-time performance
#ifdef __linux__
    if (realTimeMode_.load()) {
        mlockall(MCL_CURRENT | MCL_FUTURE);
    }
#endif

    while (running_.load()) {
        // Wait until it's time for the next scheduling decision
        {
            std::unique_lock<std::mutex> lock(schedulerMutex_);
            if (paused_.load()) {
                schedulerCondition_.wait(lock,
                                         [this] { return !paused_.load() || !running_.load(); });
                continue;
            }

            schedulerCondition_.wait_until(lock, nextWakeup);
        }

        if (!running_.load()) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        cycleTimer_.stop(); // Stop the timer to measure the cycle time
        auto cycleTime = cycleTimer_.getElapsedNanoseconds();
        cycleTimer_.start(); // Restart for the next cycle

        // Calculate scheduling jitter
        auto expectedCycleTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(timerResolution_);
        std::chrono::nanoseconds jitter = cycleTime - expectedCycleTime;
        if (jitter < std::chrono::nanoseconds::zero()) {
            jitter = -jitter;
        }
        updateSchedulingJitter(std::chrono::duration_cast<std::chrono::microseconds>(jitter));

        nextWakeup = now + timerResolution_;

        // V4: ready-set built ONCE per cycle for the 4 non-CUSTOM policies
        // (also folds the reactivation-check pass, previously a second
        // separate locked traversal -- see selectReadyTasksForCycle()).
        // CUSTOM keeps its own per-call model (V1's snapshot-then-release
        // contract): it needs its own small reactivation pass here since it
        // never calls that helper, and a task-count cap since scheduleCustom()
        // has no ready-set of its own to size the loop against.
        size_t taskCount = 0;
        if (policy_ == SchedulingPolicy::CUSTOM) {
            readyTaskIds_.clear(); // not used on this path -- see the loop below
            std::lock_guard<std::mutex> lock(tasksMutex_);
            for (auto& kv : tasks_) {
                SchedulerTask* task = kv.second;
                if (!task->active.load() && task->consecutiveFailures > 0 &&
                    now >= task->reactivationTime) {
                    task->active.store(true);
                    task->consecutiveFailures = 0; // Reset failure count on reactivation
                }
            }
            taskCount = tasks_.size();
        } else {
            selectReadyTasksForCycle(); // fills readyTaskIds_ (HIGH fix: no per-cycle heap alloc)
            taskCount = readyTaskIds_.size();
        }

        // Execute every ready task this cycle, not just one (C1). Each task is
        // claimed (executing=true) under tasksMutex_ and its user code then runs
        // WITHOUT the lock, so addTask/removeTask/getters are never blocked by
        // user code. The hard cap of taskCount iterations guarantees termination;
        // for CUSTOM, an overloaded task (execution time > period) can still be
        // reselected within one cycle and consume several iterations before the
        // cap hits -- the other policies exhaust their fixed ready-set instead.
        for (size_t executed = 0; executed < taskCount && running_.load(); ++executed) {
            uint32_t selectedTaskId = 0;
            if (policy_ == SchedulingPolicy::CUSTOM) {
                selectedTaskId = scheduleCustom();
            } else if (executed < readyTaskIds_.size()) {
                selectedTaskId = readyTaskIds_[executed];
            }

            if (selectedTaskId == 0) {
                break; // No more ready tasks this cycle
            }

            SchedulerTask* task = nullptr;
            {
                std::lock_guard<std::mutex> lock(tasksMutex_);
                auto it = tasks_.find(selectedTaskId);
                if (it != tasks_.end() && it->second->active.load() &&
                    !it->second->executing.load()) {
                    task = it->second;
                    task->executing.store(true); // Claim before releasing the lock
                }
            }

            if (task != nullptr) {
                TaskExecutionResult result = executeTask(*task);

                // V6: bookkeeping folded into the release-claim lock inside
                // finalizeTaskExecution() -- previously a separate tasksMutex_
                // acquisition inside executeTask() plus this one; now one.
                TaskFinalizeOutcome outcome = finalizeTaskExecution(*task, result);

                // V5: no statsMutex_ -- scheduler-thread-private relaxed
                // atomics, see AtomicStatistics's header comment.
                updateGlobalStatistics(result.success, result.executionTime,
                                       outcome.missedDeadline);

                // Unlocked dispatch (C18/C1). CRITICAL fix: use
                // outcome.unit, captured under tasksMutex_ inside
                // finalizeTaskExecution() BEFORE it may deallocate `task`
                // back to taskPool_ (pendingRemoval branch). `task` itself
                // must not be dereferenced here any more -- a concurrent
                // removeTask() during execution can have already returned
                // this exact block to the pool's free list by this point.
                if (!result.success && errorCallback_) {
                    if (outcome.deactivated) {
                        // Bounded, zero-heap (V2): %.180s caps the embedded
                        // message so GCC can prove buf is always large
                        // enough (silences -Wformat-truncation) -- errorMessage
                        // itself is already a 192-byte bounded buffer.
                        char buf[288];
                        std::snprintf(
                            buf, sizeof(buf),
                            "Task temporarily deactivated after %llu consecutive failures: %.180s",
                            static_cast<unsigned long long>(kMaxConsecutiveFailures),
                            result.errorMessage);
                        errorCallback_(outcome.unit, std::string(buf));
                    }
                    errorCallback_(outcome.unit, std::string(result.errorMessage));
                }
            }
        }

        lastScheduleTime_ = now;
    }

#ifdef __linux__
    if (realTimeMode_.load()) {
        munlockall();
    }
#endif
}

uint32_t RealTimeScheduler::schedulePriorityBased() {
    std::lock_guard<std::mutex> lock(tasksMutex_);

    uint32_t selectedTask = 0;
    SchedulerPriority highestPriority = SchedulerPriority::IDLE; // Start with lowest priority
    bool foundTask = false;
    auto now = std::chrono::steady_clock::now();

    for (auto& pair : tasks_) {
        auto& task = pair.second;
        if (!task->active.load() || task->executing.load()) {
            continue;
        }

        // Check if task is ready to execute
        if (now >= task->nextExecution) {
            // Lower enum values represent higher priorities (HIGH=0, NORMAL=1, LOW=2, IDLE=3)
            if (!foundTask || task->constraints.priority < highestPriority) {
                highestPriority = task->constraints.priority;
                selectedTask = pair.first;
                foundTask = true;
            }
        }
    }

    return selectedTask;
}

uint32_t RealTimeScheduler::scheduleEarliestDeadlineFirst() {
    std::lock_guard<std::mutex> lock(tasksMutex_);

    uint32_t selectedTask = 0;
    auto earliestDeadline = std::chrono::steady_clock::time_point::max();
    auto now = std::chrono::steady_clock::now();

    for (auto& pair : tasks_) {
        auto& task = pair.second;
        if (!task->active.load() || task->executing.load()) {
            continue;
        }

        // Check if task is ready to execute
        if (now >= task->nextExecution) {
            auto deadline = task->nextExecution + task->constraints.deadline;
            if (deadline < earliestDeadline) {
                earliestDeadline = deadline;
                selectedTask = pair.first;
            }
        }
    }

    return selectedTask;
}

uint32_t RealTimeScheduler::scheduleRateMonotonic() {
    std::lock_guard<std::mutex> lock(tasksMutex_);

    uint32_t selectedTask = 0;
    auto shortestPeriod = std::chrono::microseconds::max();
    auto now = std::chrono::steady_clock::now();

    for (auto& pair : tasks_) {
        auto& task = pair.second;
        if (!task->active.load() || task->executing.load()) {
            continue;
        }

        // Check if task is ready to execute
        if (now >= task->nextExecution) {
            if (task->constraints.period < shortestPeriod) {
                shortestPeriod = task->constraints.period;
                selectedTask = pair.first;
            }
        }
    }

    return selectedTask;
}

uint32_t RealTimeScheduler::scheduleRoundRobin() {
    std::lock_guard<std::mutex> lock(tasksMutex_);

    auto now = std::chrono::steady_clock::now();

    // Find next task after the last selected one (C45: lastSelectedTask_ is
    // a per-instance member, guarded by tasksMutex_ above)
    auto it = tasks_.find(lastSelectedTask_);
    if (it != tasks_.end()) {
        ++it;
    } else {
        it = tasks_.begin();
    }

    // Look for a ready task, starting from the next task after last selected
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (it == tasks_.end()) {
            it = tasks_.begin();
        }

        auto& task = it->second;
        if (task->active.load() && !task->executing.load() && now >= task->nextExecution) {
            lastSelectedTask_ = it->first;
            return it->first;
        }

        ++it;
    }

    return 0;
}

void RealTimeScheduler::selectReadyTasksForCycle() {
    // HIGH fix: reuse the persistent member buffers instead of fresh locals
    // -- clear() keeps each vector's capacity, so once the ready-set size
    // stabilizes this cycle allocates nothing (see the members' declaration
    // comment and ReadyCandidate's comment on the struct itself).
    auto& candidates = readyCandidates_;
    candidates.clear();
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        candidates.reserve(tasks_.size());
        for (auto& kv : tasks_) {
            SchedulerTask* task = kv.second;
            // Reactivation check folded into this same pass -- was a second,
            // separate locked traversal in schedulerLoop before V4.
            if (!task->active.load() && task->consecutiveFailures > 0 &&
                now >= task->reactivationTime) {
                task->active.store(true);
                task->consecutiveFailures = 0;
            }
            if (task->active.load() && !task->executing.load() && now >= task->nextExecution) {
                candidates.push_back({task->taskId, task->constraints.priority,
                                      task->nextExecution + task->constraints.deadline,
                                      task->constraints.period});
            }
        }
    }

    // Sort/select with tasksMutex_ released -- O(1) lock acquisitions for
    // the whole cycle's selection instead of one per candidate picked.
    // Comparators mirror each scheduleX() method's own selection rule
    // exactly (those methods are unchanged above, still independently
    // callable for direct callers/tests).
    switch (policy_) {
        case SchedulingPolicy::PRIORITY_BASED:
            std::sort(candidates.begin(), candidates.end(),
                      [](const ReadyCandidate& a, const ReadyCandidate& b) {
                          return a.priority < b.priority;
                      });
            break;
        case SchedulingPolicy::EARLIEST_DEADLINE_FIRST:
            std::sort(candidates.begin(), candidates.end(),
                      [](const ReadyCandidate& a, const ReadyCandidate& b) {
                          return a.deadline < b.deadline;
                      });
            break;
        case SchedulingPolicy::RATE_MONOTONIC:
            std::sort(candidates.begin(), candidates.end(),
                      [](const ReadyCandidate& a, const ReadyCandidate& b) {
                          return a.period < b.period;
                      });
            break;
        case SchedulingPolicy::ROUND_ROBIN: {
            // Same "next after cursor, wrap" order as scheduleRoundRobin(),
            // computed once for every ready task this cycle instead of
            // re-scanned under lock per pick. lastSelectedTask_'s read here
            // is unlocked -- safe because it is written only from the
            // scheduler thread (this call, or scheduleRoundRobin() itself,
            // both scheduler-thread-only call paths that this rework never
            // runs concurrently with each other).
            auto it = std::find_if(
                candidates.begin(), candidates.end(),
                [this](const ReadyCandidate& c) { return c.taskId == lastSelectedTask_; });
            if (it != candidates.end()) {
                std::rotate(candidates.begin(), it + 1, candidates.end());
            }
            break;
        }
        case SchedulingPolicy::CUSTOM:
            break; // not used for CUSTOM -- schedulerLoop keeps the old per-call path
    }

    auto& ids = readyTaskIds_;
    ids.clear();
    ids.reserve(candidates.size());
    for (const auto& c : candidates) {
        ids.push_back(c.taskId);
    }
    if (policy_ == SchedulingPolicy::ROUND_ROBIN && !ids.empty()) {
        // Guarded by tasksMutex_, matching the member's documented
        // invariant (see its declaration) even though this call path is
        // scheduler-thread-only in practice.
        std::lock_guard<std::mutex> lock(tasksMutex_);
        lastSelectedTask_ = ids.back();
    }
}

uint32_t RealTimeScheduler::scheduleCustom() {
    // C44-sibling: both this guard and the call below read customScheduler_
    // with no lock, on the scheduler thread. That is legal only because
    // setCustomScheduler() is setup-only (requireNotStarted(), latched by
    // hasStarted_): no write can land once the scheduler has started, and
    // scheduleCustom() itself never runs before then, so there is no
    // concurrent writer to race and no check-then-act window between the two
    // reads. Do not "fix" this back into a schedulerMutex_ lock -- that would
    // be a lock on the RT path for a hazard that no longer exists.
    if (!customScheduler_) {
        return schedulePriorityBased(); // Fallback
    }

    // C43: snapshot under tasksMutex_, then release before invoking the user
    // callback (C12/C18 rule — never run user code under a lock). Each
    // element is a deep copy (SchedulerTask's copy ctor copies the name and
    // .load()s the atomics), so customSchedulerSnapshot_ stays valid and
    // independent of tasks_ for the whole unlocked call below, even if the
    // callback re-enters addTask/removeTask and mutates tasks_ concurrently.
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        customSchedulerSnapshot_.clear();
        for (const auto& pair : tasks_) {
            if (pair.second->active.load()) {
                customSchedulerSnapshot_.emplace_back(*pair.second);
            }
        }
    }

    // No lock held here: the callback may legally call back into this
    // scheduler's public API. Its returned id is re-validated under
    // tasksMutex_ by schedulerLoop() before use (find + active +
    // !executing) — a stale id (removed, or since mutated by the callback
    // itself) is simply skipped that cycle, the same handling every other
    // scheduling algorithm's selection already gets, just with a much wider
    // window now that user code runs in between.
    return customScheduler_(customSchedulerSnapshot_);
}

RealTimeScheduler::TaskExecutionResult RealTimeScheduler::executeTask(SchedulerTask& task) {
    // Precondition: the caller (scheduler loop) has already claimed the task
    // (executing=true under tasksMutex_) and calls finalizeTaskExecution()
    // afterwards to release it. This function itself touches NO lock and
    // dispatches NO user callback -- it only runs the unit's own code
    // (already user code on the RT thread by design, V13) and reports what
    // happened. V2: on failure, the message is copied into a bounded stack
    // buffer (one snprintf, zero heap) rather than kept as a dangling
    // `const char*` into the (about to be destroyed) exception object.
    TaskExecutionResult result;
    result.executionStart = std::chrono::steady_clock::now();

    try {
        task.unit->processAsyncBase();
        task.unit->processSyncBase();

        result.executionEnd = std::chrono::steady_clock::now();
        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            result.executionEnd - result.executionStart);
    } catch (const std::exception& e) {
        result.success = false;
        result.executionEnd = std::chrono::steady_clock::now();
        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            result.executionEnd - result.executionStart);
        std::snprintf(result.errorMessage, sizeof(result.errorMessage), "%s", e.what());
    }

    // Halt-bound telemetry (§6.3): scheduler-thread-only writer, reused
    // timestamp already paid for above -- no extra now() call.
    lastTaskReturnAtNs_.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(result.executionEnd.time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    return result;
}

RealTimeScheduler::TaskFinalizeOutcome RealTimeScheduler::finalizeTaskExecution(
    SchedulerTask& task, const TaskExecutionResult& result) {
    // V6: task bookkeeping folded into the SAME tasksMutex_ acquisition that
    // releases the executing-claim and checks pendingRemoval -- previously
    // two separate acquisitions (one inside executeTask() for bookkeeping,
    // one in schedulerLoop() for the release). Net: one lock per task here,
    // plus the claim lock schedulerLoop already takes before calling
    // executeTask() -- unchanged from before this rework.
    TaskFinalizeOutcome outcome;
    std::lock_guard<std::mutex> lock(tasksMutex_);

    // CRITICAL fix: copy the unit pointer out while `task` is still
    // guaranteed alive (this lock is the only thing standing between here
    // and the pendingRemoval deallocate below). schedulerLoop's error
    // dispatch happens after this function returns and after the lock is
    // released, so it must use this copy, never `task.unit` directly.
    outcome.unit = task.unit;

    if (result.success) {
        updateTaskStatistics(task, result.executionTime);

        task.lastExecution = result.executionStart;
        task.nextExecution = calculateNextExecution(task);
        task.consecutiveFailures = 0; // Reset failure count on successful execution

        // Evaluated AFTER nextExecution advances (original semantics, carried
        // across this move unchanged): checks the upcoming slot, not the one
        // that just ran. The failure branch below must stay consistent with
        // this ordering (see its comment).
        outcome.missedDeadline = hasDeadlinePassed(task);
    } else {
        task.executionCount++;
        task.totalExecutionTime += result.executionTime;
        task.consecutiveFailures++;
        task.lastFailureTime = result.executionEnd;

        const auto FAILURE_BACKOFF_TIME = std::chrono::milliseconds(100);
        if (task.consecutiveFailures >= kMaxConsecutiveFailures) {
            task.active.store(false);
            task.reactivationTime = result.executionEnd + FAILURE_BACKOFF_TIME;
            outcome.deactivated = true;
        }

        // Failure path never advances nextExecution -- hasDeadlinePassed()
        // here evaluates against the same (stale) slot the task missed,
        // consistent with the success branch's "after advance" semantics
        // (there is no advance to be after).
        outcome.missedDeadline = hasDeadlinePassed(task);
        if (outcome.missedDeadline) {
            task.missedDeadlines++;
        }
    }

    // Release the claim under the same lock. removeTask only defers
    // deallocation (pendingRemoval) while executing is true and only
    // reads/writes these flags under tasksMutex_, so exactly one side
    // deallocates and never while the other still uses the task.
    task.executing.store(false);
    if (task.pendingRemoval.load()) {
        taskPool_->deallocateObject(&task);
    }

    return outcome;
}

void RealTimeScheduler::updateTaskStatistics(SchedulerTask& task,
                                             std::chrono::microseconds executionTime) {
    task.executionCount++;
    task.totalExecutionTime += executionTime;

    if (hasDeadlinePassed(task)) {
        task.missedDeadlines++;
    }
}

void RealTimeScheduler::updateGlobalStatistics(bool success,
                                               std::chrono::microseconds executionTime,
                                               bool missedDeadline) {
    // V5: scheduler-thread-only writer, relaxed atomics, no lock -- see
    // AtomicStatistics's comment in the header for the consistency argument.
    // Mirrors the pre-rework mutex-protected block field-for-field: success
    // updates successfulExecutions + the running avg/min/max; failure
    // updates only failedExecutions (never avg/min/max, matching the
    // original two independent code blocks exactly).
    uint64_t total = statistics_.totalExecutions.fetch_add(1, std::memory_order_relaxed) + 1;
    statistics_.totalExecutionTimeUs.fetch_add(executionTime.count(), std::memory_order_relaxed);

    if (success) {
        statistics_.successfulExecutions.fetch_add(1, std::memory_order_relaxed);

        if (total == 1) {
            statistics_.averageExecutionTimeUs.store(executionTime.count(),
                                                     std::memory_order_relaxed);
            statistics_.minExecutionTimeUs.store(executionTime.count(), std::memory_order_relaxed);
            statistics_.maxExecutionTimeUs.store(executionTime.count(), std::memory_order_relaxed);
        } else {
            auto oldAvg = statistics_.averageExecutionTimeUs.load(std::memory_order_relaxed);
            auto newAvg = (oldAvg * (static_cast<int64_t>(total) - 1) + executionTime.count()) /
                          static_cast<int64_t>(total);
            statistics_.averageExecutionTimeUs.store(newAvg, std::memory_order_relaxed);

            if (executionTime.count() <
                statistics_.minExecutionTimeUs.load(std::memory_order_relaxed)) {
                statistics_.minExecutionTimeUs.store(executionTime.count(),
                                                     std::memory_order_relaxed);
            }
            if (executionTime.count() >
                statistics_.maxExecutionTimeUs.load(std::memory_order_relaxed)) {
                statistics_.maxExecutionTimeUs.store(executionTime.count(),
                                                     std::memory_order_relaxed);
            }
        }
    } else {
        statistics_.failedExecutions.fetch_add(1, std::memory_order_relaxed);
    }

    if (missedDeadline) {
        statistics_.missedDeadlines.fetch_add(1, std::memory_order_relaxed);
    }
}

void RealTimeScheduler::updateSchedulingJitter(std::chrono::microseconds jitter) {
    // V5: no lock -- scheduler-thread-only writer (called once per cycle
    // from schedulerLoop, never concurrently with updateGlobalStatistics()
    // since both run on that same single thread).
    statistics_.schedulingJitterUs.store(jitter.count(), std::memory_order_relaxed);

    auto avgCount = statistics_.totalExecutions.load(std::memory_order_relaxed);
    if (avgCount > 0) {
        auto oldAvg = statistics_.averageSchedulingJitterUs.load(std::memory_order_relaxed);
        auto newAvg = (oldAvg * (static_cast<int64_t>(avgCount) - 1) + jitter.count()) /
                      static_cast<int64_t>(avgCount);
        statistics_.averageSchedulingJitterUs.store(newAvg, std::memory_order_relaxed);
    } else {
        statistics_.averageSchedulingJitterUs.store(jitter.count(), std::memory_order_relaxed);
    }
}

std::chrono::steady_clock::time_point RealTimeScheduler::calculateNextExecution(
    const SchedulerTask& task) const {
    return task.lastExecution + task.constraints.period;
}

bool RealTimeScheduler::hasDeadlinePassed(const SchedulerTask& task) const {
    auto now = std::chrono::steady_clock::now();
    auto deadline = task.nextExecution + task.constraints.deadline;
    return now > deadline;
}

void RealTimeScheduler::setThreadPriority(std::thread& thread, SchedulerPriority priority) {
#ifdef __linux__
    pthread_t nativeHandle = thread.native_handle();
    struct sched_param param;
    int policy = SCHED_OTHER;
    int priorityValue = 0;

    // Try to set real-time priority if requested and available
    if (priority >= SchedulerPriority::REAL_TIME) {
        policy = SCHED_FIFO;
        // Get valid priority range for FIFO policy
        int minPrio = sched_get_priority_min(SCHED_FIFO);
        int maxPrio = sched_get_priority_max(SCHED_FIFO);

        if (minPrio != -1 && maxPrio != -1) {
            // Map our priority enum to valid FIFO range
            int enumPrio = static_cast<int>(priority);
            priorityValue = minPrio + ((enumPrio - static_cast<int>(SchedulerPriority::REAL_TIME)) *
                                       (maxPrio - minPrio) /
                                       (static_cast<int>(SchedulerPriority::CRITICAL) -
                                        static_cast<int>(SchedulerPriority::REAL_TIME)));
            priorityValue = std::max(minPrio, std::min(maxPrio, priorityValue));
        } else {
            // Fallback to SCHED_OTHER if can't get FIFO range
            policy = SCHED_OTHER;
            priorityValue = 0;
        }
    } else {
        // For non-real-time priorities, use SCHED_OTHER with nice values
        policy = SCHED_OTHER;
        priorityValue = 0; // SCHED_OTHER must use priority 0
    }

    param.sched_priority = priorityValue;

    // Attempt to set the priority, but don't fail if it doesn't work
    int result = pthread_setschedparam(nativeHandle, policy, &param);
    if (result != 0) {
        // Fallback to normal scheduling if real-time fails (e.g., no privileges)
        if (policy != SCHED_OTHER) {
            param.sched_priority = 0;
            pthread_setschedparam(nativeHandle, SCHED_OTHER, &param);
        }
        // Note: In a production system, you might want to log this failure
    }

#elif _WIN32
    // Set Windows thread priority
    HANDLE nativeHandle = thread.native_handle();
    int winPriority = THREAD_PRIORITY_NORMAL;

    switch (priority) {
        case SchedulerPriority::IDLE:
            winPriority = THREAD_PRIORITY_IDLE;
            break;
        case SchedulerPriority::LOW:
            winPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case SchedulerPriority::NORMAL:
            winPriority = THREAD_PRIORITY_NORMAL;
            break;
        case SchedulerPriority::HIGH:
            winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case SchedulerPriority::REAL_TIME:
            winPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        case SchedulerPriority::CRITICAL:
            winPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
    }

    SetThreadPriority(nativeHandle, winPriority);
#endif
}

void RealTimeScheduler::setThreadAffinity(std::thread& thread, uint32_t cpuCore) {
#ifdef __linux__
    pthread_t nativeHandle = thread.native_handle();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuCore, &cpuset);
    pthread_setaffinity_np(nativeHandle, sizeof(cpu_set_t), &cpuset);

#elif _WIN32
    HANDLE nativeHandle = thread.native_handle();
    DWORD_PTR affinityMask = static_cast<DWORD_PTR>(1) << cpuCore;
    SetThreadAffinityMask(nativeHandle, affinityMask);
#endif
}

// TimingController Implementation
TimingController::TimingController(SchedulingPolicy policy)
    : scheduler_(std::make_unique<RealTimeScheduler>(policy)) {
    initialize();
}

TimingController::~TimingController() {
    shutdown();
}

uint32_t TimingController::scheduleProcessingUnit(ProcessingUnit* unit,
                                                  const TimingConstraints& constraints) {
    if (!unit) {
        return 0;
    }

    TimingConstraints finalConstraints = constraints;
    if (finalConstraints.period.count() == 0) {
        finalConstraints = createDefaultConstraints(executionFrequency_);
    }

    uint32_t taskId = scheduler_->addTask(unit, finalConstraints);

    if (taskId != 0) {
        std::lock_guard<std::mutex> lock(unitMapMutex_);
        unitToTaskId_[unit] = taskId;
    }

    return taskId;
}

bool TimingController::removeProcessingUnit(uint32_t taskId) {
    bool removed = scheduler_->removeTask(taskId);

    if (removed) {
        std::lock_guard<std::mutex> lock(unitMapMutex_);
        // Find and remove the unit-to-taskId mapping
        auto it = std::find_if(unitToTaskId_.begin(), unitToTaskId_.end(),
                               [taskId](const auto& pair) { return pair.second == taskId; });
        if (it != unitToTaskId_.end()) {
            unitToTaskId_.erase(it);
        }
    }

    return removed;
}

bool TimingController::removeProcessingUnit(ProcessingUnit* unit) {
    if (!unit) {
        return false;
    }

    uint32_t taskId = 0;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(unitMapMutex_);
        auto it = unitToTaskId_.find(unit);
        if (it != unitToTaskId_.end()) {
            taskId = it->second;
            unitToTaskId_.erase(it);
            found = true;
        }
    } // Lock released here automatically

    if (found) {
        return scheduler_->removeTask(taskId);
    }

    return false;
}

void TimingController::start() {
    startTime_ = std::chrono::steady_clock::now();
    totalCycles_.store(0);
    scheduler_->start();
}

void TimingController::stop() {
    scheduler_->stop();
}

bool TimingController::isOnSchedulerThread() const noexcept {
    return scheduler_ && scheduler_->isOnSchedulerThread();
}

std::chrono::microseconds TimingController::getHaltLatency() const noexcept {
    return scheduler_ ? scheduler_->getHaltLatency() : std::chrono::microseconds{0};
}

void TimingController::pause() {
    scheduler_->pause();
}

void TimingController::resume() {
    scheduler_->resume();
}

void TimingController::reset() {
    scheduler_->stop();
    scheduler_->removeAllTasks();
    scheduler_->resetStatistics();

    {
        std::lock_guard<std::mutex> lock(unitMapMutex_);
        unitToTaskId_.clear();
    }

    totalCycles_.store(0);
}

void TimingController::setExecutionFrequency(double frequency) {
    if (frequency > 0.0) {
        executionFrequency_ = frequency;
    }
}

void TimingController::enableRealTimeMode(bool enable) {
    realTimeEnabled_ = enable;
    scheduler_->enableRealTimeMode(enable);
}

void TimingController::setSchedulingPolicy(SchedulingPolicy policy) {
    scheduler_->setSchedulingPolicy(policy);
}

void TimingController::setTimerResolution(std::chrono::microseconds resolution) {
    scheduler_->setTimerResolution(resolution);
}

void TimingController::setThreadAffinity(uint32_t cpuCore) {
    scheduler_->setThreadAffinity(cpuCore);
}

SchedulerStatistics TimingController::getPerformanceMetrics() const {
    return scheduler_->getStatistics();
}

void TimingController::resetPerformanceMetrics() {
    scheduler_->resetStatistics();
}

bool TimingController::updateTaskConstraints(uint32_t taskId,
                                             const TimingConstraints& constraints) {
    return scheduler_->updateTaskConstraints(taskId, constraints);
}

std::vector<uint32_t> TimingController::getActiveTaskIds() const {
    return scheduler_->getActiveTaskIds();
}

TimingConstraints TimingController::getTaskConstraints(uint32_t taskId) const {
    return scheduler_->getTaskConstraints(taskId);
}

void TimingController::setCustomScheduler(RealTimeScheduler::CustomSchedulerCallback callback) {
    scheduler_->setCustomScheduler(std::move(callback));
}

void TimingController::setErrorCallback(RealTimeScheduler::ErrorCallback callback) {
    scheduler_->setErrorCallback(std::move(callback));
}

void TimingController::enableDeterministicExecution(bool enable) {
    deterministicExecution_ = enable;
}

void TimingController::setGlobalTimeReference(std::chrono::steady_clock::time_point reference) {
    globalTimeReference_ = reference;
    hasGlobalTimeReference_ = true;
}

void TimingController::enableDebugLogging(bool enable) {
    debugLogging_ = enable;
}

void TimingController::exportSchedulingTrace(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return;
    }

    auto stats = getPerformanceMetrics();
    file << "AxonVex TimingController Scheduling Trace\n";
    file << "========================================\n";
    file << "Total Executions: " << stats.totalExecutions << "\n";
    file << "Successful Executions: " << stats.successfulExecutions << "\n";
    file << "Failed Executions: " << stats.failedExecutions << "\n";
    file << "Missed Deadlines: " << stats.missedDeadlines << "\n";
    file << "Average Execution Time: " << stats.averageExecutionTime.count() << " µs\n";
    file << "Max Execution Time: " << stats.maxExecutionTime.count() << " µs\n";
    file << "Min Execution Time: " << stats.minExecutionTime.count() << " µs\n";
    file << "CPU Utilization: " << stats.cpuUtilization << "%\n";
    file << "Schedulability Ratio: " << stats.schedulabilityRatio << "\n";

    // Add debug trace if available
    if (debugLogging_) {
        std::lock_guard<std::mutex> lock(debugMutex_);
        file << "\nDebug Trace:\n";
        for (const auto& entry : debugTrace_) {
            file << entry << "\n";
        }
    }
}

bool TimingController::validateSchedulability() const {
    // Simple schedulability test using utilization factor
    auto taskIds = getActiveTaskIds();
    double totalUtilization = 0.0;

    for (uint32_t taskId : taskIds) {
        auto constraints = getTaskConstraints(taskId);
        if (constraints.period.count() > 0) {
            double utilization = static_cast<double>(constraints.wcet.count()) /
                                 static_cast<double>(constraints.period.count());
            totalUtilization += utilization;
        }
    }

    // Liu and Layland bound: U ≤ n(2^(1/n) - 1)
    size_t n = taskIds.size();
    if (n == 0)
        return true;

    double bound = n * (std::pow(2.0, 1.0 / n) - 1.0);
    return totalUtilization <= bound;
}

void TimingController::emergencyStop() {
    // Design spec §6.3: the non-blocking e-stop hot path. Does NOT wait for
    // an in-flight task's WCET -- callers needing full teardown (thread
    // reclaimed) should follow with stop(), which still joins.
    scheduler_->halt();

    if (failsafeCallback_) {
        failsafeCallback_("Emergency stop triggered");
    }
}

void TimingController::setFailsafeCallback(std::function<void(const std::string&)> callback) {
    failsafeCallback_ = std::move(callback);
}

void TimingController::initialize() {
    // Initialize global time reference if not set
    if (!hasGlobalTimeReference_) {
        globalTimeReference_ = std::chrono::steady_clock::now();
        hasGlobalTimeReference_ = true;
    }
}

void TimingController::shutdown() {
    if (scheduler_) {
        scheduler_->stop();
    }
}

TimingConstraints TimingController::createDefaultConstraints(double frequency) const {
    TimingConstraints constraints;

    if (frequency > 0.0) {
        // Calculate period in microseconds: 1,000,000 us / frequency
        auto period_us = static_cast<long long>(1000000.0 / frequency);
        constraints.period = std::chrono::microseconds(period_us);
        constraints.deadline = constraints.period;
        constraints.wcet = constraints.period / 10; // Conservative estimate
    } else {
        // Default constructor should have period = 0
        constraints.period = std::chrono::microseconds(0);
        constraints.deadline = std::chrono::microseconds(0);
        constraints.wcet = std::chrono::microseconds(0);
    }

    return constraints;
}

} // namespace axonvex::core
