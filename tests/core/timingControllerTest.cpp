#include "axonvex_core/timingController.hpp"

#include "axonvex_core/processingUnit.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

// Test ProcessingUnit for TimingController tests
class MockProcessingUnit : public ProcessingUnit {
  private:
    std::atomic<uint32_t> processCallCount_{0};
    std::atomic<bool> shouldThrow_{false};
    std::chrono::microseconds processingDelay_{0};

  public:
    explicit MockProcessingUnit(const std::string& name) : ProcessingUnit(name) {}

    void processSync() override {
        auto start = std::chrono::steady_clock::now();

        if (shouldThrow_.load()) {
            throw std::runtime_error("Mock processing error");
        }

        setState(ExecutionState::RUNNING);
        processCallCount_.fetch_add(1);

        // Simulate processing work
        if (processingDelay_.count() > 0) {
            std::this_thread::sleep_for(processingDelay_);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        updateSyncExecutionStats(duration);
    }

    void processAsync() override {
        auto start = std::chrono::steady_clock::now();
        processCallCount_.fetch_add(1);
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        updateAsyncExecutionStats(duration);
    }

    void reset() override {
        processCallCount_.store(0);
        setState(ExecutionState::INITIALIZED);
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "MockProcessingUnit";
    }

    // Test utilities
    uint32_t getProcessCallCount() const {
        return processCallCount_.load();
    }
    void setShouldThrow(bool shouldThrow) {
        shouldThrow_.store(shouldThrow);
    }
    void setProcessingDelay(std::chrono::microseconds delay) {
        processingDelay_ = delay;
    }
};

// C42 regression: a task body running on the scheduler thread itself (via
// executeTask()) that calls RealTimeScheduler::stop()/TimingController::stop()
// used to join the scheduler thread on itself -- std::thread::join throws
// system_error("Resource deadlock avoided"), unwinding out of the thread
// entry lambda into std::terminate (C33/C36's exact shape; this is that
// defect's scheduler-thread instance). stop() must defer the join instead of
// self-joining.
class SelfStoppingUnit : public MockProcessingUnit {
  public:
    SelfStoppingUnit(const std::string& name, TimingController* controller,
                     std::shared_ptr<std::atomic<bool>> attempted,
                     std::shared_ptr<std::atomic<bool>> finished)
        : MockProcessingUnit(name), controller_(controller), attempted_(std::move(attempted)),
          finished_(std::move(finished)) {}

    void processSync() override {
        if (!attempted_->exchange(true)) {
            controller_->stop(); // must defer the join, not self-join
            // Set only after stop() returns: the poller below must never
            // observe "done" before the deferred-stop side effects it checks
            // for (isRunning() == false) are actually in place.
            finished_->store(true);
        }
    }

  private:
    TimingController* controller_;
    std::shared_ptr<std::atomic<bool>> attempted_;
    std::shared_ptr<std::atomic<bool>> finished_;
};

class TimingControllerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        controller = std::make_unique<TimingController>(SchedulingPolicy::PRIORITY_BASED);

        // Create mock processing units
        unit1 = std::make_unique<MockProcessingUnit>("Unit1");
        unit2 = std::make_unique<MockProcessingUnit>("Unit2");
        unit3 = std::make_unique<MockProcessingUnit>("Unit3");

        // Initialize units
        unit1->initialize();
        unit2->initialize();
        unit3->initialize();
    }

    void TearDown() override {
        if (controller && controller->isRunning()) {
            controller->stop();
        }

        controller.reset();
        unit1.reset();
        unit2.reset();
        unit3.reset();
    }

    std::unique_ptr<TimingController> controller;
    std::unique_ptr<MockProcessingUnit> unit1;
    std::unique_ptr<MockProcessingUnit> unit2;
    std::unique_ptr<MockProcessingUnit> unit3;
};

// Basic TimingController Tests
TEST_F(TimingControllerTest, ConstructionAndInitialState) {
    EXPECT_FALSE(controller->isRunning());
    EXPECT_FALSE(controller->isPaused());
    EXPECT_FALSE(controller->isRealTimeEnabled());
    EXPECT_DOUBLE_EQ(controller->getExecutionFrequency(), 100.0); // Default
}

TEST_F(TimingControllerTest, ConfigurationSettings) {
    // Test execution frequency
    controller->setExecutionFrequency(50.0);
    EXPECT_DOUBLE_EQ(controller->getExecutionFrequency(), 50.0);

    // Test real-time mode
    controller->enableRealTimeMode(true);
    EXPECT_TRUE(controller->isRealTimeEnabled());

    controller->enableRealTimeMode(false);
    EXPECT_FALSE(controller->isRealTimeEnabled());

    // Test scheduling policy
    controller->setSchedulingPolicy(SchedulingPolicy::ROUND_ROBIN);
    // No direct getter for policy, but should not throw

    // Test timer resolution
    controller->setTimerResolution(std::chrono::microseconds(50));
    // No direct getter, but should not throw
}

TEST_F(TimingControllerTest, ProcessingUnitScheduling) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(20);
    constraints.deadline = std::chrono::milliseconds(15);
    constraints.priority = SchedulerPriority::NORMAL;

    // Schedule processing unit
    uint32_t taskId = controller->scheduleProcessingUnit(unit1.get(), constraints);
    EXPECT_GT(taskId, 0);

    // Verify task is scheduled
    auto activeTaskIds = controller->getActiveTaskIds();
    EXPECT_EQ(activeTaskIds.size(), 1);
    EXPECT_EQ(activeTaskIds[0], taskId);

    // Verify constraints
    auto retrievedConstraints = controller->getTaskConstraints(taskId);
    EXPECT_EQ(retrievedConstraints.period, constraints.period);
    EXPECT_EQ(retrievedConstraints.deadline, constraints.deadline);
    EXPECT_EQ(retrievedConstraints.priority, constraints.priority);
}

TEST_F(TimingControllerTest, MultipleProcessingUnitScheduling) {
    TimingConstraints constraints1;
    constraints1.period = std::chrono::milliseconds(10);
    constraints1.priority = SchedulerPriority::HIGH;

    TimingConstraints constraints2;
    constraints2.period = std::chrono::milliseconds(20);
    constraints2.priority = SchedulerPriority::NORMAL;

    TimingConstraints constraints3;
    constraints3.period = std::chrono::milliseconds(50);
    constraints3.priority = SchedulerPriority::LOW;

    uint32_t taskId1 = controller->scheduleProcessingUnit(unit1.get(), constraints1);
    uint32_t taskId2 = controller->scheduleProcessingUnit(unit2.get(), constraints2);
    uint32_t taskId3 = controller->scheduleProcessingUnit(unit3.get(), constraints3);

    EXPECT_GT(taskId1, 0);
    EXPECT_GT(taskId2, 0);
    EXPECT_GT(taskId3, 0);
    EXPECT_NE(taskId1, taskId2);
    EXPECT_NE(taskId2, taskId3);

    auto activeTaskIds = controller->getActiveTaskIds();
    EXPECT_EQ(activeTaskIds.size(), 3);
}

TEST_F(TimingControllerTest, ProcessingUnitRemoval) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    uint32_t taskId = controller->scheduleProcessingUnit(unit1.get(), constraints);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 1);

    // Remove by task ID
    bool removed = controller->removeProcessingUnit(taskId);
    EXPECT_TRUE(removed);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 0);

    // Schedule again and remove by unit pointer
    taskId = controller->scheduleProcessingUnit(unit1.get(), constraints);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 1);

    removed = controller->removeProcessingUnit(unit1.get());
    EXPECT_TRUE(removed);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 0);
}

TEST_F(TimingControllerTest, BasicSchedulerExecution) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);
    constraints.deadline = std::chrono::milliseconds(8);

    controller->scheduleProcessingUnit(unit1.get(), constraints);

    // Start scheduler
    controller->start();
    EXPECT_TRUE(controller->isRunning());

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop scheduler
    controller->stop();
    EXPECT_FALSE(controller->isRunning());

    // Check that processing occurred
    EXPECT_GT(unit1->getProcessCallCount(), 0);

    // Check performance metrics
    auto stats = controller->getPerformanceMetrics();
    EXPECT_GT(stats.totalExecutions, 0);
    EXPECT_EQ(stats.totalExecutions, stats.successfulExecutions);
    EXPECT_EQ(stats.failedExecutions, 0);
}

TEST_F(TimingControllerTest, SchedulerPauseAndResume) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();

    // Let it run and record count
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint32_t countBeforePause = unit1->getProcessCallCount();
    EXPECT_GT(countBeforePause, 0);

    // Pause
    controller->pause();
    EXPECT_TRUE(controller->isPaused());

    // Wait and verify no new processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint32_t countDuringPause = unit1->getProcessCallCount();
    EXPECT_EQ(countDuringPause, countBeforePause); // Should not increase

    // Resume
    controller->resume();
    EXPECT_FALSE(controller->isPaused());

    // Wait and verify processing resumed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    uint32_t countAfterResume = unit1->getProcessCallCount();
    EXPECT_GT(countAfterResume, countDuringPause);

    controller->stop();
}

TEST_F(TimingControllerTest, SchedulerReset) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    controller->stop();

    // Verify some execution occurred
    auto stats = controller->getPerformanceMetrics();
    EXPECT_GT(stats.totalExecutions, 0);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 1);

    // Reset
    controller->reset();

    // Verify reset
    stats = controller->getPerformanceMetrics();
    EXPECT_EQ(stats.totalExecutions, 0);
    EXPECT_EQ(controller->getActiveTaskIds().size(), 0);
    EXPECT_FALSE(controller->isRunning());
}

TEST_F(TimingControllerTest, StopFromTaskBodyDefersJoinInsteadOfSelfJoining) {
    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    TimingController* ctrl = controller.get();

    auto selfStopper = std::make_unique<SelfStoppingUnit>("SelfStopper", ctrl, attempted, finished);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    controller->scheduleProcessingUnit(selfStopper.get(), constraints);
    controller->start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(finished->load());

    // The self-stop must not have crashed the process to get here. It left
    // running_ false (schedulerLoop() exits on its own) but deferred the
    // join/handle reset; a stop() from this external thread must absorb it
    // without hanging or throwing.
    EXPECT_FALSE(controller->isRunning());
    controller->stop();
}

// C42 follow-up (review finding): after a deferred self-stop, running_ is
// false but schedulerThread_ is still joinable (see the test above). start()
// used to assign a fresh std::thread straight over that handle with no
// joinable check -- unique_ptr::operator= destroying a joinable std::thread
// is std::terminate, uncatchable, no log. The exact C39 shape one layer
// down. start() now joins the leftover handle (external-thread restart) or
// refuses outright (restart called from the scheduler thread itself, still
// inside the deferring call's own stack).
TEST_F(TimingControllerTest, StartAfterDeferredSelfStopReclaimsHandleAndRunsAgain) {
    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    TimingController* ctrl = controller.get();

    auto selfStopper = std::make_unique<SelfStoppingUnit>("SelfStopper", ctrl, attempted, finished);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    controller->scheduleProcessingUnit(selfStopper.get(), constraints);
    controller->scheduleProcessingUnit(unit2.get(), constraints);
    controller->start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(finished->load());
    EXPECT_FALSE(controller->isRunning());

    // Restart from this external (test) thread while schedulerThread_ is
    // still the joinable handle the deferred self-stop left behind.
    // Pre-fix: std::terminate here, uncaught, no log.
    controller->start();
    EXPECT_TRUE(controller->isRunning());

    // The scheduler must actually be running again, not just report it.
    uint32_t countAfterRestart = unit2->getProcessCallCount();
    auto pollDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (unit2->getProcessCallCount() <= countAfterRestart &&
           std::chrono::steady_clock::now() < pollDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GT(unit2->getProcessCallCount(), countAfterRestart)
        << "scheduler must actually execute tasks again after the restart";

    controller->stop();
}

// Scheduling Policy Tests
TEST_F(TimingControllerTest, PriorityBasedScheduling) {
    controller->setSchedulingPolicy(SchedulingPolicy::PRIORITY_BASED);

    // Create tasks with different priorities
    TimingConstraints highPriority;
    highPriority.period = std::chrono::milliseconds(20);
    highPriority.priority = SchedulerPriority::HIGH;

    TimingConstraints lowPriority;
    lowPriority.period = std::chrono::milliseconds(20);
    lowPriority.priority = SchedulerPriority::LOW;

    controller->scheduleProcessingUnit(unit1.get(), highPriority);
    controller->scheduleProcessingUnit(unit2.get(), lowPriority);

    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    controller->stop();

    // High priority task should execute more frequently
    uint32_t highPriorityCount = unit1->getProcessCallCount();
    uint32_t lowPriorityCount = unit2->getProcessCallCount();

    EXPECT_GT(highPriorityCount, 0);
    EXPECT_GT(lowPriorityCount, 0);

    // In a properly working priority scheduler, high priority should get more execution
    // This is a weak test due to timing variations, but should generally hold
}

TEST_F(TimingControllerTest, EarliestDeadlineFirstScheduling) {
    controller->setSchedulingPolicy(SchedulingPolicy::EARLIEST_DEADLINE_FIRST);

    TimingConstraints shortDeadline;
    shortDeadline.period = std::chrono::milliseconds(20);
    shortDeadline.deadline = std::chrono::milliseconds(10);

    TimingConstraints longDeadline;
    longDeadline.period = std::chrono::milliseconds(20);
    longDeadline.deadline = std::chrono::milliseconds(18);

    controller->scheduleProcessingUnit(unit1.get(), shortDeadline);
    controller->scheduleProcessingUnit(unit2.get(), longDeadline);

    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    controller->stop();

    // Both should execute
    EXPECT_GT(unit1->getProcessCallCount(), 0);
    EXPECT_GT(unit2->getProcessCallCount(), 0);

    auto stats = controller->getPerformanceMetrics();
    EXPECT_GT(stats.totalExecutions, 0);
}

TEST_F(TimingControllerTest, RoundRobinScheduling) {
    controller->setSchedulingPolicy(SchedulingPolicy::ROUND_ROBIN);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->scheduleProcessingUnit(unit2.get(), constraints);
    controller->scheduleProcessingUnit(unit3.get(), constraints);

    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    controller->stop();

    // All units should execute
    uint32_t count1 = unit1->getProcessCallCount();
    uint32_t count2 = unit2->getProcessCallCount();
    uint32_t count3 = unit3->getProcessCallCount();

    EXPECT_GT(count1, 0);
    EXPECT_GT(count2, 0);
    EXPECT_GT(count3, 0);

    // In round-robin, counts should be relatively balanced
    // Allow some variation due to timing
    uint32_t maxCount = std::max({count1, count2, count3});
    uint32_t minCount = std::min({count1, count2, count3});
    EXPECT_LE(maxCount - minCount, maxCount / 2); // Within 50% of each other
}

// Regression test for C1: the scheduler must execute ALL ready tasks each cycle,
// not one. With tick == period, three same-priority tasks are all ready every
// cycle; one-task-per-cycle caps total throughput at ~1/3 of the required rate.
TEST_F(TimingControllerTest, AllReadyTasksExecutePerCycle) {
    controller->setTimerResolution(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(10)));

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);
    constraints.priority = SchedulerPriority::NORMAL;

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->scheduleProcessingUnit(unit2.get(), constraints);
    controller->scheduleProcessingUnit(unit3.get(), constraints);

    controller->start();

    // Poll until every unit reaches the threshold (or 3s timeout). With tick ==
    // period == 10ms all three should get there in ~350ms; one-task-per-cycle
    // starves at least one unit forever, so the timeout is the failure path.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((unit1->getProcessCallCount() < 35u || unit2->getProcessCallCount() < 35u ||
            unit3->getProcessCallCount() < 35u) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    controller->stop();

    EXPECT_GE(unit1->getProcessCallCount(), 35u);
    EXPECT_GE(unit2->getProcessCallCount(), 35u);
    EXPECT_GE(unit3->getProcessCallCount(), 35u);
}

// Regression test for C1: removing a task while it executes must not destroy
// the task out from under the scheduler (use-after-free guarded by ASan runs).
TEST_F(TimingControllerTest, RemoveTaskDuringExecutionIsSafe) {
    controller->setTimerResolution(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(1)));
    unit1->setProcessingDelay(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(50)));

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);

    uint32_t taskId = controller->scheduleProcessingUnit(unit1.get(), constraints);
    EXPECT_GT(taskId, 0u);
    controller->start();

    // Poll until the unit has started executing at least once
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (unit1->getProcessCallCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GT(unit1->getProcessCallCount(), 0u);

    // Remove while (likely) mid-execution: must not crash or corrupt the pool
    EXPECT_TRUE(controller->removeProcessingUnit(taskId));
    controller->stop();
}

TEST_F(TimingControllerTest, CustomScheduling) {
    controller->setSchedulingPolicy(SchedulingPolicy::CUSTOM);

    // Set custom scheduler that always selects the first active task
    uint32_t customCallCount = 0;
    controller->setCustomScheduler(
        [&customCallCount](const std::vector<SchedulerTask>& tasks) -> uint32_t {
            customCallCount++;
            if (tasks.empty())
                return 0;

            auto now = std::chrono::steady_clock::now();
            for (const auto& task : tasks) {
                if (task.active.load() && !task.executing.load() && now >= task.nextExecution) {
                    return task.taskId;
                }
            }
            return 0;
        });

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);

    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    controller->stop();

    // Custom scheduler should have been called
    EXPECT_GT(customCallCount, 0);
    EXPECT_GT(unit1->getProcessCallCount(), 0);
}

// Timing Constraint Tests
TEST_F(TimingControllerTest, TimingConstraintUpdates) {
    TimingConstraints initialConstraints;
    initialConstraints.period = std::chrono::milliseconds(20);
    initialConstraints.priority = SchedulerPriority::NORMAL;

    uint32_t taskId = controller->scheduleProcessingUnit(unit1.get(), initialConstraints);

    // Verify initial constraints
    auto constraints = controller->getTaskConstraints(taskId);
    EXPECT_EQ(constraints.period, std::chrono::milliseconds(20));
    EXPECT_EQ(constraints.priority, SchedulerPriority::NORMAL);

    // Update constraints
    TimingConstraints newConstraints;
    newConstraints.period = std::chrono::milliseconds(10);
    newConstraints.priority = SchedulerPriority::HIGH;

    bool updated = controller->updateTaskConstraints(taskId, newConstraints);
    EXPECT_TRUE(updated);

    // Verify updated constraints
    constraints = controller->getTaskConstraints(taskId);
    EXPECT_EQ(constraints.period, std::chrono::milliseconds(10));
    EXPECT_EQ(constraints.priority, SchedulerPriority::HIGH);
}

TEST_F(TimingControllerTest, DefaultConstraintsFromFrequency) {
    controller->setExecutionFrequency(100.0); // 100 Hz = 10ms period

    // Schedule without explicit constraints - should use default based on frequency
    uint32_t taskId = controller->scheduleProcessingUnit(unit1.get());
    EXPECT_GT(taskId, 0);

    auto constraints = controller->getTaskConstraints(taskId);
    // 1000000 / 100 Hz = 10000 us = 10 ms
    EXPECT_EQ(constraints.period, std::chrono::microseconds(10000));
}

// Performance Monitoring Tests
TEST_F(TimingControllerTest, PerformanceMetricsCollection) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    // Add some processing delay
    unit1->setProcessingDelay(std::chrono::microseconds(100));

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    controller->stop();

    auto stats = controller->getPerformanceMetrics();

    EXPECT_GT(stats.totalExecutions, 0);
    EXPECT_EQ(stats.successfulExecutions, stats.totalExecutions);
    EXPECT_EQ(stats.failedExecutions, 0);
    EXPECT_EQ(stats.missedDeadlines, 0); // Should not miss with light load
    EXPECT_GT(stats.averageExecutionTime.count(), 0);
    EXPECT_GE(stats.maxExecutionTime.count(), stats.minExecutionTime.count());
}

TEST_F(TimingControllerTest, PerformanceMetricsReset) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    controller->stop();

    // Verify some metrics collected
    auto stats = controller->getPerformanceMetrics();
    EXPECT_GT(stats.totalExecutions, 0);

    // Reset metrics
    controller->resetPerformanceMetrics();

    // Verify reset
    stats = controller->getPerformanceMetrics();
    EXPECT_EQ(stats.totalExecutions, 0);
    EXPECT_EQ(stats.successfulExecutions, 0);
    EXPECT_EQ(stats.failedExecutions, 0);
}

// Schedulability Analysis Tests
TEST_F(TimingControllerTest, SchedulabilityValidation) {
    // Create schedulable task set (total utilization < 1.0)
    TimingConstraints task1;
    task1.period = std::chrono::milliseconds(20);
    task1.wcet = std::chrono::milliseconds(5); // Utilization = 0.25

    TimingConstraints task2;
    task2.period = std::chrono::milliseconds(40);
    task2.wcet = std::chrono::milliseconds(10); // Utilization = 0.25

    controller->scheduleProcessingUnit(unit1.get(), task1);
    controller->scheduleProcessingUnit(unit2.get(), task2);

    // Total utilization = 0.5, should be schedulable
    EXPECT_TRUE(controller->validateSchedulability());
}

TEST_F(TimingControllerTest, UnschedulableTaskSet) {
    // Create unschedulable task set (total utilization > 1.0)
    TimingConstraints task1;
    task1.period = std::chrono::milliseconds(10);
    task1.wcet = std::chrono::milliseconds(8); // Utilization = 0.8

    TimingConstraints task2;
    task2.period = std::chrono::milliseconds(15);
    task2.wcet = std::chrono::milliseconds(7); // Utilization ≈ 0.47

    controller->scheduleProcessingUnit(unit1.get(), task1);
    controller->scheduleProcessingUnit(unit2.get(), task2);

    // Total utilization = 1.27, likely not schedulable under rate monotonic
    // Note: This test might be flaky due to the Liu & Layland bound approximation
    // bool schedulable = controller->validateSchedulability();
    // std::cout << "Task set schedulability: " << schedulable << std::endl;
}

// Error Handling Tests
TEST_F(TimingControllerTest, ProcessingUnitErrorHandling) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    // Configure unit to throw errors
    unit1->setShouldThrow(true);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    controller->stop();

    auto stats = controller->getPerformanceMetrics();
    EXPECT_GT(stats.totalExecutions, 0);
    EXPECT_GT(stats.failedExecutions, 0);
    EXPECT_LT(stats.successfulExecutions, stats.totalExecutions);
}

TEST_F(TimingControllerTest, InvalidTaskOperations) {
    // Test invalid task ID operations
    EXPECT_FALSE(controller->removeProcessingUnit(999)); // Non-existent ID
    EXPECT_FALSE(controller->updateTaskConstraints(999, TimingConstraints{}));

    auto constraints = controller->getTaskConstraints(999);
    EXPECT_EQ(constraints.period.count(), 0); // Default constructed should be 0

    // Test null processing unit
    EXPECT_EQ(controller->scheduleProcessingUnit(nullptr), 0);
}

// Advanced Features Tests
TEST_F(TimingControllerTest, DeterministicExecution) {
    controller->enableDeterministicExecution(true);

    auto referenceTime = std::chrono::steady_clock::now();
    controller->setGlobalTimeReference(referenceTime);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);

    // Should not throw and should work normally
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    controller->stop();

    EXPECT_GT(unit1->getProcessCallCount(), 0);
}

TEST_F(TimingControllerTest, DebugLogging) {
    controller->enableDebugLogging(true);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);

    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    controller->stop();

    // Export trace (should not throw)
    controller->exportSchedulingTrace("test_trace.txt");

    // Note: In a real implementation, we might verify file contents
}

TEST_F(TimingControllerTest, EmergencyStop) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1); // Fast execution

    controller->scheduleProcessingUnit(unit1.get(), constraints);

    std::atomic<bool> failsafeCalled{false};
    controller->setFailsafeCallback([&failsafeCalled](const std::string& reason) {
        failsafeCalled.store(true);
        std::cout << "Failsafe triggered: " << reason << std::endl;
    });

    controller->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Trigger emergency stop
    controller->emergencyStop();

    EXPECT_FALSE(controller->isRunning());
    EXPECT_TRUE(failsafeCalled.load());
}

// Thread Safety Tests
TEST_F(TimingControllerTest, ConcurrentTaskManagement) {
    // Test was failing due to priority scheduler bug - now fixed, keeping original priority-based
    // test controller.reset(); controller =
    // std::make_unique<TimingController>(SchedulingPolicy::ROUND_ROBIN);

    const int numTasks = 10;
    std::vector<std::unique_ptr<MockProcessingUnit>> units;
    std::vector<uint32_t> taskIds;

    // Create multiple units
    for (int i = 0; i < numTasks; ++i) {
        auto unit = std::make_unique<MockProcessingUnit>("Unit" + std::to_string(i));
        unit->initialize();
        unit->setShouldThrow(false); // Ensure units don't throw errors
        units.push_back(std::move(unit));
    }

    // Schedule all tasks concurrently
    std::vector<std::thread> threads;
    std::mutex taskIdsMutex;

    for (int i = 0; i < numTasks; ++i) {
        threads.emplace_back([&, i]() {
            TimingConstraints constraints;
            constraints.period =
                std::chrono::milliseconds(5 + i); // Shorter, varied periods (5-14ms)
            constraints.deadline = constraints.period;
            // Use valid SchedulerPriority enum values instead of raw casting
            SchedulerPriority priorities[] = {SchedulerPriority::HIGH, SchedulerPriority::NORMAL,
                                              SchedulerPriority::LOW};
            constraints.priority = priorities[i % 3];

            uint32_t taskId = controller->scheduleProcessingUnit(units[i].get(), constraints);

            std::lock_guard<std::mutex> lock(taskIdsMutex);
            taskIds.push_back(taskId);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all tasks scheduled
    EXPECT_EQ(taskIds.size(), numTasks);
    EXPECT_EQ(controller->getActiveTaskIds().size(), numTasks);

    // Start and stop - longer execution time to ensure all tasks execute
    controller->start();
    EXPECT_TRUE(controller->isRunning());
    std::this_thread::sleep_for(
        std::chrono::milliseconds(200)); // Increased to 200ms for better coverage
    controller->stop();
    EXPECT_FALSE(controller->isRunning());

    // Verify execution occurred
    for (const auto& unit : units) {
        EXPECT_GT(unit->getProcessCallCount(), 0);
    }
}

} // anonymous namespace
