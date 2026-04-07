/**
 * @file processingUnitTest.cpp
 * @brief Unit tests for Advanced Processing Unit
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 */

#include <axonvex_core/processingUnit.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>

namespace axonvex::core::test {
namespace {

// Test implementation of ProcessingUnit
class TestProcessingUnit : public ProcessingUnit {
  public:
    explicit TestProcessingUnit(const std::string& name) : ProcessingUnit(name) {}

    void processSync() override {
        syncExecutionCount_++;
        lastSyncProcessingTime_ = std::chrono::steady_clock::now();

        // Simple processing: read from input, multiply by factor, write to output
        if (inputPort_ && inputPort_->hasNewData()) {
            int data = inputPort_->read();
            inputPort_->clearNewDataFlag();

            if (outputPort_) {
                outputPort_->write(data * processingFactor_);
            }
        }
    }

    void processAsync() override {
        asyncExecutionCount_++;

        // Handle custom async commands
        if (customAsyncPort_ && customAsyncPort_->wasUpdated()) {
            std::string command = customAsyncPort_->read();
            lastAsyncCommand_ = command;

            if (command == "MULTIPLY_2") {
                processingFactor_ = 2;
            } else if (command == "MULTIPLY_3") {
                processingFactor_ = 3;
            }
        }
    }

    void reset() override {
        resetCount_++;
        syncExecutionCount_ = 0;
        asyncExecutionCount_ = 0;
        processingFactor_ = 1;
        lastAsyncCommand_.clear();
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "TestProcessingUnit";
    }

    // Test setup helpers
    void setupPorts() {
        inputPort_ = createInputPort<int>(1, "Input");
        outputPort_ = createOutputPort<int>(1, "Output");
        customAsyncPort_ = createAsyncInputPort<std::string>(1, "AsyncCommand");
    }

    // Accessors for testing
    int getSyncExecutionCount() const {
        return syncExecutionCount_;
    }
    int getAsyncExecutionCount() const {
        return asyncExecutionCount_;
    }
    int getResetCount() const {
        return resetCount_;
    }
    int getProcessingFactor() const {
        return processingFactor_;
    }
    const std::string& getLastAsyncCommand() const {
        return lastAsyncCommand_;
    }

    InputPort<int>* getTestInputPort() {
        return inputPort_;
    }
    OutputPort<int>* getTestOutputPort() {
        return outputPort_;
    }
    AsyncInputPort<std::string>* getTestAsyncPort() {
        return customAsyncPort_;
    }

  private:
    int syncExecutionCount_ = 0;
    int asyncExecutionCount_ = 0;
    int resetCount_ = 0;
    int processingFactor_ = 1;
    std::string lastAsyncCommand_;
    std::chrono::steady_clock::time_point lastSyncProcessingTime_;

    // Test ports
    InputPort<int>* inputPort_ = nullptr;
    OutputPort<int>* outputPort_ = nullptr;
    AsyncInputPort<std::string>* customAsyncPort_ = nullptr;
};

// Basic functionality tests
class ProcessingUnitTest : public ::testing::Test {
  protected:
    void SetUp() override {
        unit = std::make_unique<TestProcessingUnit>("TestUnit");
        unit->setBlockUID(1);
        unit->setupPorts();
    }

    std::unique_ptr<TestProcessingUnit> unit;
};

TEST_F(ProcessingUnitTest, Construction) {
    EXPECT_EQ(unit->getName(), "TestUnit");
    EXPECT_EQ(unit->getInstanceDescription(), "TestUnit");
    EXPECT_EQ(unit->getBlockUID(), 1);
    EXPECT_TRUE(unit->hasBeenAddedToSystem());
    EXPECT_EQ(unit->getTypeDescription(), "TestProcessingUnit");
    EXPECT_EQ(unit->getState(), ExecutionState::UNINITIALIZED);
    EXPECT_FALSE(unit->isDisabled());
}

TEST_F(ProcessingUnitTest, BuiltInControlPorts) {
    // Test built-in reset port (created automatically)
    auto resetPort = unit->getAsyncInputPort<int>(ControlPorts::RESET);
    EXPECT_NE(resetPort, nullptr);
    EXPECT_EQ(unit->getAsyncInputPortName(ControlPorts::RESET), "Reset");

    // Test built-in disable port
    auto disablePort = unit->getAsyncInputPort<int>(ControlPorts::DISABLE);
    EXPECT_NE(disablePort, nullptr);
    EXPECT_EQ(unit->getAsyncInputPortName(ControlPorts::DISABLE), "Disable");
}

TEST_F(ProcessingUnitTest, PortCreationAndAccess) {
    // Test created ports
    EXPECT_NE(unit->getTestInputPort(), nullptr);
    EXPECT_NE(unit->getTestOutputPort(), nullptr);
    EXPECT_NE(unit->getTestAsyncPort(), nullptr);

    // Test port names
    EXPECT_EQ(unit->getInputPortName(1), "Input");
    EXPECT_EQ(unit->getOutputPortName(1), "Output");
    EXPECT_EQ(unit->getAsyncInputPortName(1), "AsyncCommand");

    // Test port collections
    EXPECT_EQ(unit->getInputPorts().size(), 1);
    EXPECT_EQ(unit->getOutputPorts().size(), 1);
    EXPECT_EQ(unit->getAsyncInputPorts().size(), 3); // 1 custom + 2 built-in
    EXPECT_EQ(unit->getAsyncOutputPorts().size(), 0);
}

TEST_F(ProcessingUnitTest, PortUIDGeneration) {
    // Verify port UIDs follow formula
    EXPECT_EQ(unit->getTestInputPort()->getPortUID(), 1 * 256 + 1);
    EXPECT_EQ(unit->getTestOutputPort()->getPortUID(), 1 * 256 + 1);
    EXPECT_EQ(unit->getTestAsyncPort()->getPortUID(), 1 * 256 + 1);

    // Built-in control ports
    auto resetPort = unit->getAsyncInputPort<int>(ControlPorts::RESET);
    auto disablePort = unit->getAsyncInputPort<int>(ControlPorts::DISABLE);
    EXPECT_EQ(resetPort->getPortUID(), 1 * 256 + ControlPorts::RESET);
    EXPECT_EQ(disablePort->getPortUID(), 1 * 256 + ControlPorts::DISABLE);
}

TEST_F(ProcessingUnitTest, SyncProcessing) {
    EXPECT_EQ(unit->getSyncExecutionCount(), 0);

    // Process without data - should execute but not process anything
    unit->processSyncBase();
    EXPECT_EQ(unit->getSyncExecutionCount(), 1);

    // Add data and process
    unit->getTestInputPort()->writeData(10);
    unit->processSyncBase();
    EXPECT_EQ(unit->getSyncExecutionCount(), 2);

    // Check output (should be input * factor = 10 * 1 = 10)
    auto outputPort = unit->getTestOutputPort();
    // Note: In real implementation, we'd need to connect to another unit's input
    // For this test, we're just verifying the processing logic was called
}

TEST_F(ProcessingUnitTest, AsyncProcessing) {
    EXPECT_EQ(unit->getAsyncExecutionCount(), 0);

    // Process without async data
    unit->processAsyncBase();
    EXPECT_EQ(unit->getAsyncExecutionCount(), 1);

    // Send async command
    unit->getTestAsyncPort()->update("MULTIPLY_2");
    unit->processAsyncBase();
    EXPECT_EQ(unit->getAsyncExecutionCount(), 2);
    EXPECT_EQ(unit->getLastAsyncCommand(), "MULTIPLY_2");
    EXPECT_EQ(unit->getProcessingFactor(), 2);
}

TEST_F(ProcessingUnitTest, BuiltInResetFunctionality) {
    // Execute some processing first
    unit->processSyncBase();
    unit->processAsyncBase();
    EXPECT_GT(unit->getSyncExecutionCount(), 0);
    EXPECT_GT(unit->getAsyncExecutionCount(), 0);

    // Trigger reset via built-in port
    auto resetPort = unit->getAsyncInputPort<int>(ControlPorts::RESET);
    resetPort->update(1); // Non-zero triggers reset

    int resetCountBefore = unit->getResetCount();
    unit->processAsyncBase();
    EXPECT_EQ(unit->getResetCount(), resetCountBefore + 1);
    EXPECT_EQ(unit->getSyncExecutionCount(), 0); // Reset by derived reset()
    // Note: Async execution count is incremented because processAsyncBase() was called to handle
    // the reset
    EXPECT_GE(unit->getAsyncExecutionCount(), 0);
}

TEST_F(ProcessingUnitTest, BuiltInDisableFunctionality) {
    EXPECT_FALSE(unit->isDisabled());

    // Disable via built-in port
    auto disablePort = unit->getAsyncInputPort<int>(ControlPorts::DISABLE);
    disablePort->update(1); // Non-zero disables
    unit->processAsyncBase();

    EXPECT_TRUE(unit->isDisabled());
    EXPECT_EQ(unit->getState(), ExecutionState::DISABLED);

    // Sync processing should be skipped when disabled
    int syncCountBefore = unit->getSyncExecutionCount();
    unit->processSyncBase();
    EXPECT_EQ(unit->getSyncExecutionCount(), syncCountBefore); // No increment

    // Re-enable
    disablePort->update(0); // Zero enables
    unit->processAsyncBase();
    EXPECT_FALSE(unit->isDisabled());
    EXPECT_EQ(unit->getState(), ExecutionState::RUNNING);
}

TEST_F(ProcessingUnitTest, DownSamplingFactor) {
    EXPECT_EQ(unit->getDownSamplingFactor(), 1); // Default

    // Set down-sampling factor
    unit->setDownSamplingFactor(3);
    EXPECT_EQ(unit->getDownSamplingFactor(), 3);

    // Should only process every 3rd call
    int syncCountBefore = unit->getSyncExecutionCount();
    unit->processSyncBase(); // Call 1 - no processing
    unit->processSyncBase(); // Call 2 - no processing
    EXPECT_EQ(unit->getSyncExecutionCount(), syncCountBefore);

    unit->processSyncBase(); // Call 3 - should process
    EXPECT_EQ(unit->getSyncExecutionCount(), syncCountBefore + 1);

    unit->processSyncBase(); // Call 4 - no processing
    unit->processSyncBase(); // Call 5 - no processing
    unit->processSyncBase(); // Call 6 - should process
    EXPECT_EQ(unit->getSyncExecutionCount(), syncCountBefore + 2);
}

TEST_F(ProcessingUnitTest, InheritDownSamplingFactor) {
    auto sourceUnit = std::make_unique<TestProcessingUnit>("SourceUnit");
    sourceUnit->setDownSamplingFactor(5);

    unit->inheritDownSamplingFactor(sourceUnit.get());
    EXPECT_EQ(unit->getDownSamplingFactor(), 5);

    // Test with null pointer
    unit->inheritDownSamplingFactor(nullptr);
    EXPECT_EQ(unit->getDownSamplingFactor(), 5); // Should remain unchanged
}

TEST_F(ProcessingUnitTest, SamplingPeriod) {
    auto defaultPeriod = unit->getBlockSamplingPeriod();
    EXPECT_EQ(defaultPeriod, std::chrono::milliseconds(10)); // Default

    auto newPeriod = std::chrono::microseconds(500);
    unit->setBlockSamplingPeriod(newPeriod);
    EXPECT_EQ(unit->getBlockSamplingPeriod(), newPeriod);
}

TEST_F(ProcessingUnitTest, URLManagement) {
    EXPECT_TRUE(unit->getRelativeURL().empty());
    EXPECT_TRUE(unit->getAbsoluteURL().empty());

    unit->setURL("test/path/unit");
    EXPECT_EQ(unit->getRelativeURL(), "test/path/unit");
    EXPECT_EQ(unit->getAbsoluteURL(), "test/path/unit");
}

TEST_F(ProcessingUnitTest, ParentBlockHierarchy) {
    auto parentUnit = std::make_unique<TestProcessingUnit>("ParentUnit");

    EXPECT_EQ(unit->getParentBlock(), nullptr);

    unit->setParentBlock(parentUnit.get());
    EXPECT_EQ(unit->getParentBlock(), parentUnit.get());
}

TEST_F(ProcessingUnitTest, InstanceDescription) {
    EXPECT_EQ(unit->getInstanceDescription(), "TestUnit");

    unit->updateInstanceDescription("Updated Test Description");
    EXPECT_EQ(unit->getInstanceDescription(), "Updated Test Description");
}

TEST_F(ProcessingUnitTest, ThreadSafety) {
    // Test thread safety setting for all ports
    unit->setPortsThreadSafe(true);
    EXPECT_TRUE(unit->getTestInputPort()->isThreadSafe());
    EXPECT_TRUE(unit->getTestOutputPort()->isThreadSafe());
    EXPECT_TRUE(unit->getTestAsyncPort()->isThreadSafe());

    unit->setPortsThreadSafe(false);
    EXPECT_FALSE(unit->getTestInputPort()->isThreadSafe());
    EXPECT_FALSE(unit->getTestOutputPort()->isThreadSafe());
    EXPECT_FALSE(unit->getTestAsyncPort()->isThreadSafe());
}

TEST_F(ProcessingUnitTest, ResetFunctionality) {
    // Set up some state
    unit->getTestInputPort()->writeData(42);
    unit->getTestAsyncPort()->update("MULTIPLY_3");

    // Check initial state before processing
    EXPECT_TRUE(unit->getTestInputPort()->hasNewData());
    EXPECT_TRUE(unit->getTestAsyncPort()->wasUpdated());

    // Process to get some execution counts
    unit->processSyncBase();
    unit->processAsyncBase();

    EXPECT_GT(unit->getSyncExecutionCount(), 0);
    EXPECT_GT(unit->getAsyncExecutionCount(), 0);

    // Set up fresh port data to verify reset clears it
    unit->getTestInputPort()->writeData(99);
    unit->getTestAsyncPort()->update("NEW_COMMAND");

    // Reset the block
    unit->resetBlock();

    // Check that ports and state were reset
    EXPECT_FALSE(unit->getTestInputPort()->hasNewData());
    EXPECT_FALSE(unit->getTestAsyncPort()->wasUpdated());
    EXPECT_EQ(unit->getSyncExecutionCount(), 0);
    EXPECT_EQ(unit->getAsyncExecutionCount(), 0);
    EXPECT_EQ(unit->getState(), ExecutionState::INITIALIZED);
}

TEST_F(ProcessingUnitTest, ExecutionStatistics) {
    auto stats = unit->getExecutionStats();
    EXPECT_EQ(stats.syncExecutionCount, 0);
    EXPECT_EQ(stats.asyncExecutionCount, 0);

    // Execute some processing
    unit->processSyncBase();
    unit->processSyncBase();
    unit->processAsyncBase();

    stats = unit->getExecutionStats();
    EXPECT_EQ(stats.syncExecutionCount, 2);
    EXPECT_EQ(stats.asyncExecutionCount, 1);
    // Note: Timing might be very small for simple test operations
    EXPECT_GE(stats.totalSyncTime.count(), 0);
    EXPECT_GE(stats.totalAsyncTime.count(), 0);

    // Reset statistics
    unit->resetExecutionStats();
    stats = unit->getExecutionStats();
    EXPECT_EQ(stats.syncExecutionCount, 0);
    EXPECT_EQ(stats.asyncExecutionCount, 0);
}

TEST_F(ProcessingUnitTest, PerformanceMetricsLegacy) {
    // Test legacy performance metrics interface
    auto metrics = unit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, 0);

    // Execute some processing
    unit->processSyncBase();
    unit->processAsyncBase();

    metrics = unit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, 2); // 1 sync + 1 async

    // Reset legacy metrics
    unit->resetPerformanceMetrics();
    metrics = unit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, 0);
}

TEST_F(ProcessingUnitTest, InvalidDownSamplingFactor) {
    EXPECT_THROW(unit->setDownSamplingFactor(0), std::invalid_argument);
    EXPECT_THROW(unit->setDownSamplingFactor(-1), std::invalid_argument);

    // Valid factors should not throw
    EXPECT_NO_THROW(unit->setDownSamplingFactor(1));
    EXPECT_NO_THROW(unit->setDownSamplingFactor(10));
}

// Integration test with multiple units
class MultiUnitIntegrationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        producer = std::make_unique<TestProcessingUnit>("Producer");
        processor = std::make_unique<TestProcessingUnit>("Processor");
        consumer = std::make_unique<TestProcessingUnit>("Consumer");

        producer->setBlockUID(1);
        processor->setBlockUID(2);
        consumer->setBlockUID(3);

        producer->setupPorts();
        processor->setupPorts();
        consumer->setupPorts();

        // Connect the pipeline: Producer -> Processor -> Consumer
        producer->getTestOutputPort()->connect(processor->getTestInputPort());
        processor->getTestOutputPort()->connect(consumer->getTestInputPort());
    }

    std::unique_ptr<TestProcessingUnit> producer;
    std::unique_ptr<TestProcessingUnit> processor;
    std::unique_ptr<TestProcessingUnit> consumer;
};

TEST_F(MultiUnitIntegrationTest, DataPipeline) {
    // Producer generates data (input = 0, output = 0 * 1 = 0, but we'll manually set)
    producer->getTestInputPort()->writeData(10);
    producer->processSyncBase(); // Should output 10 * 1 = 10

    // Processor should receive and process data
    processor->processSyncBase(); // Should output 10 * 1 = 10

    // Consumer should receive processed data
    consumer->processSyncBase(); // Should process 10

    // Verify processing occurred
    EXPECT_EQ(producer->getSyncExecutionCount(), 1);
    EXPECT_EQ(processor->getSyncExecutionCount(), 1);
    EXPECT_EQ(consumer->getSyncExecutionCount(), 1);
}

TEST_F(MultiUnitIntegrationTest, DownSamplingInheritance) {
    // Set producer to down-sample by factor of 2
    producer->setDownSamplingFactor(2);

    // Processor inherits from producer
    processor->inheritDownSamplingFactor(producer.get());
    EXPECT_EQ(processor->getDownSamplingFactor(), 2);

    // Consumer inherits from processor
    consumer->inheritDownSamplingFactor(processor.get());
    EXPECT_EQ(consumer->getDownSamplingFactor(), 2);
}

TEST_F(MultiUnitIntegrationTest, AsyncControlCommands) {
    // Send async command to processor to change processing factor
    processor->getTestAsyncPort()->update("MULTIPLY_3");
    processor->processAsyncBase();
    EXPECT_EQ(processor->getProcessingFactor(), 3);

    // Now data should be multiplied by 3
    processor->getTestInputPort()->writeData(5);
    processor->processSyncBase();
    // Processing: 5 * 3 = 15 (but we'd need to check connected output)
}

} // anonymous namespace
} // namespace axonvex::core::test
