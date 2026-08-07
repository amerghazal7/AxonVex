#pragma once

#include <atomic>
#include <axonvex_core/precisionTimer.hpp>
#include <axonvex_core/utils/containers/memoryPool.hpp>
#include <axonvex_core/utils/containers/threadSafeQueue.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

// Bring utils containers into core namespace for convenience
using axonvex::utils::containers::MemoryPool;
using axonvex::utils::containers::ThreadSafeQueue;

// Forward declaration
class ProcessingUnit;

// Process priority levels for real-time scheduling
enum class SchedulerPriority {
    IDLE = 0,
    LOW = 1,
    NORMAL = 2,
    HIGH = 3,
    REAL_TIME = 4,
    CRITICAL = 5
};

// Scheduler policies
enum class SchedulingPolicy {
    ROUND_ROBIN,
    PRIORITY_BASED,
    EARLIEST_DEADLINE_FIRST,
    RATE_MONOTONIC,
    CUSTOM
};

// Timing constraints structure
struct TimingConstraints {
    std::chrono::microseconds period{0};   // Execution period (0 = use default from frequency)
    std::chrono::microseconds deadline{0}; // Execution deadline (0 = same as period)
    std::chrono::microseconds wcet{0};     // Worst-case execution time (0 = auto-calculate)
    SchedulerPriority priority{SchedulerPriority::NORMAL};
    bool isRealTime{false}; // Real-time task flag

    void reset() {
        period = std::chrono::microseconds{0};
        deadline = std::chrono::microseconds{0};
        wcet = std::chrono::microseconds{0};
        priority = SchedulerPriority::NORMAL;
        isRealTime = false;
    }
};

// Scheduler statistics
struct SchedulerStatistics {
    uint64_t totalExecutions{0};
    uint64_t successfulExecutions{0};
    uint64_t failedExecutions{0};
    uint64_t missedDeadlines{0};
    uint64_t preemptions{0};
    std::chrono::microseconds totalExecutionTime{0};
    std::chrono::microseconds averageExecutionTime{0};
    std::chrono::microseconds maxExecutionTime{0};
    std::chrono::microseconds minExecutionTime{std::chrono::microseconds::max()};
    double cpuUtilization{0.0};
    double schedulabilityRatio{0.0};
    std::chrono::microseconds schedulingJitter{0};        // New statistic
    std::chrono::microseconds averageSchedulingJitter{0}; // New statistic

    void reset() {
        totalExecutions = 0;
        successfulExecutions = 0;
        failedExecutions = 0;
        missedDeadlines = 0;
        preemptions = 0;
        totalExecutionTime = std::chrono::microseconds{0};
        averageExecutionTime = std::chrono::microseconds{0};
        maxExecutionTime = std::chrono::microseconds{0};
        minExecutionTime = std::chrono::microseconds::max();
        cpuUtilization = 0.0;
        schedulabilityRatio = 0.0;
        schedulingJitter = std::chrono::microseconds{0};
        averageSchedulingJitter = std::chrono::microseconds{0};
    }
};

// Task descriptor for internal scheduler use
struct SchedulerTask {
    ProcessingUnit* unit{nullptr};
    TimingConstraints constraints;
    std::chrono::steady_clock::time_point nextExecution;
    std::chrono::steady_clock::time_point lastExecution;
    std::atomic<bool> active{true};
    std::atomic<bool> executing{false};
    // Set by removeTask when the task is mid-execution: the scheduler loop
    // deallocates it after the execution finishes (C1). Deliberately not
    // copied/moved by the special members — a copy is not the pool-owned object.
    std::atomic<bool> pendingRemoval{false};
    uint32_t taskId{0};
    std::string name;

    // Performance tracking
    uint64_t executionCount{0};
    uint64_t missedDeadlines{0};
    std::chrono::microseconds totalExecutionTime{0};

    // Failure tracking for error handling
    uint64_t consecutiveFailures{0};
    std::chrono::steady_clock::time_point lastFailureTime;
    std::chrono::steady_clock::time_point reactivationTime;

    // Custom constructors for atomic members
    SchedulerTask() = default;

    SchedulerTask(const SchedulerTask& other)
        : unit(other.unit), constraints(other.constraints), nextExecution(other.nextExecution),
          lastExecution(other.lastExecution), active(other.active.load()),
          executing(other.executing.load()), taskId(other.taskId), name(other.name),
          executionCount(other.executionCount), missedDeadlines(other.missedDeadlines),
          totalExecutionTime(other.totalExecutionTime),
          consecutiveFailures(other.consecutiveFailures), lastFailureTime(other.lastFailureTime),
          reactivationTime(other.reactivationTime) {}

    SchedulerTask& operator=(const SchedulerTask& other) {
        if (this != &other) {
            unit = other.unit;
            constraints = other.constraints;
            nextExecution = other.nextExecution;
            lastExecution = other.lastExecution;
            active.store(other.active.load());
            executing.store(other.executing.load());
            taskId = other.taskId;
            name = other.name;
            executionCount = other.executionCount;
            missedDeadlines = other.missedDeadlines;
            totalExecutionTime = other.totalExecutionTime;
            consecutiveFailures = other.consecutiveFailures;
            lastFailureTime = other.lastFailureTime;
            reactivationTime = other.reactivationTime;
        }
        return *this;
    }

    SchedulerTask(SchedulerTask&& other) noexcept
        : unit(other.unit), constraints(std::move(other.constraints)),
          nextExecution(other.nextExecution), lastExecution(other.lastExecution),
          active(other.active.load()), executing(other.executing.load()), taskId(other.taskId),
          name(std::move(other.name)), executionCount(other.executionCount),
          missedDeadlines(other.missedDeadlines), totalExecutionTime(other.totalExecutionTime),
          consecutiveFailures(other.consecutiveFailures), lastFailureTime(other.lastFailureTime),
          reactivationTime(other.reactivationTime) {}

    SchedulerTask& operator=(SchedulerTask&& other) noexcept {
        if (this != &other) {
            unit = other.unit;
            constraints = std::move(other.constraints);
            nextExecution = other.nextExecution;
            lastExecution = other.lastExecution;
            active.store(other.active.load());
            executing.store(other.executing.load());
            taskId = other.taskId;
            name = std::move(other.name);
            executionCount = other.executionCount;
            missedDeadlines = other.missedDeadlines;
            totalExecutionTime = other.totalExecutionTime;
            consecutiveFailures = other.consecutiveFailures;
            lastFailureTime = other.lastFailureTime;
            reactivationTime = other.reactivationTime;
        }
        return *this;
    }
};

// Test-only accessor (defined in timingControllerTest.cpp) granted
// friendship below so the C45 regression test can drive
// scheduleRoundRobin() directly and deterministically, with no scheduler
// thread involved, instead of inferring its behavior from timing-sensitive
// task-execution counts.
class RoundRobinTestAccessor;

// Real-time scheduler class
class RealTimeScheduler {
  public:
    explicit RealTimeScheduler(SchedulingPolicy policy = SchedulingPolicy::PRIORITY_BASED);
    ~RealTimeScheduler();

    friend class RoundRobinTestAccessor;

    // Task management
    uint32_t addTask(ProcessingUnit* unit, const TimingConstraints& constraints);
    bool removeTask(uint32_t taskId);
    void removeAllTasks();
    bool updateTaskConstraints(uint32_t taskId, const TimingConstraints& constraints);

    // Scheduler control
    void start();
    void stop();
    // Bounded e-stop primitive (design spec §6.3): non-blocking. Stops
    // dispatching further tasks -- running_.store(false) + notify_all(),
    // nothing else -- WITHOUT joining. Safe to call from ANY thread,
    // including the scheduler thread itself (no self-join hazard: there is
    // no join here to defer, unlike stop()'s C33/C42 guard). Idempotent:
    // repeated calls are just extra store+notify, no state machine to
    // corrupt. stop() = halt() + join; call halt() directly when the "no
    // further tasks" guarantee is needed fast and the join can be deferred
    // (see TimingController::emergencyStop()).
    void halt() noexcept;
    void pause();
    void resume();
    bool isRunning() const {
        return running_.load();
    }
    bool isPaused() const {
        return paused_.load();
    }
    // C42: true iff called from this scheduler's own thread — the id is
    // published by schedulerLoop() itself (first act) and cleared on every
    // exit path (last act). Lets stop() detect a self-call (a ProcessingUnit
    // task body, or the error callback, calling stop() from inside
    // executeTask()) without joining schedulerThread_, which would be a
    // self-join.
    bool isOnSchedulerThread() const noexcept;

    // Configuration
    void setSchedulingPolicy(SchedulingPolicy policy);
    void setThreadAffinity(uint32_t cpuCore);
    void enableRealTimeMode(bool enable);
    void setTimerResolution(std::chrono::microseconds resolution);

    // Statistics and monitoring
    SchedulerStatistics getStatistics() const;
    void resetStatistics();

    // Wall-clock bound from the most recent halt()/stop() request to the
    // last in-flight task's return (design spec §6.2:
    // B = max task WCET + one timer resolution). Zero if halt() has never
    // been called, or if no task has returned since it was called (still
    // executing -- this is telemetry, not a synchronization signal; check
    // again once isRunning() is false).
    std::chrono::microseconds getHaltLatency() const noexcept;

    // Task inspection
    std::vector<uint32_t> getActiveTaskIds() const;
    TimingConstraints getTaskConstraints(uint32_t taskId) const;
    bool isTaskActive(uint32_t taskId) const;

    // Callback for custom scheduling. Re-entrancy contract (C43): the
    // callback runs with tasksMutex_ NOT held, so it may legally call back
    // into addTask/removeTask/getStatistics/etc. The uint32_t it returns is
    // re-validated under tasksMutex_ before use (find + active + !executing);
    // a stale id (already removed, or mutated by the callback itself) is
    // simply skipped that cycle, not a bug.
    using CustomSchedulerCallback = std::function<uint32_t(const std::vector<SchedulerTask>&)>;
    void setCustomScheduler(CustomSchedulerCallback callback);

    // Error callback for processing unit failures. Setup-only (C44): throws
    // std::logic_error if called after the scheduler has ever started —
    // executeTask()'s failure path reads errorCallback_ unlocked on the
    // scheduler thread, so a late registration would race it (torn
    // std::function read). Mirrors ports.hpp's requireNoTrafficYet (C32).
    using ErrorCallback = std::function<void(ProcessingUnit*, const std::string&)>;
    void setErrorCallback(ErrorCallback callback);

  private:
    // Scheduler thread function
    void schedulerLoop();

    // C44: throws std::logic_error(what) if the scheduler has ever started —
    // see hasStarted_.
    void requireNotStarted(const char* what) const;

    // Scheduling algorithms. Each remains independently callable (direct
    // single-shot selection: its own tasksMutex_ scan) for existing callers
    // -- the RoundRobinTestAccessor-driven regression test, and
    // scheduleCustom()'s fallback below. schedulerLoop()'s hot path does NOT
    // call these repeatedly any more for the non-CUSTOM policies; see
    // selectReadyTasksForCycle() (V4).
    uint32_t scheduleRoundRobin();
    uint32_t schedulePriorityBased();
    uint32_t scheduleEarliestDeadlineFirst();
    uint32_t scheduleRateMonotonic();
    uint32_t scheduleCustom();

    // V4/V6 ready-set rework (design spec §5): one locked pass over tasks_
    // collects every ready task's id + policy sort key for the WHOLE cycle
    // (also folds the reactivation-check pass, previously a second separate
    // locked traversal). Sorting and the resulting selection order are
    // computed with tasksMutex_ released -- O(1) lock acquisitions for the
    // cycle's selection instead of one per candidate picked (the old
    // schedulerLoop called schedulePriorityBased()/etc., each its own O(n)
    // locked scan, up to taskCount times per cycle: O(n^2) locked scans).
    // Not used for SchedulingPolicy::CUSTOM -- scheduleCustom() keeps its
    // own per-call snapshot/dispatch (V1's contract), which schedulerLoop
    // still drives with its own per-call loop.
    //
    // Sort key snapshot for one candidate task, copied out from under
    // tasksMutex_ by value (never a pointer -- the task may be removed and
    // deallocated the instant the lock below is released, since it is not
    // yet claimed/executing). Claiming re-validates by id under the lock
    // exactly like the pre-rework code did, so nothing here can dangle.
    struct ReadyCandidate {
        uint32_t taskId;
        SchedulerPriority priority;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::microseconds period;
    };
    // Fills readyCandidates_/readyTaskIds_ (below) rather than returning by
    // value: the HIGH-severity RT-path fix for this rework. A return-by-
    // value vector (or a fresh local `candidates`/`ids` per call) heap-
    // allocates on the scheduler thread every single cycle, including idle
    // ones -- exactly the "no heap allocation on the hot path" rule
    // (CLAUDE.md #2) this rework was supposed to be cleaning up, not
    // reintroducing. void return + persistent, clear()-only member buffers
    // mirrors the existing customSchedulerSnapshot_ pattern below: capacity
    // is retained across calls, so steady-state allocation is zero once the
    // ready-set size stabilizes.
    void selectReadyTasksForCycle();

    // Task execution. executeTask() runs the user code UNLOCKED and returns
    // the outcome without touching any lock itself; finalizeTaskExecution()
    // is the one place that takes tasksMutex_ afterwards, folding the task
    // bookkeeping (nextExecution advance, failure counters, deadline check)
    // into the SAME acquisition schedulerLoop uses to release the
    // executing-claim and check pendingRemoval (V6) -- previously two
    // separate post-execution lock acquisitions (one inside executeTask for
    // bookkeeping, one in schedulerLoop for the release), now one.
    struct TaskExecutionResult {
        bool success{true};
        std::chrono::steady_clock::time_point executionStart{};
        std::chrono::steady_clock::time_point executionEnd{};
        std::chrono::microseconds executionTime{0};
        // V2: truncated, fixed-size buffer instead of a heap std::string --
        // filled only on the (already-exceptional) failure path via
        // snprintf, no dynamic allocation. Empty ("") on success.
        char errorMessage[192]{};
    };
    struct TaskFinalizeOutcome {
        bool deactivated{false};
        bool missedDeadline{false};
        // CRITICAL fix: captured under tasksMutex_ (same acquisition that
        // may deallocate `task` back to taskPool_ via the pendingRemoval
        // branch below) so schedulerLoop's UNLOCKED error-callback dispatch
        // never dereferences `task->unit` after the task may already be
        // pool-recycled -- the exact C18 dangle shape, just reached through
        // this rework's finalizeTaskExecution() split instead of a raw lock.
        ProcessingUnit* unit{nullptr};
    };

    TaskExecutionResult executeTask(SchedulerTask& task);
    TaskFinalizeOutcome finalizeTaskExecution(SchedulerTask& task,
                                              const TaskExecutionResult& result);
    void updateTaskStatistics(SchedulerTask& task, std::chrono::microseconds executionTime);
    // V5: scheduler-thread-only writer, no lock -- see AtomicStatistics's
    // comment below for the consistency argument.
    void updateGlobalStatistics(bool success, std::chrono::microseconds executionTime,
                                bool missedDeadline);

    // Timing utilities
    std::chrono::steady_clock::time_point calculateNextExecution(const SchedulerTask& task) const;
    bool hasDeadlinePassed(const SchedulerTask& task) const;

    // Thread management
    void setThreadPriority(std::thread& thread, SchedulerPriority priority);
    void setThreadAffinity(std::thread& thread, uint32_t cpuCore);

    // Member variables
    SchedulingPolicy policy_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> realTimeMode_{false};
    // C44: one-way latch, set true (never cleared) the first time start() is
    // called. Deliberately monotonic rather than "currently running": after
    // stop() sets running_ false, executeTask() can still be draining on the
    // scheduler thread, so reopening the window on a stop/restart would
    // reintroduce the torn read this latch exists to prevent.
    std::atomic<bool> hasStarted_{false};
    std::chrono::microseconds timerResolution_{std::chrono::microseconds{1}};

    // Task management with MemoryPool
    std::unique_ptr<MemoryPool<SchedulerTask>> taskPool_;
    std::unordered_map<uint32_t, SchedulerTask*> tasks_;
    mutable std::mutex tasksMutex_;
    std::atomic<uint32_t> nextTaskId_{1};
    // C45: scheduleRoundRobin()'s rotation cursor. Was a function-local
    // static — ONE variable shared by every RealTimeScheduler instance in
    // the process, corrupting each other's rotation order and racing across
    // instances' schedulerMutex_-independent threads. Now per-instance,
    // guarded by tasksMutex_ (the lock scheduleRoundRobin already holds).
    uint32_t lastSelectedTask_{0};

    // HIGH fix: selectReadyTasksForCycle()'s scratch buffers, scheduler-
    // thread-only (that method is only ever called from schedulerLoop()),
    // refilled via clear() + push_back every cycle instead of being fresh
    // locals. Same reuse contract as customSchedulerSnapshot_ below: once
    // the ready-set size stabilizes, capacity is retained and steady-state
    // refills allocate nothing.
    std::vector<ReadyCandidate> readyCandidates_;
    std::vector<uint32_t> readyTaskIds_;

    // Scheduler thread
    std::unique_ptr<std::thread> schedulerThread_;
    std::condition_variable schedulerCondition_;
    std::mutex schedulerMutex_;
    // C42: published by schedulerLoop() itself as its first act, cleared as
    // its last (every exit path) — never read from schedulerThread_ (which
    // stop() is busy tearing down). Same discipline as system.cpp's
    // monitoringThreadId_/eventThreadId_ (C41).
    std::atomic<std::thread::id> schedulerThreadId_{};

    // Bounded e-stop halt telemetry (design spec §6.3), stored as nanosecond
    // counts rather than std::atomic<time_point> to guarantee a genuinely
    // lock-free atomic on every platform. haltRequestedAt_ is written by
    // whichever thread calls halt() -- if two callers race a duplicate
    // e-stop, "last write wins" is fine, this is diagnostic, not a gate.
    // lastTaskReturnAt_ is written ONLY by the scheduler thread, right after
    // executeTask() returns (reuses a timestamp already taken for execution
    // timing). Both read with relaxed loads in getHaltLatency().
    std::atomic<int64_t> haltRequestedAtNs_{0};
    std::atomic<int64_t> lastTaskReturnAtNs_{0};

    // Thread affinity
    uint32_t cpuCore_{0};
    bool affinitySet_{false};

    // V5: written ONLY by the scheduler thread (updateGlobalStatistics() and
    // updateSchedulingJitter(), both called exclusively from schedulerLoop),
    // read by any thread via getStatistics(). Each field is an independent
    // atomic; relaxed ordering suffices because there is exactly one writer
    // (no writer-writer race to arbitrate, only writer-reader visibility,
    // which atomicity itself provides regardless of memory order) and
    // SchedulerStatistics never promised cross-field transactional
    // consistency -- a mutex-protected snapshot mid-update was never atomic
    // w.r.t. the scheduler's OWN in-flight computation either (e.g.
    // averageExecutionTime is a running approximation, not exact). This
    // closes the "statsMutex_ taken every cycle + per task execution"
    // violation: no lock at all on this path now. getStatistics() /
    // resetStatistics() assemble/reset the public SchedulerStatistics DTO
    // from these fields; see RealTimeSchedulerStatsTest.
    // ConcurrentStatsReadDuringExecutionIsRaceFree for the TSan-clean proof
    // this rule (CLAUDE.md #8) requires for any lock-free claim.
    struct AtomicStatistics {
        std::atomic<uint64_t> totalExecutions{0};
        std::atomic<uint64_t> successfulExecutions{0};
        std::atomic<uint64_t> failedExecutions{0};
        std::atomic<uint64_t> missedDeadlines{0};
        std::atomic<uint64_t> preemptions{0};
        std::atomic<int64_t> totalExecutionTimeUs{0};
        std::atomic<int64_t> averageExecutionTimeUs{0};
        std::atomic<int64_t> maxExecutionTimeUs{0};
        std::atomic<int64_t> minExecutionTimeUs{std::chrono::microseconds::max().count()};
        std::atomic<int64_t> schedulingJitterUs{0};
        std::atomic<int64_t> averageSchedulingJitterUs{0};
    };
    AtomicStatistics statistics_;

    // Failure-handling constant shared by executeTask()'s (deleted) former
    // inline copy and finalizeTaskExecution()'s deactivation message.
    static constexpr uint64_t kMaxConsecutiveFailures = 10;

    // Custom scheduler
    CustomSchedulerCallback customScheduler_;
    // C43: reusable snapshot buffer for scheduleCustom(), refilled (clear() +
    // emplace_back) under tasksMutex_ every cycle, then handed to
    // customScheduler_ OUTSIDE the lock. Touched only from the scheduler
    // thread (scheduleCustom() is only ever called from schedulerLoop()), so
    // it needs no mutex of its own beyond the one already guarding the fill.
    // Retains its high-water-mark capacity across calls: once the task set
    // and every task's name have stabilized, refilling reuses existing
    // buffers (vector capacity + std::string capacity/SSO reuse) and
    // allocates nothing. Ceiling: a task-set change (add/remove, or a name
    // that outgrows its prior capacity) can still allocate on the next
    // cycle — closing that fully would need a fixed-capacity, pool-backed
    // buffer (mirroring taskPool_), which is more machinery than this
    // opt-in, not-RT-invariant policy (plan §C43) currently needs.
    std::vector<SchedulerTask> customSchedulerSnapshot_;

    // Error callback
    ErrorCallback errorCallback_;

    // Scheduler performance timer
    PrecisionTimer cycleTimer_;

    // Performance optimization
    std::chrono::steady_clock::time_point lastScheduleTime_;
    std::priority_queue<std::pair<std::chrono::steady_clock::time_point, uint32_t>,
                        std::vector<std::pair<std::chrono::steady_clock::time_point, uint32_t>>,
                        std::greater<>>
        taskQueue_;

    // Helper methods
    void updateSchedulingJitter(std::chrono::microseconds jitter);
};

// Main TimingController class
class TimingController {
  public:
    explicit TimingController(SchedulingPolicy policy = SchedulingPolicy::PRIORITY_BASED);
    ~TimingController();

    // Processing unit management
    uint32_t scheduleProcessingUnit(ProcessingUnit* unit,
                                    const TimingConstraints& constraints = {});
    bool removeProcessingUnit(uint32_t taskId);
    bool removeProcessingUnit(ProcessingUnit* unit);

    // System control
    void start();
    void stop();
    void pause();
    void resume();
    void reset();

    // Configuration
    void setExecutionFrequency(double frequency);
    void enableRealTimeMode(bool enable);
    void setSchedulingPolicy(SchedulingPolicy policy);
    void setTimerResolution(std::chrono::microseconds resolution);
    void setThreadAffinity(uint32_t cpuCore);

    // Performance monitoring
    SchedulerStatistics getPerformanceMetrics() const;
    void resetPerformanceMetrics();

    // Task management
    bool updateTaskConstraints(uint32_t taskId, const TimingConstraints& constraints);
    std::vector<uint32_t> getActiveTaskIds() const;
    TimingConstraints getTaskConstraints(uint32_t taskId) const;

    // State queries
    bool isRunning() const {
        return scheduler_->isRunning();
    }
    bool isPaused() const {
        return scheduler_->isPaused();
    }
    bool isRealTimeEnabled() const {
        return realTimeEnabled_;
    }
    double getExecutionFrequency() const {
        return executionFrequency_;
    }
    // C42: forwards to the scheduler; null-safe (false if no scheduler —
    // cannot happen in practice since scheduler_ is constructed in the
    // TimingController constructor and never reset, but the check costs
    // nothing and avoids relying on that invariant here).
    bool isOnSchedulerThread() const noexcept;
    // Forwards to RealTimeScheduler::getHaltLatency() -- see its doc.
    std::chrono::microseconds getHaltLatency() const noexcept;

    // Advanced features
    void setCustomScheduler(RealTimeScheduler::CustomSchedulerCallback callback);
    void setErrorCallback(RealTimeScheduler::ErrorCallback callback);
    void enableDeterministicExecution(bool enable);
    void setGlobalTimeReference(std::chrono::steady_clock::time_point reference);

    // Diagnostics and debugging
    void enableDebugLogging(bool enable);
    void exportSchedulingTrace(const std::string& filename) const;
    bool validateSchedulability() const;

    // Emergency controls
    void emergencyStop();
    void setFailsafeCallback(std::function<void(const std::string&)> callback);

  protected:
    // Core utility members for real-time performance
    // High-precision timer for measuring scheduling jitter, cycle time, and latency
    mutable PrecisionTimer schedulerTimer_{PrecisionTimer::DEFAULT_MAX_SAMPLES};
    // Thread-safe queue for scheduling commands, deferred actions, or event notifications
    // Example: ThreadSafeQueue<Command> commandQueue_;
    // Memory pool for real-time safe allocation of timing event/statistics objects
    // Example: MemoryPool<TimingEvent> eventPool_;
    // Usage hooks:
    // - Use schedulerTimer_ to time scheduling cycles and jitter
    // - Use ThreadSafeQueue for command/event scheduling
    // - Use MemoryPool for timing event/statistics allocation

  private:
    // Helper methods
    void initialize();
    void shutdown();
    TimingConstraints createDefaultConstraints(double frequency) const;

    // Task ID management
    std::unordered_map<ProcessingUnit*, uint32_t> unitToTaskId_;
    mutable std::mutex unitMapMutex_;

    // Core components
    std::unique_ptr<RealTimeScheduler> scheduler_;

    // Configuration
    double executionFrequency_{100.0}; // Default 100 Hz
    bool realTimeEnabled_{false};
    bool deterministicExecution_{false};
    bool debugLogging_{false};

    // Time reference
    std::chrono::steady_clock::time_point globalTimeReference_;
    bool hasGlobalTimeReference_{false};

    // Safety and diagnostics
    std::function<void(const std::string&)> failsafeCallback_;
    mutable std::vector<std::string> debugTrace_;
    mutable std::mutex debugMutex_;

    // Performance tracking
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<uint64_t> totalCycles_{0};
};

} // namespace axonvex::core
