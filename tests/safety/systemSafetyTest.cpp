#include <atomic>
#include <axonvex_core/system.hpp>
#include <axonvex_safety/safetyManager.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace axonvex::core;
using namespace axonvex::safety;

namespace {

class SafetyTestSystem : public AxonVexSystem {
  public:
    explicit SafetyTestSystem(const SystemConfiguration& config) : AxonVexSystem(config) {}

  protected:
    bool initializeBlocksLayout() override {
        return true;
    }
};

SystemConfiguration makeTestConfig() {
    SystemConfiguration config;
    config.systemName = "SafetyTestSystem";
    config.enableFileLogging = false;
    config.enableAutoRecovery = false;
    return config;
}

// Regression test for C2: a SafetyManager e-stop must actually halt the system.
// Before the fix, the system stored the manager pointer and never read it.
TEST(SystemSafetyTest, EmergencyStopHaltsSystem) {
    // Declared before the system: the hook must outlive it (see setSafetyHook)
    SafetyManager manager;
    auto system = std::unique_ptr<SafetyTestSystem>(new SafetyTestSystem(makeTestConfig()));

    system->setSafetyHook(&manager);
    ASSERT_TRUE(system->initialize());
    ASSERT_TRUE(system->start());
    ASSERT_TRUE(system->isRunning());

    manager.triggerEmergencyStop("test e-stop");

    // Poll with timeout: the e-stop callback shuts the system down
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (system->isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(system->isRunning()) << "E-stop did not halt the system (C2)";
    EXPECT_EQ(system->getState(), SystemState::FATAL_ERROR);
    EXPECT_TRUE(manager.isEmergencyStopped());
}

// Regression test for C2's self-join guard: an e-stop triggered from INSIDE a
// system-owned thread (health callback on the monitoring thread) must halt the
// system without deadlocking or terminating on a self-join.
TEST(SystemSafetyTest, EmergencyStopFromSystemThreadIsSafe) {
    SafetyManager manager;
    auto config = makeTestConfig();
    config.healthCheckInterval = std::chrono::seconds(1);
    auto system = std::unique_ptr<SafetyTestSystem>(new SafetyTestSystem(config));

    system->setSafetyHook(&manager);
    ASSERT_TRUE(system->initialize());

    SafetyManager* managerPtr = &manager;
    system->registerHealthCheckCallback([managerPtr]() -> SystemHealth {
        // Runs on the monitoring thread; the e-stop callback then calls
        // emergencyShutdown on that same thread (self-join guard path)
        managerPtr->triggerEmergencyStop("triggered from monitoring thread");
        SystemHealth h;
        h.overallStatus = SystemHealth::Status::HEALTHY;
        return h;
    });

    ASSERT_TRUE(system->start());

    // The monitoring thread fires the health check after healthCheckInterval
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (system->isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_FALSE(system->isRunning()) << "Self-triggered e-stop did not halt the system";
    EXPECT_EQ(system->getState(), SystemState::FATAL_ERROR);

    // Destruction joins the monitoring thread the guard skipped
    system.reset();
    EXPECT_TRUE(manager.isEmergencyStopped());
}

// A ProcessingUnit that counts every processSync() call, for the halt-bound
// test below. The counter is externally owned (shared_ptr) rather than a
// member: emergencyShutdown() destroys every registered ProcessingUnit
// synchronously (processingUnits_.clear()), so a raw pointer into the unit
// is not valid to read after triggerEmergencyStop() returns -- the counter
// must outlive that destruction for the test to observe its final value.
class CountingUnit : public ProcessingUnit {
  public:
    explicit CountingUnit(std::shared_ptr<std::atomic<uint32_t>> count)
        : ProcessingUnit("CountingUnit"), count_(std::move(count)) {}

    void processSync() override {
        count_->fetch_add(1, std::memory_order_relaxed);
    }
    void processAsync() override {}
    void reset() override {
        count_->store(0, std::memory_order_relaxed);
    }
    void initialize() override {}
    std::string getTypeDescription() override {
        return "CountingUnit";
    }

  private:
    std::shared_ptr<std::atomic<uint32_t>> count_;
};

// Design spec §6.4: "halted within a bound" -- after an e-stop, the
// scheduler dispatches no FURTHER tasks; at most the one already in-flight
// task completes. AxonVexSystem's emergency path (SafetyHook callback ->
// emergencyShutdown() -> timingController_->stop()) already routes through
// RealTimeScheduler::stop(), which is now halt() (immediate, non-blocking
// running_=false) followed by the join -- so the "stop dispatching" moment
// happens at halt() time, not at join completion. This test proves that
// holds at the system level: task executions actually stop, not just that
// isRunning() eventually flips.
TEST(SystemSafetyTest, EmergencyStopHaltsSchedulerWithinBound) {
    SafetyManager manager;
    auto system = std::unique_ptr<SafetyTestSystem>(new SafetyTestSystem(makeTestConfig()));
    system->setSafetyHook(&manager);
    ASSERT_TRUE(system->initialize());

    auto count = std::make_shared<std::atomic<uint32_t>>(0);
    auto unit = std::unique_ptr<CountingUnit>(new CountingUnit(count));
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1);
    ASSERT_NE(system->registerProcessingUnit(std::move(unit), constraints), 0u);

    ASSERT_TRUE(system->start());

    // Wait until it has actually executed at least once.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (count->load(std::memory_order_relaxed) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GT(count->load(std::memory_order_relaxed), 0u);

    manager.triggerEmergencyStop("bounded halt test");

    // Poll for the state transition (the e-stop callback shuts the system
    // down asynchronously from the safety evaluation call). This also
    // synchronously destroys the registered CountingUnit (emergencyShutdown()
    // clears processingUnits_) -- `count` is a shared_ptr the unit merely
    // wrote into, so it stays valid and readable afterward.
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (system->isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_FALSE(system->isRunning());
    EXPECT_EQ(system->getState(), SystemState::FATAL_ERROR);
    EXPECT_TRUE(manager.isEmergencyStopped());

    // The real bound: executions actually stop. Sample the counter, wait
    // several task periods (1ms period -- 50ms is generously many periods),
    // and confirm it did not keep climbing -- this is the failure mode a
    // zombie scheduler (still dispatching after the "stop") would show.
    uint32_t countAfterStop = count->load(std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count->load(std::memory_order_relaxed), countAfterStop)
        << "scheduler kept dispatching tasks after the e-stop halted the system";
}

// The hook getter reports the registered hook; clearing works.
TEST(SystemSafetyTest, SafetyHookRegistration) {
    SafetyManager manager;
    auto system = std::unique_ptr<SafetyTestSystem>(new SafetyTestSystem(makeTestConfig()));

    EXPECT_EQ(system->getSafetyHook(), nullptr);
    system->setSafetyHook(&manager);
    EXPECT_EQ(system->getSafetyHook(), &manager);
    system->setSafetyHook(nullptr);
    EXPECT_EQ(system->getSafetyHook(), nullptr);
}

} // namespace
