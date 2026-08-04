#include <atomic>
#include <axonvex_core/system.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

// Mock ProcessingUnit for testing
class MockProcessingUnit : public ProcessingUnit {
  private:
    axonvex::core::OutputPort<double>* outputPort_;
    axonvex::core::InputPort<double>* inputPort_;

  public:
    explicit MockProcessingUnit(const std::string& name) : ProcessingUnit(name) {
        // Create ports that system port tests expect
        outputPort_ = createOutputPort<double>(1000, "test_output");
        inputPort_ = createInputPort<double>(1001, "test_input");
    }

    void initialize() override {
        initializeCallCount_++;
        setState(ExecutionState::INITIALIZED);
    }

    void processSync() override {
        auto start = std::chrono::steady_clock::now();
        processCallCount_.fetch_add(1);
        setState(ExecutionState::RUNNING);

        if (shouldThrow_) {
            throw std::runtime_error("Mock processing error");
        }

        std::this_thread::sleep_for(processingTime_);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        updateSyncExecutionStats(duration);
    }

    void processAsync() override {
        auto start = std::chrono::steady_clock::now();
        processCallCount_.fetch_add(1);

        if (shouldThrow_) {
            throw std::runtime_error("Mock processing error");
        }

        std::this_thread::sleep_for(processingTime_);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        updateAsyncExecutionStats(duration);
    }

    void reset() override {
        resetCallCount_++;
        setState(ExecutionState::INITIALIZED);
    }

    void finalize() override {
        finalizeCallCount_++;
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "MockProcessingUnit";
    }

    // Test helpers
    void setShouldThrow(bool shouldThrow) {
        shouldThrow_ = shouldThrow;
    }
    void setProcessingTime(std::chrono::microseconds time) {
        processingTime_ = time;
    }

    int getProcessCallCount() const {
        return processCallCount_.load();
    }
    int getInitializeCallCount() const {
        return initializeCallCount_;
    }
    int getResetCallCount() const {
        return resetCallCount_;
    }
    int getFinalizeCallCount() const {
        return finalizeCallCount_;
    }

  private:
    std::atomic<int> processCallCount_{0};
    int initializeCallCount_{0};
    int resetCallCount_{0};
    int finalizeCallCount_{0};
    bool shouldThrow_{false};
    std::chrono::microseconds processingTime_{100};
};

/**
 * @brief Concrete AxonVexSystem implementation for testing
 */
class TestAxonVexSystem : public AxonVexSystem {
  public:
    explicit TestAxonVexSystem(const SystemConfiguration& config = SystemConfiguration{})
        : AxonVexSystem(config) {}

  protected:
    bool initializeBlocksLayout() override {
        // For basic system tests, we don't need any specific processing units
        // Just return true to indicate successful initialization
        return true;
    }
};

/**
 * @brief Test system that creates MockProcessingUnits for testing
 */
class TestAxonVexSystemWithUnits : public AxonVexSystem {
  private:
    std::unique_ptr<MockProcessingUnit> mockUnit_;

  public:
    explicit TestAxonVexSystemWithUnits(const SystemConfiguration& config = SystemConfiguration{})
        : AxonVexSystem(config) {}

  protected:
    bool initializeBlocksLayout() override {
        try {
            // Create and register a mock processing unit for tests
            mockUnit_ = std::make_unique<MockProcessingUnit>("TestUnit");

            TimingConstraints constraints;
            constraints.period = std::chrono::milliseconds(100);
            constraints.priority = SchedulerPriority::NORMAL;

            registerProcessingUnit(std::move(mockUnit_), constraints);
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Test block layout initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

  public:
    MockProcessingUnit* getTestUnit() const {
        auto units = getAllProcessingUnits();
        if (!units.empty()) {
            return dynamic_cast<MockProcessingUnit*>(units[0]);
        }
        return nullptr;
    }
};

// Google Test fixture for AxonVexSystem tests
class AxonVexSystemTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a test configuration
        config_.systemName = "TestSystem";
        config_.version = "1.0.0";
        config_.logLevel = LogLevel::Debug;
        config_.enablePerformanceMonitoring = true;
        config_.enableAutoRecovery = true;
        config_.maxRecoveryAttempts = 2;
        config_.statisticsUpdateInterval = std::chrono::seconds(1);
        config_.healthCheckInterval = std::chrono::seconds(2);
        config_.enableFileLogging = false; // Disable file logging for tests

        // Create system with test configuration
        system_ = std::make_unique<TestAxonVexSystem>(config_);
    }

    void TearDown() override {
        // Reset the system immediately to prevent lingering threads
        system_.reset();

        // Give a moment for all threads to properly shut down
        std::this_thread::sleep_for(100ms);
    }

    SystemConfiguration config_;
    std::unique_ptr<TestAxonVexSystem> system_;
};

// =================================================================
// BASIC SYSTEM LIFECYCLE TESTS
// =================================================================

TEST_F(AxonVexSystemTest, Construction) {
    EXPECT_EQ(system_->getState(), SystemState::UNINITIALIZED);
    EXPECT_FALSE(system_->isRunning());
    EXPECT_TRUE(system_->isHealthy());
    EXPECT_EQ(system_->getProcessingUnitCount(), 0);

    const auto& stats = system_->getStatistics();
    EXPECT_EQ(stats.totalProcessingUnits.load(), 0);
    EXPECT_EQ(stats.activeProcessingUnits.load(), 0);
}

TEST_F(AxonVexSystemTest, InitializationAndStartup) {
    // Test initialization
    EXPECT_TRUE(system_->initialize());
    EXPECT_EQ(system_->getState(), SystemState::INITIALIZED);
    EXPECT_FALSE(system_->isRunning());

    // Test startup
    EXPECT_TRUE(system_->start());
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);
    EXPECT_TRUE(system_->isRunning());

    // Test shutdown
    EXPECT_TRUE(system_->stop());
    EXPECT_EQ(system_->getState(), SystemState::STOPPED);
    EXPECT_FALSE(system_->isRunning());
}

TEST_F(AxonVexSystemTest, InitializationFailure) {
    // Test with invalid configuration
    SystemConfiguration invalidConfig;
    invalidConfig.systemName = ""; // Invalid empty name

    EXPECT_THROW({ TestAxonVexSystem invalidSystem(invalidConfig); }, std::invalid_argument);
}

TEST_F(AxonVexSystemTest, PauseAndResume) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);

    // Test pause
    EXPECT_TRUE(system_->pause());
    EXPECT_EQ(system_->getState(), SystemState::PAUSED);
    EXPECT_FALSE(system_->isRunning());

    // Test resume
    EXPECT_TRUE(system_->resume());
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);
    EXPECT_TRUE(system_->isRunning());

    EXPECT_TRUE(system_->stop());
}

TEST_F(AxonVexSystemTest, EmergencyShutdown) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);

    // Trigger emergency shutdown
    system_->emergencyShutdown();
    EXPECT_EQ(system_->getState(), SystemState::FATAL_ERROR);
    EXPECT_FALSE(system_->isRunning());
}

TEST_F(AxonVexSystemTest, SystemReset) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    // Reset the system
    system_->reset();
    EXPECT_EQ(system_->getState(), SystemState::UNINITIALIZED);
    EXPECT_FALSE(system_->isRunning());
    EXPECT_EQ(system_->getProcessingUnitCount(), 0);
}

// C39 guard: an e-stop initiated FROM an event callback runs emergencyShutdown
// on the event thread itself, whose self-join guard (C33/C36) leaves the
// thread handle set. Re-initialization must join-and-clear stale handles
// before assigning new threads over them (~std::thread on a joinable thread
// is std::terminate). The throw-mid-initialize shape of C39 has no
// deterministic seam; this locks the nearest reachable lifecycle.
TEST_F(AxonVexSystemTest, ReinitializeAfterCallbackInitiatedEmergencyShutdown) {
    EXPECT_TRUE(system_->initialize());

    auto fired = std::make_shared<std::atomic<bool>>(false);
    AxonVexSystem* sys = system_.get();
    system_->registerEventCallback([sys, fired](const SystemEvent&) {
        if (!fired->exchange(true)) {
            sys->emergencyShutdown(); // runs on the event thread
        }
    });
    // STATE_CHANGE events (published under start()'s own transitionState calls)
    // trigger the callback asynchronously on the event thread, racing this
    // thread's own transitionState(RUNNING): the e-stop may land before or
    // after start() reaches RUNNING, so start()'s return value is not
    // deterministic here — only that FATAL_ERROR is eventually reached
    // (asserted below) matters for this guard.
    system_->start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (system_->getState() != SystemState::FATAL_ERROR &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(system_->getState(), SystemState::FATAL_ERROR);

    system_->reset();
    EXPECT_TRUE(system_->initialize()); // must not terminate on a stale handle
    EXPECT_TRUE(system_->start());
    EXPECT_TRUE(system_->stop());
}

// C41 regression: initialize() called from an event callback runs ON the
// event-processing thread. Pre-fix, the C39 helper detached the self-handle
// and initializeComponents() then replaced eventPool_/eventQueue_/logger_
// under the detached loop's live stack — a cross-pool free and a duplicate
// queue consumer (pre-C39 this was a std::terminate; the detach made it
// silent). initialize() now refuses on a worker thread.
TEST_F(AxonVexSystemTest, InitializeFromEventCallbackIsRefused) {
    EXPECT_TRUE(system_->initialize());

    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<std::atomic<bool>>(true);
    AxonVexSystem* sys = system_.get();
    system_->registerEventCallback([sys, attempted, result, finished](const SystemEvent&) {
        if (!attempted->exchange(true)) {
            result->store(sys->initialize()); // must be refused, not honored
            // Set only after the call returns: the poller below must never
            // observe a "done" signal before result actually holds the
            // outcome, or it can race the store and read a stale `true`.
            finished->store(true);
        }
    });
    EXPECT_TRUE(system_->start()); // STATE_CHANGE events drive the callback

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(finished->load());
    EXPECT_FALSE(result->load()) << "initialize() on the event thread must refuse";
    // The system must still be intact and stoppable from the outside.
    EXPECT_TRUE(system_->stop());
}

// C40 regression: stop() from an event callback self-joined, threw
// resource_deadlock_would_occur into its own catch, and silently escalated a
// graceful stop to FATAL_ERROR. It now refuses up front, before the
// STOPPING transition, so the system stays RUNNING.
TEST_F(AxonVexSystemTest, StopFromEventCallbackIsRefusedWithoutEscalation) {
    EXPECT_TRUE(system_->initialize());

    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    auto stopResult = std::make_shared<std::atomic<bool>>(true);
    AxonVexSystem* sys = system_.get();
    system_->registerEventCallback([sys, attempted, stopResult, finished](const SystemEvent&) {
        if (!attempted->exchange(true)) {
            stopResult->store(sys->stop());
            // Set only after the call returns: see InitializeFromEventCallbackIsRefused.
            finished->store(true);
        }
    });
    EXPECT_TRUE(system_->start());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(finished->load());
    EXPECT_FALSE(stopResult->load());
    // Pre-fix the swallowed self-join escalated to FATAL_ERROR; post-fix the
    // refusal happens before the STOPPING transition, so state stays RUNNING.
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);
    EXPECT_TRUE(system_->stop()); // a real stop from outside still works
}

// C40 regression, reset() variant. reset() from an event callback ran
// emergencyShutdown() + a destructive teardown (timingController_/
// configuration_/logger_.reset()) on the very event thread invoking it — the
// callback's own call stack was using those objects underneath it. It now
// refuses up front and returns without touching anything. reset() returns
// void, so the refusal is observed indirectly: the system must still be
// intact (not UNINITIALIZED) and a subsequent external stop() must succeed.
TEST_F(AxonVexSystemTest, ResetFromEventCallbackIsRefused) {
    EXPECT_TRUE(system_->initialize());

    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto finished = std::make_shared<std::atomic<bool>>(false);
    AxonVexSystem* sys = system_.get();
    system_->registerEventCallback([sys, attempted, finished](const SystemEvent&) {
        if (!attempted->exchange(true)) {
            sys->reset();
            // Set only after reset() returns: see
            // InitializeFromEventCallbackIsRefused. Without this, polling on
            // `attempted` lets the main thread race ahead of reset() itself
            // (which returns void, so there is no result to observe) and the
            // post-assertions below could run before reset() ever executes.
            finished->store(true);
        }
    });
    EXPECT_TRUE(system_->start());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(finished->load());
    EXPECT_NE(system_->getState(), SystemState::UNINITIALIZED);
    EXPECT_TRUE(system_->stop());
}

// C7 regression: emergencyShutdown/reset used to write currentState_ directly,
// bypassing transitionState — no validation, no statistics, no STATE_CHANGE
// event. The transition counter is the observable: a bypassed store leaves it
// unchanged.
TEST_F(AxonVexSystemTest, EmergencyShutdownIsAValidatedStateTransition) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    const uint64_t before = system_->getStatistics().totalStateTransitions.load();
    system_->emergencyShutdown();

    EXPECT_EQ(system_->getState(), SystemState::FATAL_ERROR);
    EXPECT_EQ(system_->getStatistics().totalStateTransitions.load(), before + 1);
}

// C7 regression, reset() variant. reset() = emergencyShutdown() (→ FATAL_ERROR,
// +1 transition), a wait for in-flight teardown, then transitionState(→
// UNINITIALIZED) (+1 more) — but reset()'s own statistics_.reset() call runs
// immediately after that second transition and zeroes totalStateTransitions
// back to 0 as part of its documented job (see SystemStatistics::reset()).
// Reading the counter after reset() returns is therefore 0 either way and
// can't distinguish the fix from the bypass it replaces. Run reset() on a
// background thread and poll for the counter moving off `before` instead.
//
// C40 removed reset()'s sleep_for(100ms) guess in favor of blocking on
// shutdownMutex_ — on this system (no concurrent teardown owner) that wait is
// near-instant, so the discriminating window is no longer a fabricated
// delay. It is real anyway: this test's reset() call runs on a plain
// std::thread (not a system worker), so emergencyShutdown() actually joins
// eventProcessingThread_ and monitoringThread_ here rather than skipping via
// the self-join guard — and each of those loops only re-checks its shutdown
// flag once per ~100ms poll cadence (eventProcessingLoop's tryDequeue
// timeout, monitoringLoop's sleep_for). That join time is the window: it
// elapses between the FATAL_ERROR transition (immediate) and the
// UNINITIALIZED transition + statistics_.reset() (after both joins return),
// and it is bounded below by zero only in the measure-zero case where both
// loops happen to be at their top-of-loop check at the exact instant the
// flags flip.
//
// Decision record (whole-branch review, evidence-based): 20 consecutive
// serial runs observed the bump every time, but 6-way parallel load
// (6 processes x 6 repeats = 36 runs) missed it once — the 5ms poll cadence
// against a sub-5ms window is not reliably wide enough under contention, and
// this remains true even after the C40 follow-up fix added extra
// joinAndClearThreadHandle() calls to reset() (those only close the
// deferred-self-join gap; they don't run on this test's plain-thread path,
// where emergencyShutdown() already joins directly, so they don't widen this
// particular window). Per the documented fallback, the sawBump assertion is
// dropped rather than chasing a tighter poll: the invariant it was trying to
// verify (reset() routes through transitionState() instead of bypassing it)
// is already covered without a race by
// EmergencyShutdownIsAValidatedStateTransition, which asserts the FATAL_ERROR
// half of reset()'s path (reset() == emergencyShutdown() + transition to
// UNINITIALIZED) directly and synchronously. This test keeps its
// non-racy post-conditions: reset() actually completes, and the system ends
// up UNINITIALIZED with statistics zeroed.
TEST_F(AxonVexSystemTest, ResetIsAValidatedStateTransition) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    std::atomic<bool> done{false};
    std::thread worker([this, &done]() {
        system_->reset();
        done.store(true);
    });

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }

    if (!done.load()) {
        worker.detach();
    } else {
        worker.join();
    }

    ASSERT_TRUE(done.load()) << "reset() did not complete";
    EXPECT_EQ(system_->getState(), SystemState::UNINITIALIZED);
    // reset()'s statistics_.reset() zeroes the counter — documented behavior,
    // not evidence either way for the C7 fix (see EmergencyShutdownIsAValidatedStateTransition
    // for the assertion that actually exercises it).
    EXPECT_EQ(system_->getStatistics().totalStateTransitions.load(), 0u);
}

// FATAL_ERROR must be reachable from ANY state — an e-stop is always legal.
// Pre-fix the transition table only allowed it from STOPPING.
TEST_F(AxonVexSystemTest, EmergencyShutdownFromInitializedIsValidated) {
    EXPECT_TRUE(system_->initialize());

    const uint64_t before = system_->getStatistics().totalStateTransitions.load();
    system_->emergencyShutdown();

    EXPECT_EQ(system_->getState(), SystemState::FATAL_ERROR);
    EXPECT_EQ(system_->getStatistics().totalStateTransitions.load(), before + 1);
}

// Guard test for the trap the C7 fix opens: e-stop on a NEVER-initialized
// system now reaches publishEvent, whose eventPool_/eventQueue_ are null.
// Passes trivially pre-fix; crashes post-fix if the null guard is missing.
TEST_F(AxonVexSystemTest, EmergencyShutdownOnUninitializedSystemIsSafe) {
    TestAxonVexSystem s;
    s.emergencyShutdown();
    EXPECT_EQ(s.getState(), SystemState::FATAL_ERROR);
}

// =================================================================
// PROCESSING UNIT MANAGEMENT TESTS
// =================================================================

TEST_F(AxonVexSystemTest, ProcessingUnitRegistration) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();

    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    EXPECT_GT(unitId, 0);
    EXPECT_EQ(system_->getProcessingUnitCount(), 1);
    EXPECT_EQ(system_->getProcessingUnit(unitId), unitPtr);

    const auto& stats = system_->getStatistics();
    EXPECT_EQ(stats.totalProcessingUnits.load(), 1);
    EXPECT_EQ(stats.activeProcessingUnits.load(), 1);
}

TEST_F(AxonVexSystemTest, ProcessingUnitUnregistration) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    EXPECT_EQ(system_->getProcessingUnitCount(), 1);

    // Unregister the unit
    EXPECT_TRUE(system_->unregisterProcessingUnit(unitId));
    EXPECT_EQ(system_->getProcessingUnitCount(), 0);
    EXPECT_EQ(system_->getProcessingUnit(unitId), nullptr);

    // Try to unregister non-existent unit
    EXPECT_FALSE(system_->unregisterProcessingUnit(999));
}

TEST_F(AxonVexSystemTest, MultipleProcessingUnits) {
    EXPECT_TRUE(system_->initialize());

    const int numUnits = 5;
    std::vector<uint32_t> unitIds;
    std::vector<MockProcessingUnit*> unitPtrs;

    // Register multiple units
    for (int i = 0; i < numUnits; ++i) {
        auto unit = std::make_unique<MockProcessingUnit>("Unit" + std::to_string(i));
        MockProcessingUnit* unitPtr = unit.get();
        unitPtrs.push_back(unitPtr);

        uint32_t unitId = system_->registerProcessingUnit(std::move(unit));
        unitIds.push_back(unitId);
    }

    EXPECT_EQ(system_->getProcessingUnitCount(), numUnits);

    // Verify all units are registered
    auto allUnits = system_->getAllProcessingUnits();
    EXPECT_EQ(allUnits.size(), numUnits);

    for (int i = 0; i < numUnits; ++i) {
        EXPECT_EQ(system_->getProcessingUnit(unitIds[i]), unitPtrs[i]);
    }
}

TEST_F(AxonVexSystemTest, ProcessingUnitExecution) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    unit->setProcessingTime(std::chrono::microseconds(10));

    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    EXPECT_TRUE(system_->start());

    // Let the system run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check that the unit was executed
    EXPECT_GT(unitPtr->getProcessCallCount(), 0);

    EXPECT_TRUE(system_->stop());
}

TEST_F(AxonVexSystemTest, ProcessingUnitWithConstraints) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(50);
    constraints.deadline = std::chrono::milliseconds(40);
    constraints.priority = SchedulerPriority::HIGH;

    uint32_t unitId = system_->registerProcessingUnit(std::move(unit), constraints);

    EXPECT_GT(unitId, 0);
    EXPECT_EQ(system_->getProcessingUnitCount(), 1);
}

// =================================================================
// CONFIGURATION MANAGEMENT TESTS
// =================================================================

TEST_F(AxonVexSystemTest, SystemConfigurationAccess) {
    const auto& sysConfig = system_->getSystemConfiguration();

    EXPECT_EQ(sysConfig.systemName, "TestSystem");
    EXPECT_EQ(sysConfig.version, "1.0.0");
    EXPECT_EQ(sysConfig.logLevel, LogLevel::Debug);
}

TEST_F(AxonVexSystemTest, SystemConfigurationUpdate) {
    // C25: config is immutable once initialization begins — worker threads
    // read systemConfig_ unlocked, so live updates were a data race.
    SystemConfiguration newConfig = config_;
    newConfig.systemName = "UpdatedSystem";
    newConfig.logLevel = LogLevel::Warning;

    EXPECT_TRUE(system_->updateSystemConfiguration(newConfig));

    const auto& updatedConfig = system_->getSystemConfiguration();
    EXPECT_EQ(updatedConfig.systemName, "UpdatedSystem");
    EXPECT_EQ(updatedConfig.logLevel, LogLevel::Warning);

    // After initialize() the update must be rejected
    EXPECT_TRUE(system_->initialize());
    newConfig.systemName = "TooLate";
    EXPECT_FALSE(system_->updateSystemConfiguration(newConfig));
    EXPECT_EQ(system_->getSystemConfiguration().systemName, "UpdatedSystem");
}

TEST_F(AxonVexSystemTest, FrameworkConfigurationAccess) {
    EXPECT_TRUE(system_->initialize());

    Configuration& config = system_->getConfiguration();
    const Configuration& constConfig = system_->getConfiguration();

    // Test that we can access the configuration objects
    EXPECT_NO_THROW(config.set("test.key", "test_value"));
    EXPECT_EQ(constConfig.get<std::string>("test.key"), "test_value");
}

// =================================================================
// EVENT AND CALLBACK SYSTEM TESTS
// =================================================================

TEST_F(AxonVexSystemTest, EventCallbacks) {
    auto receivedEvents = std::make_shared<std::vector<SystemEvent>>();
    auto eventMutex = std::make_shared<std::mutex>();

    // Register event callback using shared pointers to ensure lifetime
    uint32_t callbackId =
        system_->registerEventCallback([receivedEvents, eventMutex](const SystemEvent& event) {
            std::lock_guard<std::mutex> lock(*eventMutex);
            receivedEvents->push_back(event);
        });

    EXPECT_GT(callbackId, 0);

    // Perform state transitions that should trigger events
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    // Wait a moment for event processing
    std::this_thread::sleep_for(50ms);

    {
        std::lock_guard<std::mutex> lock(*eventMutex);
        EXPECT_GE(receivedEvents->size(), 2); // At least state change events

        // Check for state change events
        bool foundStateChangeEvent = false;
        for (const auto& event : *receivedEvents) {
            if (event.type == SystemEvent::Type::STATE_CHANGE) {
                foundStateChangeEvent = true;
                break;
            }
        }
        EXPECT_TRUE(foundStateChangeEvent);
    }

    // Unregister callback
    system_->unregisterEventCallback(callbackId);

    EXPECT_TRUE(system_->stop());

    // Allow a moment for the event processing thread to finish
    std::this_thread::sleep_for(50ms);
}

TEST_F(AxonVexSystemTest, ProcessingUnitEvents) {
    // Use shared pointers to ensure lifetime management
    auto receivedEvents = std::make_shared<std::vector<SystemEvent>>();
    auto eventMutex = std::make_shared<std::mutex>();

    // Register event callback with shared pointers to prevent dangling references
    system_->registerEventCallback([receivedEvents, eventMutex](const SystemEvent& event) {
        std::lock_guard<std::mutex> lock(*eventMutex);
        receivedEvents->push_back(event);
    });

    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    // Wait for event processing
    std::this_thread::sleep_for(50ms);

    {
        std::lock_guard<std::mutex> lock(*eventMutex);

        // Check for processing unit added event
        bool foundUnitAddedEvent = false;
        for (const auto& event : *receivedEvents) {
            if (event.type == SystemEvent::Type::PROCESSING_UNIT_ADDED) {
                foundUnitAddedEvent = true;
                EXPECT_EQ(event.metadata.at("unit_name"), "TestUnit");
                break;
            }
        }
        EXPECT_TRUE(foundUnitAddedEvent);
    }

    system_->unregisterProcessingUnit(unitId);

    // Unregister callbacks and stop the system before the test ends
    system_->registerEventCallback(nullptr);
    system_->stop();

    // Give a moment for any pending callbacks to finish
    std::this_thread::sleep_for(50ms);
}

// =================================================================
// HEALTH MONITORING TESTS
// =================================================================

TEST_F(AxonVexSystemTest, BasicHealthCheck) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    // Perform health check
    system_->performHealthCheck();

    SystemHealth health = system_->getHealth();
    EXPECT_EQ(health.overallStatus, SystemHealth::Status::HEALTHY);
    EXPECT_TRUE(health.isHealthy());
    EXPECT_TRUE(health.timingControllerHealthy);
    EXPECT_TRUE(health.configurationHealthy);
    EXPECT_TRUE(health.loggerHealthy);

    EXPECT_TRUE(system_->stop());
}

TEST_F(AxonVexSystemTest, HealthCheckCallbacks) {
    EXPECT_TRUE(system_->initialize());

    // Captured by value (shared_ptr): the monitoring thread may invoke this callback
    // after the test body returns — a stack-local captured by reference dangles (C18 crash).
    auto healthCheckCalled = std::make_shared<std::atomic<bool>>(false);

    // Register health check callback
    uint32_t callbackId =
        system_->registerHealthCheckCallback([healthCheckCalled]() -> SystemHealth {
            healthCheckCalled->store(true);
            SystemHealth customHealth;
            customHealth.overallStatus = SystemHealth::Status::HEALTHY;
            return customHealth;
        });

    EXPECT_GT(callbackId, 0);

    // Perform health check
    system_->performHealthCheck();

    EXPECT_TRUE(healthCheckCalled->load());
}

// Regression test for C18: user callbacks must not be invoked while callbacksMutex_
// is held — a callback that re-enters the callback API would deadlock.
TEST_F(AxonVexSystemTest, HealthCheckCallbackReentrantRegistration) {
    EXPECT_TRUE(system_->initialize());

    auto innerId = std::make_shared<std::atomic<uint32_t>>(0);
    AxonVexSystem* sys = system_.get();

    system_->registerHealthCheckCallback([sys, innerId]() -> SystemHealth {
        // Re-entrant use of the callback API from inside a callback
        uint32_t id = sys->registerHealthCheckCallback([]() -> SystemHealth {
            SystemHealth h;
            h.overallStatus = SystemHealth::Status::HEALTHY;
            return h;
        });
        innerId->store(id);
        SystemHealth h;
        h.overallStatus = SystemHealth::Status::HEALTHY;
        return h;
    });

    std::atomic<bool> done{false};
    std::thread worker([&done, sys] {
        sys->performHealthCheck();
        done.store(true);
    });

    // Poll with timeout: before the C18 fix this deadlocks and never completes.
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    // Detach on the failure path: destroying a joinable thread calls std::terminate,
    // which would abort the whole binary instead of reporting this test's failure.
    if (!done.load()) {
        worker.detach();
    }
    ASSERT_TRUE(done.load())
        << "performHealthCheck deadlocked: callbacks invoked under callbacksMutex_ (C18)";
    worker.join();
    EXPECT_GT(innerId->load(), 0u);
}

// =================================================================
// PERFORMANCE AND STATISTICS TESTS
// =================================================================

TEST_F(AxonVexSystemTest, StatisticsCollection) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    system_->registerProcessingUnit(std::move(unit));

    EXPECT_TRUE(system_->start());

    // Let the system run for a short time
    std::this_thread::sleep_for(100ms);

    const auto& stats = system_->getStatistics();

    EXPECT_EQ(stats.totalProcessingUnits.load(), 1);
    EXPECT_EQ(stats.activeProcessingUnits.load(), 1);
    EXPECT_GT(stats.totalStateTransitions.load(), 0);

    // Check that unit was executed
    EXPECT_GT(unitPtr->getProcessCallCount(), 0);

    EXPECT_TRUE(system_->stop());
}

TEST_F(AxonVexSystemTest, UptimeTracking) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    // Let some time pass
    std::this_thread::sleep_for(100ms);

    double uptime = system_->getUptimeSeconds();
    EXPECT_GT(uptime, 0.05); // Should be at least 50ms
    EXPECT_LT(uptime, 1.0);  // Should be less than 1 second

    EXPECT_TRUE(system_->stop());
}

// =================================================================
// ERROR HANDLING AND RECOVERY TESTS
// =================================================================

TEST_F(AxonVexSystemTest, ProcessingUnitErrorHandling) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("ErrorUnit");
    unit->setShouldThrow(true);
    MockProcessingUnit* unitPtr = unit.get();

    system_->registerProcessingUnit(std::move(unit));

    EXPECT_TRUE(system_->start());

    // Let the system run with errors
    std::this_thread::sleep_for(200ms);

    // System should still be running despite errors
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);
    EXPECT_TRUE(system_->isRunning());

    // Check that error statistics are collected
    const auto& stats = system_->getStatistics();
    EXPECT_GT(stats.errorCount.load(), 0);

    EXPECT_TRUE(system_->stop());
}

// =================================================================
// RESOURCE MANAGEMENT TESTS
// =================================================================

TEST_F(AxonVexSystemTest, MemoryUsageTracking) {
    EXPECT_TRUE(system_->initialize());

    size_t initialMemory = system_->getMemoryUsage();
    EXPECT_GE(initialMemory, 0);

    size_t peakMemory = system_->getPeakMemoryUsage();
    EXPECT_GE(peakMemory, initialMemory);
}

TEST_F(AxonVexSystemTest, ResourceReporting) {
    EXPECT_TRUE(system_->initialize());

    std::string resourceReport = system_->getResourceReport();
    EXPECT_FALSE(resourceReport.empty());
    EXPECT_NE(resourceReport.find("Memory"), std::string::npos);
}

// =================================================================
// LOGGING AND DIAGNOSTICS TESTS
// =================================================================

TEST_F(AxonVexSystemTest, LoggerAccess) {
    EXPECT_TRUE(system_->initialize());

    Logger& logger = system_->getLogger();
    const Logger& constLogger = system_->getLogger();

    EXPECT_TRUE(logger.isRunning());
    EXPECT_TRUE(constLogger.isRunning());
}

TEST_F(AxonVexSystemTest, DebugMode) {
    EXPECT_FALSE(system_->isDebugMode());

    system_->setDebugMode(true);
    EXPECT_TRUE(system_->isDebugMode());

    system_->setDebugMode(false);
    EXPECT_FALSE(system_->isDebugMode());
}

TEST_F(AxonVexSystemTest, SystemReporting) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    std::string systemReport = system_->getSystemReport();
    EXPECT_FALSE(systemReport.empty());
    EXPECT_NE(systemReport.find("System"), std::string::npos);

    EXPECT_TRUE(system_->stop());
}

// =================================================================
// CONCURRENT OPERATIONS TESTS
// =================================================================

TEST_F(AxonVexSystemTest, ConcurrentProcessingUnitRegistration) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());

    const int numThreads = 4;
    const int unitsPerThread = 5;
    std::vector<std::thread> threads;
    std::atomic<int> registeredCount{0};
    std::atomic<int> errorCount{0};

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < unitsPerThread; ++i) {
                try {
                    auto unit = std::make_unique<MockProcessingUnit>("Thread" + std::to_string(t) +
                                                                     "_Unit" + std::to_string(i));
                    system_->registerProcessingUnit(std::move(unit));
                    registeredCount.fetch_add(1);
                } catch (...) { errorCount.fetch_add(1); }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(registeredCount.load(), numThreads * unitsPerThread);
    EXPECT_EQ(errorCount.load(), 0);
    EXPECT_EQ(system_->getProcessingUnitCount(), numThreads * unitsPerThread);

    EXPECT_TRUE(system_->stop());
}

TEST_F(AxonVexSystemTest, StateTransitionThreadSafety) {
    EXPECT_TRUE(system_->initialize());

    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    // Multiple threads trying to start the system
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            if (system_->start()) {
                successCount.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Only one thread should succeed in starting the system
    EXPECT_EQ(successCount.load(), 1);
    EXPECT_EQ(system_->getState(), SystemState::RUNNING);

    EXPECT_TRUE(system_->stop());
}

// =================================================================
// SYSTEM PORT MANAGEMENT TESTS
// =================================================================

TEST_F(AxonVexSystemTest, SystemPortAssignment) {
    EXPECT_TRUE(system_->initialize());

    // Create a processing unit with ports
    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();

    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    // Test assigning system input port
    EXPECT_TRUE(system_->assignSystemInputPort("system_input", unitPtr, 1001));
    EXPECT_TRUE(system_->hasSystemInputPort("system_input"));
    EXPECT_FALSE(system_->hasSystemInputPort("nonexistent"));

    // Test assigning system output port
    EXPECT_TRUE(system_->assignSystemOutputPort("system_output", unitPtr, 1000));
    EXPECT_TRUE(system_->hasSystemOutputPort("system_output"));
    EXPECT_FALSE(system_->hasSystemOutputPort("nonexistent"));

    // Test duplicate assignment (should fail)
    EXPECT_FALSE(system_->assignSystemInputPort("system_input", unitPtr, 1001));
    EXPECT_FALSE(system_->assignSystemOutputPort("system_output", unitPtr, 1000));

    // Test port retrieval
    BasePort* inputPort = system_->getSystemInputPort("system_input");
    BasePort* outputPort = system_->getSystemOutputPort("system_output");
    EXPECT_NE(inputPort, nullptr);
    EXPECT_NE(outputPort, nullptr);

    // Test port names retrieval
    auto inputNames = system_->getSystemInputPortNames();
    auto outputNames = system_->getSystemOutputPortNames();
    EXPECT_EQ(inputNames.size(), 1);
    EXPECT_EQ(outputNames.size(), 1);
    EXPECT_EQ(inputNames[0], "system_input");
    EXPECT_EQ(outputNames[0], "system_output");
}

TEST_F(AxonVexSystemTest, SystemPortRemoval) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    system_->registerProcessingUnit(std::move(unit));

    // Assign ports
    EXPECT_TRUE(system_->assignSystemInputPort("test_input", unitPtr, 1001));
    EXPECT_TRUE(system_->assignSystemOutputPort("test_output", unitPtr, 1000));

    // Verify they exist
    EXPECT_TRUE(system_->hasSystemInputPort("test_input"));
    EXPECT_TRUE(system_->hasSystemOutputPort("test_output"));

    // Remove them
    EXPECT_TRUE(system_->removeSystemInputPort("test_input"));
    EXPECT_TRUE(system_->removeSystemOutputPort("test_output"));

    // Verify they're gone
    EXPECT_FALSE(system_->hasSystemInputPort("test_input"));
    EXPECT_FALSE(system_->hasSystemOutputPort("test_output"));

    // Test removing non-existent ports
    EXPECT_FALSE(system_->removeSystemInputPort("nonexistent"));
    EXPECT_FALSE(system_->removeSystemOutputPort("nonexistent"));
}

TEST_F(AxonVexSystemTest, SystemPortAutoCleanup) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    // Assign system ports
    EXPECT_TRUE(system_->assignSystemInputPort("auto_input", unitPtr, 1001));
    EXPECT_TRUE(system_->assignSystemOutputPort("auto_output", unitPtr, 1000));

    // Verify ports exist
    EXPECT_TRUE(system_->hasSystemInputPort("auto_input"));
    EXPECT_TRUE(system_->hasSystemOutputPort("auto_output"));

    // Remove the processing unit - should auto-cleanup system ports
    EXPECT_TRUE(system_->unregisterProcessingUnit(unitId));

    // Verify system ports were automatically removed
    EXPECT_FALSE(system_->hasSystemInputPort("auto_input"));
    EXPECT_FALSE(system_->hasSystemOutputPort("auto_output"));
}

TEST_F(AxonVexSystemTest, SystemPortInfo) {
    EXPECT_TRUE(system_->initialize());

    auto unit1 = std::make_unique<MockProcessingUnit>("Unit1");
    auto unit2 = std::make_unique<MockProcessingUnit>("Unit2");
    MockProcessingUnit* unit1Ptr = unit1.get();
    MockProcessingUnit* unit2Ptr = unit2.get();

    system_->registerProcessingUnit(std::move(unit1));
    system_->registerProcessingUnit(std::move(unit2));

    // Assign multiple system ports
    system_->assignSystemInputPort("input1", unit1Ptr, 1001);
    system_->assignSystemInputPort("input2", unit2Ptr, 1001);
    system_->assignSystemOutputPort("output1", unit1Ptr, 1000);
    system_->assignSystemOutputPort("output2", unit2Ptr, 1000);

    // Get port info
    std::string portInfo = system_->getSystemPortInfo();
    EXPECT_FALSE(portInfo.empty());
    EXPECT_NE(portInfo.find("input1"), std::string::npos);
    EXPECT_NE(portInfo.find("input2"), std::string::npos);
    EXPECT_NE(portInfo.find("output1"), std::string::npos);
    EXPECT_NE(portInfo.find("output2"), std::string::npos);
    EXPECT_NE(portInfo.find("Unit1"), std::string::npos);
    EXPECT_NE(portInfo.find("Unit2"), std::string::npos);
}

TEST_F(AxonVexSystemTest, InvalidSystemPortOperations) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    system_->registerProcessingUnit(std::move(unit));

    // Test invalid parameters
    EXPECT_FALSE(system_->assignSystemInputPort("", unitPtr, 1001));     // Empty name
    EXPECT_FALSE(system_->assignSystemInputPort("test", nullptr, 1001)); // Null unit
    EXPECT_FALSE(system_->assignSystemInputPort("test", unitPtr, 9999)); // Invalid port ID

    // Test with unregistered unit
    auto unregisteredUnit = std::make_unique<MockProcessingUnit>("Unregistered");
    EXPECT_FALSE(system_->assignSystemInputPort("test", unregisteredUnit.get(), 1001));
}

TEST_F(AxonVexSystemTest, SystemPortTypeConsistency) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("TestUnit");
    MockProcessingUnit* unitPtr = unit.get();
    system_->registerProcessingUnit(std::move(unit));

    // Assign ports successfully
    EXPECT_TRUE(system_->assignSystemInputPort("typed_input", unitPtr, 1001));
    EXPECT_TRUE(system_->assignSystemOutputPort("typed_output", unitPtr, 1000));

    // Verify port types through getSystemPortInfo
    std::string portInfo = system_->getSystemPortInfo();
    EXPECT_NE(portInfo.find("typed_input"), std::string::npos);
    EXPECT_NE(portInfo.find("typed_output"), std::string::npos);
    EXPECT_NE(portInfo.find("TestUnit"), std::string::npos);
}

TEST_F(AxonVexSystemTest, SystemPortDuplicateAssignment) {
    EXPECT_TRUE(system_->initialize());

    auto unit1 = std::make_unique<MockProcessingUnit>("Unit1");
    auto unit2 = std::make_unique<MockProcessingUnit>("Unit2");
    MockProcessingUnit* unit1Ptr = unit1.get();
    MockProcessingUnit* unit2Ptr = unit2.get();

    system_->registerProcessingUnit(std::move(unit1));
    system_->registerProcessingUnit(std::move(unit2));

    // First assignment should succeed
    EXPECT_TRUE(system_->assignSystemInputPort("shared_input", unit1Ptr, 1001));

    // Second assignment with same name should fail
    EXPECT_FALSE(system_->assignSystemInputPort("shared_input", unit2Ptr, 1001));

    // But different names should succeed
    EXPECT_TRUE(system_->assignSystemInputPort("different_input", unit2Ptr, 1001));
}

TEST_F(AxonVexSystemTest, SystemPortCascadingCleanup) {
    EXPECT_TRUE(system_->initialize());

    auto unit1 = std::make_unique<MockProcessingUnit>("Unit1");
    auto unit2 = std::make_unique<MockProcessingUnit>("Unit2");
    MockProcessingUnit* unit1Ptr = unit1.get();
    MockProcessingUnit* unit2Ptr = unit2.get();

    uint32_t unit1Id = system_->registerProcessingUnit(std::move(unit1));
    uint32_t unit2Id = system_->registerProcessingUnit(std::move(unit2));

    // Assign multiple system ports
    system_->assignSystemInputPort("unit1_input", unit1Ptr, 1001);
    system_->assignSystemOutputPort("unit1_output", unit1Ptr, 1000);
    system_->assignSystemInputPort("unit2_input", unit2Ptr, 1001);
    system_->assignSystemOutputPort("unit2_output", unit2Ptr, 1000);

    // Verify all ports exist
    EXPECT_EQ(system_->getSystemInputPortNames().size(), 2);
    EXPECT_EQ(system_->getSystemOutputPortNames().size(), 2);

    // Remove unit1 - should auto-cleanup its ports
    EXPECT_TRUE(system_->unregisterProcessingUnit(unit1Id));

    // Unit1 ports should be gone, Unit2 ports should remain
    auto inputNames = system_->getSystemInputPortNames();
    auto outputNames = system_->getSystemOutputPortNames();
    EXPECT_EQ(inputNames.size(), 1);
    EXPECT_EQ(outputNames.size(), 1);
    EXPECT_EQ(inputNames[0], "unit2_input");
    EXPECT_EQ(outputNames[0], "unit2_output");

    // Remove unit2 - should cleanup remaining ports
    EXPECT_TRUE(system_->unregisterProcessingUnit(unit2Id));
    EXPECT_EQ(system_->getSystemInputPortNames().size(), 0);
    EXPECT_EQ(system_->getSystemOutputPortNames().size(), 0);
}

TEST_F(AxonVexSystemTest, SystemPortEdgeCases) {
    EXPECT_TRUE(system_->initialize());

    auto unit = std::make_unique<MockProcessingUnit>("EdgeCaseUnit");
    MockProcessingUnit* unitPtr = unit.get();
    uint32_t unitId = system_->registerProcessingUnit(std::move(unit));

    // Test very long port names
    std::string longName(255, 'a');
    EXPECT_TRUE(system_->assignSystemInputPort(longName, unitPtr, 1001));
    EXPECT_TRUE(system_->hasSystemInputPort(longName));

    // Test removal of non-existent ports
    EXPECT_FALSE(system_->removeSystemInputPort("does_not_exist"));
    EXPECT_FALSE(system_->removeSystemOutputPort("does_not_exist"));

    // Test getting non-existent ports
    EXPECT_EQ(system_->getSystemInputPort("does_not_exist"), nullptr);
    EXPECT_EQ(system_->getSystemOutputPort("does_not_exist"), nullptr);

    // Test port info when ports exist
    std::string info = system_->getSystemPortInfo();
    EXPECT_FALSE(info.empty());
    EXPECT_NE(info.find(longName), std::string::npos);
}

TEST_F(AxonVexSystemTest, SystemPortThreadSafety) {
    EXPECT_TRUE(system_->initialize());

    // Create multiple units for concurrent operations
    std::vector<std::unique_ptr<MockProcessingUnit>> units;
    std::vector<MockProcessingUnit*> unitPtrs;

    for (int i = 0; i < 5; ++i) {
        auto unit = std::make_unique<MockProcessingUnit>("ThreadUnit" + std::to_string(i));
        unitPtrs.push_back(unit.get());
        system_->registerProcessingUnit(std::move(unit));
    }

    // Concurrent port assignments
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([this, i, &unitPtrs, &successCount]() {
            std::string inputName = "thread_input_" + std::to_string(i);
            std::string outputName = "thread_output_" + std::to_string(i);

            if (system_->assignSystemInputPort(inputName, unitPtrs[i], 1001)) {
                successCount++;
            }
            if (system_->assignSystemOutputPort(outputName, unitPtrs[i], 1000)) {
                successCount++;
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // All assignments should succeed in thread-safe manner
    EXPECT_EQ(successCount.load(), 10); // 5 inputs + 5 outputs
    EXPECT_EQ(system_->getSystemInputPortNames().size(), 5);
    EXPECT_EQ(system_->getSystemOutputPortNames().size(), 5);
}

namespace {
// Registers a MockProcessingUnit on `sys` and exposes its ports as system
// ports "out"/"in". Returns false on any setup failure.
bool exposeSystemPorts(AxonVexSystem& sys) {
    auto unit = std::make_unique<MockProcessingUnit>("PortUnit");
    MockProcessingUnit* unitPtr = unit.get();
    sys.registerProcessingUnit(std::move(unit));
    return sys.assignSystemOutputPort("out", unitPtr, 1000) &&
           sys.assignSystemInputPort("in", unitPtr, 1001);
}
} // namespace

// C9 regression: connectToSystem/disconnectFromSystem locked the two systems'
// systemPortsMutex_ in ARGUMENT order, so a→b concurrent with b→a acquired
// them in opposite orders — AB/BA deadlock. Deadline-guarded: a regression
// hangs rather than fails.
TEST_F(AxonVexSystemTest, OpposingCrossSystemConnectsDoNotDeadlock) {
    TestAxonVexSystem a, b;
    ASSERT_TRUE(a.initialize());
    ASSERT_TRUE(b.initialize());
    ASSERT_TRUE(exposeSystemPorts(a));
    ASSERT_TRUE(exposeSystemPorts(b));

    auto doneA = std::make_shared<std::atomic<bool>>(false);
    auto doneB = std::make_shared<std::atomic<bool>>(false);

    std::thread ta([&a, &b, doneA]() {
        for (int i = 0; i < 500; ++i) {
            a.connectToSystem<double>("out", &b, "in");
            a.disconnectFromSystem<double>("out", &b, "in");
        }
        doneA->store(true);
    });
    std::thread tb([&a, &b, doneB]() {
        for (int i = 0; i < 500; ++i) {
            b.connectToSystem<double>("out", &a, "in");
            b.disconnectFromSystem<double>("out", &a, "in");
        }
        doneB->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!(doneA->load() && doneB->load()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!doneA->load() || !doneB->load()) {
        ta.detach(); // wedged; leak them rather than hang the suite
        tb.detach();
        FAIL() << "opposing cross-system connects deadlocked";
    }
    ta.join();
    tb.join();
}

// C9, second shape: targetSystem == this locked the same non-recursive mutex
// twice (UB, hangs in practice). Self-connection is legitimate — an output
// port looped back to an input port of the same system.
TEST_F(AxonVexSystemTest, ConnectSystemToItselfDoesNotSelfDeadlock) {
    TestAxonVexSystem a;
    ASSERT_TRUE(a.initialize());
    ASSERT_TRUE(exposeSystemPorts(a));

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto connected = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([&a, done, connected]() {
        connected->store(a.connectToSystem<double>("out", &a, "in"));
        a.disconnectFromSystem<double>("out", &a, "in");
        done->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done->load()) {
        worker.detach();
        FAIL() << "self-connection deadlocked on systemPortsMutex_";
    }
    worker.join();
    EXPECT_TRUE(connected->load());
}

// C38 regression: isShuttingDown_ (set by stop/emergencyShutdown) was never
// cleared, so a system re-initialized after reset() had a dead event pipeline:
// eventProcessingLoop's guard saw the stale flag and exited immediately, and
// publishEvent dropped every event — with the state machine reporting healthy.
TEST_F(AxonVexSystemTest, EventPipelineIsAliveAfterResetAndReinitialize) {
    EXPECT_TRUE(system_->initialize());
    EXPECT_TRUE(system_->start());
    EXPECT_TRUE(system_->stop());
    system_->reset();

    EXPECT_TRUE(system_->initialize());
    auto sawEvent = std::make_shared<std::atomic<bool>>(false);
    system_->registerEventCallback([sawEvent](const SystemEvent&) { sawEvent->store(true); });
    EXPECT_TRUE(system_->start()); // publishes STATE_CHANGE events

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!sawEvent->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(sawEvent->load()) << "re-initialized system's event pipeline is dead";
    EXPECT_TRUE(system_->stop());
}

} // anonymous namespace
