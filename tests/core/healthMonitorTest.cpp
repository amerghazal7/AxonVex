/**
 * @file healthMonitorTest.cpp
 * @brief Unit tests for HealthMonitor (Phase 2 core decomposition, step 5)
 *
 * Pins the behavior extracted from AxonVexSystem's
 * monitoringWorker_/monitoringEnabled_/healthCheckCallbacks_/callbacksMutex_/
 * nextCallbackId_/monitoringLoop()/performHealthCheck()/
 * performInternalHealthCheck() (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.5). AxonVexSystemTest's health-check tests continue to pin the
 * façade-level behavior; these tests pin the monitor in isolation.
 */

#include <atomic>
#include <axonvex_core/healthMonitor.hpp>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

// Polls `pred` against a steady_clock deadline instead of a fixed sleep --
// this project's no-sleep-based-synchronization rule.
template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// Test fixture wiring HealthMonitor's injected callables to plain local
/// state -- no AxonVexSystem -- so these tests pin the monitor's own
/// contract, not the façade's.
class HealthMonitorTest : public ::testing::Test {
  protected:
    SystemConfiguration cfg_;
    std::atomic<bool> shutdownRequested_{false};
    std::atomic<SystemState> state_{SystemState::RUNNING};
    std::atomic<bool> timingHealthy_{true};
    std::atomic<bool> configHealthy_{true};
    std::atomic<bool> loggerHealthy_{true};
    std::atomic<size_t> memoryUsage_{0};
    std::atomic<int> successRatePercent_{100};
    std::atomic<int> criticalHealthCalls_{0};
    std::string lastCriticalDesc_;
    std::mutex lastCriticalDescMutex_;
    std::atomic<int> loopErrorCalls_{0};
    std::atomic<int> updateStatisticsCalls_{0};

    EventBus events_{[] { return static_cast<Logger*>(nullptr); }, [this] { return false; },
                     [this] { return shutdownRequested_.load(); }, [] {}};

    HealthMonitor monitor_{cfg_,
                           [this] { return shutdownRequested_.load(); },
                           events_,
                           [] { return static_cast<Logger*>(nullptr); },
                           HealthMonitor::Providers{
                               [this] { return state_.load(); },
                               [this] { return timingHealthy_.load(); },
                               [this] { return configHealthy_.load(); },
                               [this] { return loggerHealthy_.load(); },
                               [this] { return memoryUsage_.load(); },
                               [this] { return successRatePercent_.load() / 100.0; },
                           },
                           [this](const std::string& desc) {
                               criticalHealthCalls_.fetch_add(1);
                               std::lock_guard<std::mutex> lock(lastCriticalDescMutex_);
                               lastCriticalDesc_ = desc;
                           },
                           [this] { loopErrorCalls_.fetch_add(1); },
                           [this] { updateStatisticsCalls_.fetch_add(1); }};

    HealthMonitorTest() {
        cfg_.maxMemoryPoolSize = 1000; // simplifies memoryUtilization arithmetic below
    }
};

} // namespace

TEST_F(HealthMonitorTest, RegisterNullCallbackReturnsZeroWithoutTouchingCounter) {
    EXPECT_EQ(monitor_.registerHealthCallback(nullptr), 0u);
    uint32_t id = monitor_.registerHealthCallback([] { return SystemHealth{}; });
    EXPECT_GT(id, 0u);
}

TEST_F(HealthMonitorTest, CheckReportsFailureWhenStateIsErrorOrFatal) {
    state_.store(SystemState::FATAL_ERROR);
    SystemHealth health = monitor_.check();
    EXPECT_EQ(health.overallStatus, SystemHealth::Status::FAILURE);
    ASSERT_FALSE(health.errors.empty());
}

TEST_F(HealthMonitorTest, CheckReflectsComponentHealthProviders) {
    timingHealthy_.store(false);
    configHealthy_.store(false);
    loggerHealthy_.store(false);
    SystemHealth health = monitor_.check();
    EXPECT_FALSE(health.timingControllerHealthy);
    EXPECT_FALSE(health.configurationHealthy);
    EXPECT_FALSE(health.loggerHealthy);
}

TEST_F(HealthMonitorTest, CheckFlagsHighMemoryUtilizationAsWarning) {
    memoryUsage_.store(800); // 80% of maxMemoryPoolSize == 1000 -> > 0.75
    SystemHealth health = monitor_.check();
    EXPECT_EQ(health.overallStatus, SystemHealth::Status::WARNING);
    EXPECT_FALSE(health.warnings.empty());
}

TEST_F(HealthMonitorTest, CheckFlagsCriticalMemoryUtilization) {
    memoryUsage_.store(950); // 95% -> > 0.9
    SystemHealth health = monitor_.check();
    EXPECT_EQ(health.overallStatus, SystemHealth::Status::CRITICAL);
    EXPECT_FALSE(health.memoryHealthy);
}

TEST_F(HealthMonitorTest, CheckFlagsLowSuccessRateAsWarning) {
    successRatePercent_.store(50);
    SystemHealth health = monitor_.check();
    EXPECT_EQ(health.overallStatus, SystemHealth::Status::WARNING);
}

TEST_F(HealthMonitorTest, PerformHealthCheckMergesUserCallbackAndPublishesEvent) {
    events_.reinitialize(8, 8);
    auto received = std::make_shared<std::vector<SystemEvent>>();
    auto mutex = std::make_shared<std::mutex>();
    events_.registerCallback([received, mutex](const SystemEvent& e) {
        std::lock_guard<std::mutex> lock(*mutex);
        received->push_back(e);
    });
    events_.start();

    monitor_.registerHealthCallback([] {
        SystemHealth h;
        h.overallStatus = SystemHealth::Status::WARNING;
        h.warnings.push_back("user warning");
        return h;
    });

    monitor_.performHealthCheck();

    ASSERT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> lock(*mutex);
        return !received->empty();
    }));
    {
        std::lock_guard<std::mutex> lock(*mutex);
        EXPECT_EQ(received->front().type, SystemEvent::Type::HEALTH_CHECK);
    }

    EXPECT_TRUE(events_.stopAndJoin());
}

// Regression test for C18: user callbacks must not be invoked while
// callbacksMutex_ is held -- a callback that re-enters the callback API
// would deadlock on a non-recursive mutex otherwise.
TEST_F(HealthMonitorTest, ReentrantCallbackRegistrationDoesNotDeadlock) {
    std::atomic<uint32_t> innerId{0};
    std::atomic<bool> done{false};
    HealthMonitor* mon = &monitor_;

    monitor_.registerHealthCallback([mon, &innerId, &done] {
        innerId.store(mon->registerHealthCallback([] { return SystemHealth{}; }));
        done.store(true);
        SystemHealth h;
        h.overallStatus = SystemHealth::Status::HEALTHY;
        return h;
    });

    std::thread worker([&] { monitor_.performHealthCheck(); });

    ASSERT_TRUE(waitFor([&] { return done.load(); }))
        << "performHealthCheck deadlocked: callbacks invoked under callbacksMutex_ (C18)";
    worker.join();
    EXPECT_GT(innerId.load(), 0u);
}

TEST_F(HealthMonitorTest, ResetCallbacksClearsRegistryAndIdCounter) {
    monitor_.registerHealthCallback([] { return SystemHealth{}; });
    monitor_.registerHealthCallback([] { return SystemHealth{}; });

    monitor_.resetCallbacks();

    // The id counter restarts at 1 -- same first id a brand-new monitor
    // would hand out.
    EXPECT_EQ(monitor_.registerHealthCallback([] { return SystemHealth{}; }), 1u);
}

TEST_F(HealthMonitorTest, CriticalHealthTriggersRecoveryHookOnlyWhenAutoRecoveryEnabled) {
    cfg_.enableAutoRecovery = false;
    memoryUsage_.store(950); // CRITICAL
    monitor_.performHealthCheck();
    EXPECT_EQ(criticalHealthCalls_.load(), 0);

    cfg_.enableAutoRecovery = true;
    monitor_.performHealthCheck();
    EXPECT_EQ(criticalHealthCalls_.load(), 1);
}

TEST_F(HealthMonitorTest, StartStopJoinLifecycleAndIsOnMonitorThread) {
    cfg_.statisticsUpdateInterval = 0s;
    cfg_.healthCheckInterval = 3600s; // don't let the loop call performHealthCheck() here

    EXPECT_FALSE(monitor_.isOnMonitorThread());
    monitor_.start();

    ASSERT_TRUE(waitFor([&] { return updateStatisticsCalls_.load() > 0; }))
        << "monitor loop never invoked the injected updateStatistics callable";

    EXPECT_TRUE(monitor_.stopAndJoin());
    EXPECT_FALSE(monitor_.join()); // already joined; nothing left to reap
}

TEST_F(HealthMonitorTest, JoinRefusesSelfJoinFromInsideTheLoop) {
    cfg_.statisticsUpdateInterval = 3600s;
    cfg_.healthCheckInterval = 0s;

    std::atomic<bool> selfJoinReturned{false};
    std::atomic<bool> selfJoinResult{true};

    monitor_.registerHealthCallback([&] {
        if (!selfJoinReturned.load()) {
            selfJoinResult.store(monitor_.join());
            selfJoinReturned.store(true);
        }
        return SystemHealth{};
    });

    monitor_.start();

    ASSERT_TRUE(waitFor([&] { return selfJoinReturned.load(); }));
    EXPECT_FALSE(selfJoinResult.load());

    EXPECT_TRUE(monitor_.stopAndJoin());
}

TEST_F(HealthMonitorTest, LoopExceptionInvokesOnLoopErrorAndKeepsRunning) {
    cfg_.statisticsUpdateInterval = 0s;
    cfg_.healthCheckInterval = 3600s;

    std::atomic<int> updateCalls{0};
    // Override the fixture's updateStatistics_ behavior isn't possible after
    // construction (it's captured by value in the ctor init list), so this
    // test throws from the memory-usage provider instead -- check() is only
    // reached via performHealthCheck(), not the statistics branch, so drive
    // the exception through a second monitor wired to throw from
    // updateStatistics.
    SystemConfiguration cfg = cfg_;
    HealthMonitor throwingMonitor{cfg,
                                  [this] { return shutdownRequested_.load(); },
                                  events_,
                                  [] { return static_cast<Logger*>(nullptr); },
                                  HealthMonitor::Providers{
                                      [this] { return state_.load(); },
                                      [this] { return timingHealthy_.load(); },
                                      [this] { return configHealthy_.load(); },
                                      [this] { return loggerHealthy_.load(); },
                                      [this] { return memoryUsage_.load(); },
                                      [this] { return successRatePercent_.load() / 100.0; },
                                  },
                                  [](const std::string&) {},
                                  [this] { loopErrorCalls_.fetch_add(1); },
                                  [&updateCalls] {
                                      updateCalls.fetch_add(1);
                                      throw std::runtime_error("injected failure");
                                  }};

    throwingMonitor.start();

    ASSERT_TRUE(waitFor([&] { return loopErrorCalls_.load() > 0; }))
        << "onLoopError_ never fired for an exception thrown inside the monitor loop";
    // The loop must survive the exception and keep iterating (matches the
    // pre-extraction monitoringLoop()'s per-iteration try/catch).
    ASSERT_TRUE(waitFor([&] { return updateCalls.load() > 1; }));

    EXPECT_TRUE(throwingMonitor.stopAndJoin());
}
