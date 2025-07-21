#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <queue>
#include <string>

namespace axonvex::core {

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
    std::chrono::microseconds period{0};           // Execution period (0 = use default from frequency)
    std::chrono::microseconds deadline{0};         // Execution deadline (0 = same as period)
    std::chrono::microseconds wcet{0};             // Worst-case execution time (0 = auto-calculate)
    SchedulerPriority priority{SchedulerPriority::NORMAL};
    bool isRealTime{false};                        // Real-time task flag
    
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
    uint32_t taskId{0};
    std::string name;
    
    // Performance tracking
    uint64_t executionCount{0};
    uint64_t missedDeadlines{0};
    std::chrono::microseconds totalExecutionTime{0};
    
    // Custom constructors for atomic members
    SchedulerTask() = default;
    
    SchedulerTask(const SchedulerTask& other)
        : unit(other.unit)
        , constraints(other.constraints)
        , nextExecution(other.nextExecution)
        , lastExecution(other.lastExecution)
        , active(other.active.load())
        , executing(other.executing.load())
        , taskId(other.taskId)
        , name(other.name)
        , executionCount(other.executionCount)
        , missedDeadlines(other.missedDeadlines)
        , totalExecutionTime(other.totalExecutionTime) {}
    
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
        }
        return *this;
    }
    
    SchedulerTask(SchedulerTask&& other) noexcept
        : unit(other.unit)
        , constraints(std::move(other.constraints))
        , nextExecution(other.nextExecution)
        , lastExecution(other.lastExecution)
        , active(other.active.load())
        , executing(other.executing.load())
        , taskId(other.taskId)
        , name(std::move(other.name))
        , executionCount(other.executionCount)
        , missedDeadlines(other.missedDeadlines)
        , totalExecutionTime(other.totalExecutionTime) {}
    
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
        }
        return *this;
    }
};

// Real-time scheduler class
class RealTimeScheduler {
public:
    explicit RealTimeScheduler(SchedulingPolicy policy = SchedulingPolicy::PRIORITY_BASED);
    ~RealTimeScheduler();
    
    // Task management
    uint32_t addTask(ProcessingUnit* unit, const TimingConstraints& constraints);
    bool removeTask(uint32_t taskId);
    void removeAllTasks();
    bool updateTaskConstraints(uint32_t taskId, const TimingConstraints& constraints);
    
    // Scheduler control
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const { return running_.load(); }
    bool isPaused() const { return paused_.load(); }
    
    // Configuration
    void setSchedulingPolicy(SchedulingPolicy policy);
    void setThreadAffinity(uint32_t cpuCore);
    void enableRealTimeMode(bool enable);
    void setTimerResolution(std::chrono::microseconds resolution);
    
    // Statistics and monitoring
    SchedulerStatistics getStatistics() const;
    void resetStatistics();
    
    // Task inspection
    std::vector<uint32_t> getActiveTaskIds() const;
    TimingConstraints getTaskConstraints(uint32_t taskId) const;
    bool isTaskActive(uint32_t taskId) const;
    
    // Callback for custom scheduling
    using CustomSchedulerCallback = std::function<uint32_t(const std::vector<SchedulerTask>&)>;
    void setCustomScheduler(CustomSchedulerCallback callback);
    
    // Error callback for processing unit failures
    using ErrorCallback = std::function<void(ProcessingUnit*, const std::string&)>;
    void setErrorCallback(ErrorCallback callback);
    
private:
    // Scheduler thread function
    void schedulerLoop();
    
    // Scheduling algorithms
    uint32_t scheduleRoundRobin();
    uint32_t schedulePriorityBased();
    uint32_t scheduleEarliestDeadlineFirst();
    uint32_t scheduleRateMonotonic();
    uint32_t scheduleCustom();
    
    // Task execution
    void executeTask(SchedulerTask& task);
    void updateTaskStatistics(SchedulerTask& task, std::chrono::microseconds executionTime);
    
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
    std::chrono::microseconds timerResolution_{std::chrono::microseconds{1}};
    
    // Task management
    mutable std::mutex tasksMutex_;
    std::unordered_map<uint32_t, SchedulerTask> tasks_;
    std::atomic<uint32_t> nextTaskId_{1};
    
    // Scheduler thread
    std::unique_ptr<std::thread> schedulerThread_;
    std::condition_variable schedulerCondition_;
    std::mutex schedulerMutex_;
    
    // Thread affinity
    uint32_t cpuCore_{0};
    bool affinitySet_{false};
    
    // Statistics
    mutable std::mutex statsMutex_;
    SchedulerStatistics statistics_;
    
    // Custom scheduler
    CustomSchedulerCallback customScheduler_;
    
    // Error callback
    ErrorCallback errorCallback_;
    
    // Performance optimization
    std::chrono::steady_clock::time_point lastScheduleTime_;
    std::priority_queue<std::pair<std::chrono::steady_clock::time_point, uint32_t>, 
                       std::vector<std::pair<std::chrono::steady_clock::time_point, uint32_t>>,
                       std::greater<>> taskQueue_;
};

// Main TimingController class
class TimingController {
public:
    explicit TimingController(SchedulingPolicy policy = SchedulingPolicy::PRIORITY_BASED);
    ~TimingController();
    
    // Processing unit management
    uint32_t scheduleProcessingUnit(ProcessingUnit* unit, const TimingConstraints& constraints = {});
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
    bool isRunning() const { return scheduler_->isRunning(); }
    bool isPaused() const { return scheduler_->isPaused(); }
    bool isRealTimeEnabled() const { return realTimeEnabled_; }
    double getExecutionFrequency() const { return executionFrequency_; }
    
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