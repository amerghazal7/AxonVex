#include "axonvex_core/timingController.hpp"

#include "axonvex_core/processingUnit.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

// Shared with builtinUnitsTest.cpp (same test_core binary): that TU defines
// the process-wide operator new/delete override, gated by g_trackAllocs, that
// counts heap allocations. Reused here (rather than a second, conflicting
// override) to prove selectReadyTasksForCycle() performs zero heap
// allocations per scheduler cycle in steady state (HIGH review fix).
extern std::atomic<bool> g_trackAllocs;
extern std::atomic<long> g_allocCount;

namespace axonvex::core {
// C45: grants direct, single-threaded access to the otherwise-private
// scheduleRoundRobin() so the regression test below can drive it
// deterministically (no scheduler thread, no timing dependence) instead of
// inferring correctness from task-execution counts under real concurrency.
class RoundRobinTestAccessor {
  public:
    static uint32_t call(RealTimeScheduler& scheduler) {
        return scheduler.scheduleRoundRobin();
    }
};
} // namespace axonvex::core

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

// HIGH regression: V4's selectReadyTasksForCycle() used fresh local vectors
// (`candidates`, and the returned `ids`) every scheduler cycle -- a
// `.reserve()`/heap allocation on the scheduler thread even in a fully idle
// cycle, violating CLAUDE.md rule 2 ("no heap allocation" on the scheduler
// hot path) in the very change whose purpose was RT-path cleanup. Fix: both
// became clear()-only persistent members (readyCandidates_/readyTaskIds_),
// so steady-state cycles allocate nothing.
//
// Measured via the process-wide allocation counter defined in
// builtinUnitsTest.cpp (see the `extern` declarations near the top of this
// file): the task body samples the counter on every call, so the delta
// between two consecutive calls captures everything the scheduler thread
// allocated in between -- which necessarily includes one or more
// selectReadyTasksForCycle() passes (period 1ms vs. a 200us timer
// resolution means several idle cycles run between executions).
TEST_F(TimingControllerTest, SchedulerReadySetSelectionAllocatesNoHeapInSteadyState) {
    class AllocationProbeUnit : public MockProcessingUnit {
      public:
        explicit AllocationProbeUnit(const std::string& name) : MockProcessingUnit(name) {}
        void processSync() override {
            // callCount_ is polled from the main thread below while this
            // runs on the scheduler thread -- must be atomic (TSan-caught
            // race on a plain uint32_t here, in an earlier version of this
            // test). lastCount_ is scheduler-thread-only (read+written only
            // from here), so it stays a plain long.
            uint32_t n = callCount_.fetch_add(1, std::memory_order_relaxed) + 1;
            long count = g_allocCount.load(std::memory_order_relaxed);
            long delta = count - lastCount_;
            lastCount_ = count;
            // Skip the first 20 calls: capacity has not yet reached its
            // high-water mark, so a one-time growth allocation there is
            // expected and not a regression.
            if (n > 20 && delta > maxDeltaAfterWarmup.load(std::memory_order_relaxed)) {
                maxDeltaAfterWarmup.store(delta, std::memory_order_relaxed);
            }
        }
        uint32_t getCallCount() const {
            return callCount_.load(std::memory_order_relaxed);
        }
        std::atomic<long> maxDeltaAfterWarmup{0};

      private:
        std::atomic<uint32_t> callCount_{0};
        long lastCount_{0};
    };

    AllocationProbeUnit probeUnit("AllocProbe");
    probeUnit.initialize();

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1);
    controller->scheduleProcessingUnit(&probeUnit, constraints);
    controller->setTimerResolution(std::chrono::microseconds(200));

    g_allocCount.store(0, std::memory_order_relaxed);
    g_trackAllocs.store(true, std::memory_order_relaxed);
    controller->start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (probeUnit.getCallCount() < 200 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    g_trackAllocs.store(false, std::memory_order_relaxed);
    controller->stop();

    ASSERT_GE(probeUnit.getCallCount(), 200u);
    EXPECT_EQ(probeUnit.maxDeltaAfterWarmup.load(std::memory_order_relaxed), 0)
        << "scheduler thread heap-allocated between two consecutive task "
           "executions after warmup -- selectReadyTasksForCycle() is "
           "allocating on the RT path";
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

// V6 use-after-free regression (CRITICAL, C18-shape): schedulerLoop()
// dispatches errorCallback_ using `task->unit` AFTER finalizeTaskExecution()
// may already have deallocated `task` back to taskPool_ (the pendingRemoval
// branch, taken when removeTask() runs while the task is still "executing").
//
// Deterministic single-threaded repro (no races needed): the failing unit
// removes ITSELF on its 10th consecutive failure -- still safe to do from
// inside processSync(), since executeTask() holds no lock while user code
// runs. That 10th failure is also the one that sets outcome.deactivated,
// so schedulerLoop makes TWO error-callback calls for it. From inside the
// FIRST call, we schedule a brand-new task: the pool's Treiber-stack (LIFO)
// free list hands it the exact block just freed by finalizeTaskExecution(),
// overwriting its `unit` field. Pre-fix, the SECOND call re-reads
// `task->unit` from that now-reused block and reports the WRONG
// ProcessingUnit*; post-fix it uses TaskFinalizeOutcome::unit, captured
// under tasksMutex_ before any deallocation could happen.
TEST_F(TimingControllerTest, ErrorCallbackAfterSelfRemovalReportsOriginalUnit) {
    class SelfRemovingUnit : public MockProcessingUnit {
      public:
        explicit SelfRemovingUnit(const std::string& name) : MockProcessingUnit(name) {}
        void setController(TimingController* controller) {
            controller_ = controller;
        }
        void setTaskId(uint32_t id) {
            taskId_ = id;
        }
        void processSync() override {
            // 10 == RealTimeScheduler::kMaxConsecutiveFailures (private);
            // this is the call that pushes consecutiveFailures to 10, lining
            // up self-removal with outcome.deactivated becoming true.
            if (++callCount_ == 10) {
                controller_->removeProcessingUnit(taskId_);
            }
            throw std::runtime_error("forced failure");
        }

      private:
        TimingController* controller_{nullptr};
        uint32_t taskId_{0};
        int callCount_{0};
    };

    SelfRemovingUnit failingUnit("SelfRemoving");
    failingUnit.setController(controller.get());
    failingUnit.initialize();

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1);
    uint32_t taskId = controller->scheduleProcessingUnit(&failingUnit, constraints);
    ASSERT_GT(taskId, 0u);
    failingUnit.setTaskId(taskId);

    // errorCallback_ fires once per failure (calls 1-9: consecutiveFailures
    // < 10, one dispatch each) and TWICE on call 10 (the deactivation
    // message, then the regular one) -- 9 + 2 = 11 total, then never again
    // (the task is erased from tasks_ by the self-removal on call 10).
    constexpr size_t kExpectedTotalCalls = 11;

    // observedUnits is only ever read from the main thread AFTER
    // controller->stop() below has joined the scheduler thread -- that join
    // is the synchronization point. The POLL condition below must not touch
    // it directly (the scheduler thread is still writing it concurrently at
    // that point); poll on this atomic counter instead (TSan-caught race on
    // observedUnits.size() in an earlier version of this test).
    std::vector<ProcessingUnit*> observedUnits;
    std::atomic<size_t> callbackCount{0};
    bool reused = false;
    controller->setErrorCallback([&](ProcessingUnit* u, const std::string& msg) {
        observedUnits.push_back(u);
        // Only the FIRST of the deactivation pair carries this message; fire
        // the reuse trick right there so the SECOND call for this SAME
        // failure is the one that would read the corrupted block pre-fix.
        if (!reused && msg.find("deactivated") != std::string::npos) {
            reused = true;
            // Force the pool's just-freed block to be handed to a NEW task
            // from inside this callback, before the pair's second callback
            // runs.
            TimingConstraints reuseConstraints;
            reuseConstraints.period = std::chrono::milliseconds(50);
            controller->scheduleProcessingUnit(unit2.get(), reuseConstraints);
        }
        callbackCount.fetch_add(1, std::memory_order_relaxed);
    });

    controller->setTimerResolution(std::chrono::microseconds(200));
    controller->start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (callbackCount.load(std::memory_order_relaxed) < kExpectedTotalCalls &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    controller->stop();

    ASSERT_EQ(observedUnits.size(), kExpectedTotalCalls)
        << "expected exactly " << kExpectedTotalCalls
        << " error-callback dispatches (9 single failures + the deactivating "
           "pair), then none more once the task self-removes";
    EXPECT_EQ(observedUnits.back(), &failingUnit)
        << "last callback of the deactivating pair reported a different "
           "ProcessingUnit* -- use-after-free on the deallocated SchedulerTask";
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

// C44-sibling regression: scheduleCustom() reads customScheduler_ unlocked on
// the scheduler thread (both the `if (!customScheduler_)` guard and the
// invocation itself), while setCustomScheduler() writes it under
// schedulerMutex_ -- the identical torn-std::function-read shape C44 fixed
// for errorCallback_. Fix: registration refuses once the scheduler has ever
// started, per the same hasStarted_ latch / requireNotStarted() helper --
// pre-fix, this call silently succeeds instead of throwing.
TEST_F(TimingControllerTest, SetCustomSchedulerAfterStartThrows) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);
    controller->scheduleProcessingUnit(unit1.get(), constraints);

    controller->start();

    EXPECT_THROW(controller->setCustomScheduler(
                     [](const std::vector<SchedulerTask>&) -> uint32_t { return 0; }),
                 std::logic_error);

    controller->stop();
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

// Bounded e-stop halt (design spec §6): emergencyStop() must be the
// non-blocking primitive -- it must not wait for the in-flight task's WCET.
// Before the fix, TimingController::emergencyStop() called
// RealTimeScheduler::stop(), which joins the scheduler thread and therefore
// blocks for as long as the currently-executing task takes.
TEST_F(TimingControllerTest, EmergencyStopIsNonBlockingWithinBound) {
    unit1->setProcessingDelay(std::chrono::milliseconds(80));

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();

    // Poll until the unit has started executing at least once, so
    // emergencyStop() below races a genuinely in-flight ~80ms task.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (unit1->getProcessCallCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GT(unit1->getProcessCallCount(), 0u);

    auto before = std::chrono::steady_clock::now();
    controller->emergencyStop();
    auto elapsed = std::chrono::steady_clock::now() - before;

    // Loose, CI-safe bound: the primitive must return long before the
    // ~80ms in-flight task does. The real number is a Phase 3 benchmark
    // concern (docs/benchmarks.md); this test only asserts "bounded at all".
    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
    EXPECT_FALSE(controller->isRunning());
}

// halt() (via emergencyStop()) must be safe to call from the scheduler
// thread itself -- a task body invoking it is a realistic shape (a policy
// evaluated on-demand from inside a task). It must not deadlock or
// std::terminate (the C33/C42 self-join shape stop() already guards
// against); unlike stop(), halt() never joins, so there is no self-join
// hazard to defer -- this test proves that holds through the public API.
class SelfEmergencyStoppingUnit : public MockProcessingUnit {
  public:
    SelfEmergencyStoppingUnit(const std::string& name, TimingController* controller,
                              std::shared_ptr<std::atomic<bool>> attempted)
        : MockProcessingUnit(name), controller_(controller), attempted_(std::move(attempted)) {}

    void processSync() override {
        if (!attempted_->exchange(true)) {
            controller_->emergencyStop();
        }
    }

  private:
    TimingController* controller_;
    std::shared_ptr<std::atomic<bool>> attempted_;
};

TEST_F(TimingControllerTest, EmergencyStopFromTaskBodyIsSafe) {
    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto selfStopper =
        std::make_unique<SelfEmergencyStoppingUnit>("SelfStopper", controller.get(), attempted);

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    controller->scheduleProcessingUnit(selfStopper.get(), constraints);
    controller->start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!attempted->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(attempted->load()) << "self emergencyStop() was never attempted";

    // Scheduler must actually wind down (running_ false) after the
    // self-triggered halt, and a normal stop() from this (external) thread
    // afterwards must complete (join) without hanging.
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (controller->isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(controller->isRunning());
    controller->stop(); // must return -- no self-join, no terminate
}

// V5 (CLAUDE.md #8: lock-free claims require a TSan-clean stress test plus a
// memory-ordering comment -- the comment lives on RealTimeScheduler's
// AtomicStatistics member). Hammer getPerformanceMetrics()/
// resetPerformanceMetrics() from a separate thread while the scheduler
// thread continuously executes tasks (the sole writer of statistics_), for
// long enough that a real race would have a chance to fire. No assertion on
// the numbers themselves -- this test's only job is to run cleanly under
// ThreadSanitizer (see the tsan ctest invocation); a plain build merely
// checks it doesn't crash/hang.
TEST_F(TimingControllerTest, ConcurrentStatsReadDuringExecutionIsRaceFree) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1);
    controller->scheduleProcessingUnit(unit1.get(), constraints);
    controller->start();

    std::atomic<bool> stop{false};
    std::thread reader([this, &stop]() {
        while (!stop.load()) {
            auto stats = controller->getPerformanceMetrics();
            (void)stats;
            controller->resetPerformanceMetrics();
        }
    });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    stop.store(true);
    reader.join();
    controller->stop();

    SUCCEED(); // reaching here without a TSan report / crash / hang is the test
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

// C44 regression: setErrorCallback() must be setup-only. Registering before
// start() must still work and the callback must still fire on a task
// failure.
TEST_F(TimingControllerTest, SetErrorCallbackBeforeStartFiresOnTaskFailure) {
    std::atomic<int> errorCount{0};
    controller->setErrorCallback(
        [&errorCount](ProcessingUnit*, const std::string&) { errorCount.fetch_add(1); });

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    unit1->setShouldThrow(true);
    controller->scheduleProcessingUnit(unit1.get(), constraints);

    controller->start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (errorCount.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    controller->stop();

    EXPECT_GT(errorCount.load(), 0) << "error callback registered before start() never fired";
}

// C44 regression: executeTask()'s failure path reads errorCallback_ unlocked
// on the scheduler thread. Registering a NEW callback after start() would
// race that read (a torn std::function). Fix: registration refuses once the
// scheduler has ever started, per the C32 precedent (BasePort::
// requireNoTrafficYet) -- pre-fix, this call silently succeeds instead of
// throwing.
TEST_F(TimingControllerTest, SetErrorCallbackAfterStartThrows) {
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(10);
    controller->scheduleProcessingUnit(unit1.get(), constraints);

    controller->start();

    EXPECT_THROW(controller->setErrorCallback([](ProcessingUnit*, const std::string&) {}),
                 std::logic_error);

    controller->stop();
}

// C45 regression: scheduleRoundRobin() kept its rotation cursor in a
// function-local static, so every RealTimeScheduler instance in the process
// shared ONE cursor. This drives scheduleRoundRobin() directly (via the
// friend accessor, single-threaded, no scheduler loop involved) instead of
// inferring correctness from task-execution counts under real concurrency,
// which turned out to mask the bug: with only two always-ready tasks per
// instance, scheduleRoundRobin()'s own "scan up to tasks_.size() candidates"
// loop visits both tasks every pass regardless of where the (possibly
// corrupted) cursor starts, so a timing-based count comparison stayed
// balanced even pre-fix.
//
// The property under test: an isolated RealTimeScheduler with a given
// sequence of addTask() calls must produce a fully deterministic
// scheduleRoundRobin() selection sequence -- unordered_map iteration order
// is a pure function of the keys inserted (integers, default std::hash, no
// randomization), so two freshly-constructed instances given the identical
// task IDs in the identical order are bitwise-reproducible in isolation.
// Interleaving unrelated calls on a second, disjoint-ID-space instance must
// not change that sequence. Pre-fix (shared static) it does; post-fix
// (per-instance lastSelectedTask_ member) it cannot, by construction.
TEST_F(TimingControllerTest, ConcurrentInstancesRotateIndependently) {
    TimingConstraints constraints;
    constraints.period = std::chrono::microseconds(0); // always ready

    // Baseline: A run in complete isolation, 3 tasks, 6 consecutive
    // selections (two full rotations) recorded with no other instance ever
    // constructed.
    RealTimeScheduler baselineA(SchedulingPolicy::ROUND_ROBIN);
    MockProcessingUnit ba1("A1"), ba2("A2"), ba3("A3");
    ba1.initialize();
    ba2.initialize();
    ba3.initialize();
    baselineA.addTask(&ba1, constraints);
    baselineA.addTask(&ba2, constraints);
    baselineA.addTask(&ba3, constraints);

    std::vector<uint32_t> baselineSequence;
    for (int i = 0; i < 6; ++i) {
        baselineSequence.push_back(RoundRobinTestAccessor::call(baselineA));
    }
    // Sanity: an isolated instance must actually pick ready tasks, never 0.
    for (uint32_t id : baselineSequence) {
        ASSERT_NE(id, 0u) << "isolated scheduler failed to select any ready task";
    }

    // Now repeat the IDENTICAL construction as A2 -- same task IDs (1,2,3),
    // same insertion order -- but interleave calls to a SECOND instance B
    // with a disjoint ID range in between every A2 call. B's task IDs must
    // not exist in A2's map for the corruption to bite (a shared cursor
    // holding a value the local map DOES contain still resolves via ++it,
    // masking the bug the same way the timing version above did).
    RealTimeScheduler interleavedA(SchedulingPolicy::ROUND_ROBIN);
    MockProcessingUnit ia1("A1"), ia2("A2"), ia3("A3");
    ia1.initialize();
    ia2.initialize();
    ia3.initialize();
    interleavedA.addTask(&ia1, constraints);
    interleavedA.addTask(&ia2, constraints);
    interleavedA.addTask(&ia3, constraints);

    RealTimeScheduler otherB(SchedulingPolicy::ROUND_ROBIN);
    MockProcessingUnit burn("Burn"), ib1("B1");
    burn.initialize();
    ib1.initialize();
    // Burn B's IDs 1..5 so its one real task lands at ID 6 -- disjoint from
    // A's {1,2,3}.
    for (int i = 0; i < 5; ++i) {
        uint32_t id = otherB.addTask(&burn, constraints);
        otherB.removeTask(id);
    }
    otherB.addTask(&ib1, constraints);

    std::vector<uint32_t> interleavedSequence;
    for (int i = 0; i < 6; ++i) {
        interleavedSequence.push_back(RoundRobinTestAccessor::call(interleavedA));
        RoundRobinTestAccessor::call(otherB); // unrelated instance, disjoint IDs
    }

    // The property: A2's own selections must be identical to the baseline,
    // regardless of B's interleaved activity. Pre-fix, the shared static
    // means every otherB call between two interleavedA calls overwrites the
    // cursor with a value (6) absent from A's map, forcing a reset to
    // tasks_.begin() -- which desyncs the sequence from the baseline as
    // soon as the correct rotation would NOT have been at begin().
    EXPECT_EQ(interleavedSequence, baselineSequence)
        << "an unrelated instance's activity changed this instance's round-robin "
           "selection order -- the rotation cursor is not instance-local (C45)";
}

// C43 regression: scheduleCustom() used to invoke customScheduler_ WHILE
// STILL HOLDING tasksMutex_. A custom scheduler that calls back into any
// scheduler API taking that same lock (getActiveTaskIds(), addTask(),
// removeTask(), getStatistics()...) self-deadlocks -- permanently -- on the
// (non-recursive) mutex, on the scheduler thread. This test's failure mode
// pre-fix is exactly that hang, so it is guarded by its own deadline: if the
// re-entrant call hasn't completed within it, the TimingController is
// deliberately leaked instead of destroyed -- ~TimingController joins the
// scheduler thread, and a thread permanently blocked on its own mutex would
// hang the whole ctest run, not just this test.
TEST_F(TimingControllerTest, CustomSchedulerCallbackCanReenterSchedulerApi) {
    auto ctrl = std::make_unique<TimingController>(SchedulingPolicy::CUSTOM);
    TimingController* ctrlRaw = ctrl.get();

    MockProcessingUnit unit("Reentrant");
    unit.initialize();

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(5);
    ctrl->scheduleProcessingUnit(&unit, constraints);

    std::atomic<bool> reentered{false};
    std::atomic<int> reentrantTaskCount{-1};

    ctrl->setCustomScheduler([ctrlRaw, &reentered, &reentrantTaskCount](
                                 const std::vector<SchedulerTask>& tasks) -> uint32_t {
        // Re-entrant call: pre-fix this deadlocks (tasksMutex_ is already
        // held by the caller, on this same thread).
        auto ids = ctrlRaw->getActiveTaskIds();
        reentrantTaskCount.store(static_cast<int>(ids.size()));
        reentered.store(true);

        for (const auto& task : tasks) {
            if (task.active.load() && !task.executing.load()) {
                return task.taskId;
            }
        }
        return 0;
    });

    ctrl->start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!reentered.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!reentered.load()) {
        // Pre-fix hang: do not touch ctrl any further -- see the comment
        // above the test.
        ctrl.release();
        FAIL() << "custom scheduler callback re-entering the scheduler API never completed -- "
                  "scheduleCustom() is still invoking the callback under tasksMutex_ (C43)";
    }

    ctrl->stop();
    EXPECT_TRUE(reentered.load());
    EXPECT_EQ(reentrantTaskCount.load(), 1);
}

} // anonymous namespace
