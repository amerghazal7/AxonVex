#include <axonvex/core/timingController.hpp>
#include <axonvex/core/processingUnit.hpp>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

#ifdef __linux__
    #include <sched.h>
    #include <pthread.h>
    #include <sys/mman.h>
#elif _WIN32
    #include <windows.h>
    #include <timeapi.h>
#endif

// Usage documentation for core utilities:
// - Use schedulerTimer_ to measure each scheduling cycle and track jitter/latency.
// - Use ThreadSafeQueue (e.g., commandQueue_) for thread-safe command/event scheduling.
// - Use MemoryPool (e.g., eventPool_) for real-time safe allocation of timing events/statistics.
// These members are available in the TimingController base class for use in implementation and extensions.

namespace axonvex::core {

// RealTimeScheduler Implementation
RealTimeScheduler::RealTimeScheduler(SchedulingPolicy policy)
    : policy_(policy) {
    // Initialize the task pool with a default capacity
    taskPool_ = std::make_unique<MemoryPool<SchedulerTask>>(128);
}

RealTimeScheduler::~RealTimeScheduler() {
    if (running_.load()) {
        stop();
    }
}

uint32_t RealTimeScheduler::addTask(ProcessingUnit* unit, const TimingConstraints& constraints) {
    if (!unit) {
        return 0; // Invalid task ID
    }

    std::lock_guard<std::mutex> lock(tasksMutex_);

    // Allocate a new task from the memory pool
    SchedulerTask* task = taskPool_->allocateObject();
    if (!task) {
        // Handle pool exhaustion
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
        taskPool_->deallocateObject(task); // Deallocate back to the pool
        tasks_.erase(it);
        return true;
    }
    return false;
}

void RealTimeScheduler::removeAllTasks() {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    for (auto& pair : tasks_) {
        pair.second->active.store(false);
        taskPool_->deallocateObject(pair.second);
    }
    tasks_.clear();
}

bool RealTimeScheduler::updateTaskConstraints(uint32_t taskId, const TimingConstraints& constraints) {
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
    if (running_.load()) {
        return; // Already running
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
    if (!running_.load()) {
        return; // Not running
    }

    running_.store(false);
    schedulerCondition_.notify_all();

    if (schedulerThread_ && schedulerThread_->joinable()) {
        schedulerThread_->join();
    }
    schedulerThread_.reset();
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
    std::lock_guard<std::mutex> lock(statsMutex_);
    return statistics_;
}

void RealTimeScheduler::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    statistics_.reset();
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
    std::lock_guard<std::mutex> lock(schedulerMutex_);
    customScheduler_ = callback;
}

void RealTimeScheduler::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(schedulerMutex_);
    errorCallback_ = callback;
}

void RealTimeScheduler::schedulerLoop() {
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
                schedulerCondition_.wait(lock, [this] { return !paused_.load() || !running_.load(); });
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
        auto expectedCycleTime = std::chrono::duration_cast<std::chrono::nanoseconds>(timerResolution_);
        auto jitter = std::chrono::abs(cycleTime - expectedCycleTime);
        updateSchedulingJitter(std::chrono::duration_cast<std::chrono::microseconds>(jitter));

        nextWakeup = now + timerResolution_;

        // Select next task to execute based on scheduling policy
        uint32_t selectedTaskId = 0;
        switch (policy_) {
            case SchedulingPolicy::ROUND_ROBIN:
                selectedTaskId = scheduleRoundRobin();
                break;
            case SchedulingPolicy::PRIORITY_BASED:
                selectedTaskId = schedulePriorityBased();
                break;
            case SchedulingPolicy::EARLIEST_DEADLINE_FIRST:
                selectedTaskId = scheduleEarliestDeadlineFirst();
                break;
            case SchedulingPolicy::RATE_MONOTONIC:
                selectedTaskId = scheduleRateMonotonic();
                break;
            case SchedulingPolicy::CUSTOM:
                selectedTaskId = scheduleCustom();
                break;
        }

        // Check for tasks that need reactivation
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            for (auto& [taskId, task] : tasks_) {
                if (!task->active.load() &&
                    task->consecutiveFailures > 0 &&
                    now >= task->reactivationTime) {
                    task->active.store(true);
                    task->consecutiveFailures = 0; // Reset failure count on reactivation
                }
            }
        }

        // Execute selected task
        if (selectedTaskId != 0) {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            auto it = tasks_.find(selectedTaskId);
            if (it != tasks_.end() && it->second->active.load()) {
                executeTask(*it->second);
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

    static uint32_t lastSelectedTask = 0;
    auto now = std::chrono::steady_clock::now();

    // Find next task after the last selected one
    auto it = tasks_.find(lastSelectedTask);
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
            lastSelectedTask = it->first;
            return it->first;
        }

        ++it;
    }

    return 0;
}

uint32_t RealTimeScheduler::scheduleCustom() {
    if (!customScheduler_) {
        return schedulePriorityBased(); // Fallback
    }

    std::lock_guard<std::mutex> lock(tasksMutex_);
    std::vector<SchedulerTask> activeTasks;

    for (const auto& pair : tasks_) {
        if (pair.second->active.load()) {
            activeTasks.emplace_back(*pair.second);
        }
    }

    return customScheduler_(activeTasks);
}

void RealTimeScheduler::executeTask(SchedulerTask& task) {
    if (task.executing.load()) {
        return; // Already executing
    }

    task.executing.store(true);
    auto executionStart = std::chrono::steady_clock::now();

    try {
        // Execute the async processing unit
        task.unit->processAsyncBase();

        // Execute the sync processing unit
        task.unit->processSyncBase();

        // Calculate the execution time
        auto executionEnd = std::chrono::steady_clock::now();
        auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            executionEnd - executionStart);

        // Update task statistics
        updateTaskStatistics(task, executionTime);

        // Update next execution time
        task.lastExecution = executionStart;
        task.nextExecution = calculateNextExecution(task);
        task.consecutiveFailures = 0; // Reset failure count on successful execution

        // Update global statistics
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            statistics_.totalExecutions++;
            statistics_.successfulExecutions++;
            statistics_.totalExecutionTime += executionTime;

            if (statistics_.totalExecutions == 1) {
                statistics_.averageExecutionTime = executionTime;
                statistics_.minExecutionTime = executionTime;
                statistics_.maxExecutionTime = executionTime;
            } else {
                // Update running averages
                auto avgCount = statistics_.totalExecutions;
                auto oldAvg = statistics_.averageExecutionTime.count();
                auto newAvg = (oldAvg * (avgCount - 1) + executionTime.count()) / avgCount;
                statistics_.averageExecutionTime = std::chrono::microseconds(static_cast<long long>(newAvg));

                if (executionTime < statistics_.minExecutionTime) {
                    statistics_.minExecutionTime = executionTime;
                }
                if (executionTime > statistics_.maxExecutionTime) {
                    statistics_.maxExecutionTime = executionTime;
                }
            }

            // Check for deadline miss
            if (hasDeadlinePassed(task)) {
                statistics_.missedDeadlines++;
            }
        }

    } catch (const std::exception& e) {
        // Handle execution error
        auto executionEnd = std::chrono::steady_clock::now();
        auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(
            executionEnd - executionStart);

        std::lock_guard<std::mutex> lock(statsMutex_);
        statistics_.totalExecutions++;  // Count failed executions in total
        statistics_.failedExecutions++;
        statistics_.totalExecutionTime += executionTime;

        // Update task statistics for failed execution
        task.executionCount++;
        task.totalExecutionTime += executionTime;
        task.consecutiveFailures++;
        task.lastFailureTime = executionEnd;

        // Temporarily deactivate task if too many consecutive failures
        const uint64_t MAX_CONSECUTIVE_FAILURES = 10;
        const auto FAILURE_BACKOFF_TIME = std::chrono::milliseconds(100);

        if (task.consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
            task.active.store(false);
            task.reactivationTime = executionEnd + FAILURE_BACKOFF_TIME;

            // Log task deactivation
            if (errorCallback_) {
                std::string msg = "Task temporarily deactivated after " +
                                std::to_string(MAX_CONSECUTIVE_FAILURES) +
                                " consecutive failures: " + e.what();
                errorCallback_(task.unit, msg.c_str());
            }
        }

        // Notify error callback if available
        if (errorCallback_) {
            errorCallback_(task.unit, e.what());
        }
        if (hasDeadlinePassed(task)) {
            statistics_.missedDeadlines++;
            task.missedDeadlines++;
        }

        // Log error if debugging is enabled (but throttled)
        if (task.consecutiveFailures <= MAX_CONSECUTIVE_FAILURES) {
            std::cerr << "Task execution failed: " << e.what() << std::endl;
        }
    }

    task.executing.store(false);
}

void RealTimeScheduler::updateTaskStatistics(SchedulerTask& task, std::chrono::microseconds executionTime) {
    task.executionCount++;
    task.totalExecutionTime += executionTime;

    if (hasDeadlinePassed(task)) {
        task.missedDeadlines++;
    }
}

void RealTimeScheduler::updateSchedulingJitter(std::chrono::microseconds jitter) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    statistics_.schedulingJitter = jitter;

    // Update running average for jitter
    auto avgCount = statistics_.totalExecutions;
    if (avgCount > 0) {
        auto oldAvg = statistics_.averageSchedulingJitter.count();
        auto newAvg = (oldAvg * (avgCount - 1) + jitter.count()) / avgCount;
        statistics_.averageSchedulingJitter = std::chrono::microseconds(static_cast<long long>(newAvg));
    } else {
        statistics_.averageSchedulingJitter = jitter;
    }
}

std::chrono::steady_clock::time_point RealTimeScheduler::calculateNextExecution(const SchedulerTask& task) const {
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
                                     (maxPrio - minPrio) / (static_cast<int>(SchedulerPriority::CRITICAL) -
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
        case SchedulerPriority::IDLE: winPriority = THREAD_PRIORITY_IDLE; break;
        case SchedulerPriority::LOW: winPriority = THREAD_PRIORITY_BELOW_NORMAL; break;
        case SchedulerPriority::NORMAL: winPriority = THREAD_PRIORITY_NORMAL; break;
        case SchedulerPriority::HIGH: winPriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case SchedulerPriority::REAL_TIME: winPriority = THREAD_PRIORITY_TIME_CRITICAL; break;
        case SchedulerPriority::CRITICAL: winPriority = THREAD_PRIORITY_TIME_CRITICAL; break;
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

uint32_t TimingController::scheduleProcessingUnit(ProcessingUnit* unit, const TimingConstraints& constraints) {
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

bool TimingController::updateTaskConstraints(uint32_t taskId, const TimingConstraints& constraints) {
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
    if (n == 0) return true;

    double bound = n * (std::pow(2.0, 1.0/n) - 1.0);
    return totalUtilization <= bound;
}

void TimingController::emergencyStop() {
    scheduler_->stop();

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
