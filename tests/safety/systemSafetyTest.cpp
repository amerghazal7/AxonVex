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
