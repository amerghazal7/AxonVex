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

    std::atomic<int> getProcessCallCount() const {
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
    EXPECT_TRUE(system_->initialize());

    SystemConfiguration newConfig = config_;
    newConfig.systemName = "UpdatedSystem";
    newConfig.logLevel = LogLevel::Warning;

    EXPECT_TRUE(system_->updateSystemConfiguration(newConfig));

    const auto& updatedConfig = system_->getSystemConfiguration();
    EXPECT_EQ(updatedConfig.systemName, "UpdatedSystem");
    EXPECT_EQ(updatedConfig.logLevel, LogLevel::Warning);
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

    bool healthCheckCalled = false;

    // Register health check callback
    uint32_t callbackId = system_->registerHealthCheckCallback([&]() -> SystemHealth {
        healthCheckCalled = true;
        SystemHealth customHealth;
        customHealth.overallStatus = SystemHealth::Status::HEALTHY;
        return customHealth;
    });

    EXPECT_GT(callbackId, 0);

    // Perform health check
    system_->performHealthCheck();

    EXPECT_TRUE(healthCheckCalled);
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

} // anonymous namespace
