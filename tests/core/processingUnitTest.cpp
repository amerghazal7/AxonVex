#include <gtest/gtest.h>
#include "axonvex/core/processingUnit.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <set>
#include <iostream>

using namespace axonvex::core;
using namespace std::chrono_literals;

// Test ProcessingUnit implementation for testing
class TestProcessingUnit : public ProcessingUnit {
private:
    std::atomic<uint32_t> syncCallCount_{0};
    std::atomic<uint32_t> asyncCallCount_{0};
    std::atomic<uint32_t> resetCallCount_{0};
    std::atomic<bool> shouldThrow_{false};
    std::chrono::microseconds simulatedProcessingTime_{0};

public:
    explicit TestProcessingUnit(const std::string& name) : ProcessingUnit(name) {}

    void processSync() override {
        auto start = std::chrono::steady_clock::now();
        
        if (shouldThrow_.load()) {
            throw std::runtime_error("Simulated processing error");
        }
        
        setState(ExecutionState::RUNNING);
        syncCallCount_.fetch_add(1);
        
        // Simulate processing time
        if (simulatedProcessingTime_.count() > 0) {
            std::this_thread::sleep_for(simulatedProcessingTime_);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        updatePerformanceMetrics(duration);
    }

    void processAsync() override {
        asyncCallCount_.fetch_add(1);
        processSync();
    }

    void reset() override {
        resetCallCount_.fetch_add(1);
        setState(ExecutionState::INITIALIZED);
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    // Test accessors
    uint32_t getSyncCallCount() const { return syncCallCount_.load(); }
    uint32_t getAsyncCallCount() const { return asyncCallCount_.load(); }
    uint32_t getResetCallCount() const { return resetCallCount_.load(); }
    
    void setShouldThrow(bool shouldThrow) { shouldThrow_.store(shouldThrow); }
    void setSimulatedProcessingTime(std::chrono::microseconds time) { simulatedProcessingTime_ = time; }
};

class ProcessingUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        testUnit = std::make_unique<TestProcessingUnit>("TestUnit");
    }

    void TearDown() override {
        testUnit.reset();
    }

    std::unique_ptr<TestProcessingUnit> testUnit;
};

// Basic ProcessingUnit Tests
TEST_F(ProcessingUnitTest, ConstructionAndBasicProperties) {
    EXPECT_EQ(testUnit->getName(), "TestUnit");
    EXPECT_EQ(testUnit->getState(), ExecutionState::UNINITIALIZED);
    EXPECT_FALSE(testUnit->isInitialized());
    EXPECT_FALSE(testUnit->isRunning());
    EXPECT_FALSE(testUnit->hasError());
    EXPECT_EQ(testUnit->getProcessPriority(), ProcessPriority::NORMAL);
}

TEST_F(ProcessingUnitTest, InitializationAndStateManagement) {
    // Test initialization
    testUnit->initialize();
    EXPECT_TRUE(testUnit->isInitialized());
    EXPECT_EQ(testUnit->getState(), ExecutionState::INITIALIZED);
    
    // Test reset
    testUnit->reset();
    EXPECT_EQ(testUnit->getResetCallCount(), 1);
    EXPECT_EQ(testUnit->getState(), ExecutionState::INITIALIZED);
}

TEST_F(ProcessingUnitTest, ProcessingSyncAndAsync) {
    testUnit->initialize();
    
    // Test sync processing
    testUnit->processSync();
    EXPECT_EQ(testUnit->getSyncCallCount(), 1);
    EXPECT_EQ(testUnit->getState(), ExecutionState::RUNNING);
    
    // Test async processing
    testUnit->processAsync();
    EXPECT_EQ(testUnit->getAsyncCallCount(), 1);
    EXPECT_EQ(testUnit->getSyncCallCount(), 2); // Async calls sync internally
}

TEST_F(ProcessingUnitTest, ErrorHandling) {
    testUnit->initialize();
    testUnit->setShouldThrow(true);
    
    // Processing should throw
    EXPECT_THROW(testUnit->processSync(), std::runtime_error);
}

TEST_F(ProcessingUnitTest, PerformanceMetricsTracking) {
    testUnit->initialize();
    testUnit->setSimulatedProcessingTime(std::chrono::microseconds(100));
    
    // Process a few times to generate metrics
    for (int i = 0; i < 5; ++i) {
        testUnit->processSync();
    }
    
    auto metrics = testUnit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, 5);
    EXPECT_GT(metrics.averageExecutionTime.count(), 0);
    EXPECT_GT(metrics.maxExecutionTime.count(), 0);
    EXPECT_LT(metrics.minExecutionTime.count(), std::chrono::microseconds::max().count());
    
    // Reset metrics
    testUnit->resetPerformanceMetrics();
    metrics = testUnit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, 0);
}

TEST_F(ProcessingUnitTest, PriorityManagement) {
    EXPECT_EQ(testUnit->getProcessPriority(), ProcessPriority::NORMAL);
    
    testUnit->setProcessPriority(ProcessPriority::HIGH);
    EXPECT_EQ(testUnit->getProcessPriority(), ProcessPriority::HIGH);
    
    testUnit->setProcessPriority(ProcessPriority::REAL_TIME);
    EXPECT_EQ(testUnit->getProcessPriority(), ProcessPriority::REAL_TIME);
}

TEST_F(ProcessingUnitTest, ExecutionPeriodManagement) {
    auto defaultPeriod = testUnit->getExecutionPeriod();
    EXPECT_EQ(defaultPeriod, 10ms); // Default from implementation
    
    testUnit->setExecutionPeriod(5ms);
    EXPECT_EQ(testUnit->getExecutionPeriod(), 5ms);
}

// Port System Tests
TEST_F(ProcessingUnitTest, InputPortCreationAndManagement) {
    testUnit->initialize();
    
    // Create input port
    auto* inputPort = testUnit->createInputPort<double>(1, "test_input");
    ASSERT_NE(inputPort, nullptr);
    EXPECT_EQ(inputPort->getId(), 1);
    EXPECT_EQ(inputPort->getName(), "test_input");
    EXPECT_EQ(inputPort->getOwner(), testUnit.get());
    
    // Test port retrieval
    auto* retrievedPort = testUnit->getPort(1);
    EXPECT_EQ(retrievedPort, inputPort);
    
    auto* retrievedByName = testUnit->getPort("test_input");
    EXPECT_EQ(retrievedByName, inputPort);
    
    // Test duplicate ID error
    EXPECT_THROW(testUnit->createInputPort<double>(1, "duplicate_id"), std::runtime_error);
    
    // Test duplicate name error
    EXPECT_THROW(testUnit->createInputPort<double>(2, "test_input"), std::runtime_error);
}

TEST_F(ProcessingUnitTest, OutputPortCreationAndManagement) {
    testUnit->initialize();
    
    // Create output port
    auto* outputPort = testUnit->createOutputPort<int>(10, "test_output");
    ASSERT_NE(outputPort, nullptr);
    EXPECT_EQ(outputPort->getId(), 10);
    EXPECT_EQ(outputPort->getName(), "test_output");
    EXPECT_EQ(outputPort->getOwner(), testUnit.get());
    
    // Test port retrieval
    auto* retrievedPort = testUnit->getPort(10);
    EXPECT_EQ(retrievedPort, outputPort);
    
    auto* retrievedByName = testUnit->getPort("test_output");
    EXPECT_EQ(retrievedByName, outputPort);
}

TEST_F(ProcessingUnitTest, PortConnectionAndDataFlow) {
    testUnit->initialize();
    
    // Create ports
    auto* outputPort = testUnit->createOutputPort<double>(1, "output");
    auto* inputPort = testUnit->createInputPort<double>(2, "input");
    
    // Initially no connection
    EXPECT_FALSE(outputPort->isConnected());
    EXPECT_FALSE(inputPort->hasNewData());
    
    // Connect ports
    outputPort->connect(inputPort);
    EXPECT_TRUE(outputPort->isConnected());
    EXPECT_EQ(outputPort->getConnectionCount(), 1);
    
    // Test data flow
    const double testValue = 42.5;
    outputPort->write(testValue);
    
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_DOUBLE_EQ(inputPort->read(), testValue);
    
    // Clear flag
    inputPort->clearNewDataFlag();
    EXPECT_FALSE(inputPort->hasNewData());
    
    // Disconnect
    outputPort->disconnect(inputPort);
    EXPECT_FALSE(outputPort->isConnected());
    EXPECT_EQ(outputPort->getConnectionCount(), 0);
}

TEST_F(ProcessingUnitTest, PortMultipleConnections) {
    testUnit->initialize();
    
    // Create one output and multiple inputs
    auto* outputPort = testUnit->createOutputPort<int>(1, "output");
    auto* input1 = testUnit->createInputPort<int>(2, "input1");
    auto* input2 = testUnit->createInputPort<int>(3, "input2");
    auto* input3 = testUnit->createInputPort<int>(4, "input3");
    
    // Connect all inputs to output
    outputPort->connect(input1);
    outputPort->connect(input2);
    outputPort->connect(input3);
    
    EXPECT_EQ(outputPort->getConnectionCount(), 3);
    
    // Write data - should reach all inputs
    const int testValue = 123;
    outputPort->write(testValue);
    
    EXPECT_TRUE(input1->hasNewData());
    EXPECT_TRUE(input2->hasNewData());
    EXPECT_TRUE(input3->hasNewData());
    
    EXPECT_EQ(input1->read(), testValue);
    EXPECT_EQ(input2->read(), testValue);
    EXPECT_EQ(input3->read(), testValue);
    
    // Disconnect all
    outputPort->disconnectAll();
    EXPECT_EQ(outputPort->getConnectionCount(), 0);
}

TEST_F(ProcessingUnitTest, PortDataValidation) {
    testUnit->initialize();
    auto* inputPort = testUnit->createInputPort<double>(1, "validated_input");
    
    // Set validation callback - only accept positive values
    inputPort->setValidationCallback([](const double& value) {
        return value > 0.0;
    });
    
    // Test valid data
    inputPort->writeData(10.5);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->getValidMessages(), 1);
    EXPECT_EQ(inputPort->getInvalidMessages(), 0);
    
    // Clear the flag from previous valid write
    inputPort->clearNewDataFlag();
    EXPECT_FALSE(inputPort->hasNewData());
    
    // Test invalid data
    inputPort->writeData(-5.0);
    EXPECT_FALSE(inputPort->hasNewData()); // Should be rejected
    EXPECT_EQ(inputPort->getValidMessages(), 1); // Still 1
    EXPECT_EQ(inputPort->getInvalidMessages(), 1);
    
    EXPECT_EQ(inputPort->getTotalMessages(), 2);
}

TEST_F(ProcessingUnitTest, PortCallbacks) {
    testUnit->initialize();
    auto* inputPort = testUnit->createInputPort<std::string>(1, "callback_input");
    auto* outputPort = testUnit->createOutputPort<std::string>(2, "callback_output");
    
    std::atomic<int> dataCallbackCount{0};
    std::atomic<int> outputCallbackCount{0};
    std::string lastDataReceived;
    std::string lastDataSent;
    
    // Set data callback for input
    inputPort->setDataCallback([&](const std::string& data) {
        dataCallbackCount.fetch_add(1);
        lastDataReceived = data;
    });
    
    // Set output callback
    outputPort->setOutputCallback([&](const std::string& data) {
        outputCallbackCount.fetch_add(1);
        lastDataSent = data;
    });
    
    // Test callbacks
    inputPort->writeData("test_data");
    outputPort->write("output_data");
    
    EXPECT_EQ(dataCallbackCount.load(), 1);
    EXPECT_EQ(outputCallbackCount.load(), 1);
    EXPECT_EQ(lastDataReceived, "test_data");
    EXPECT_EQ(lastDataSent, "output_data");
}

TEST_F(ProcessingUnitTest, PortStatistics) {
    testUnit->initialize();
    auto* inputPort = testUnit->createInputPort<int>(1, "stats_input");
    auto* outputPort = testUnit->createOutputPort<int>(2, "stats_output");
    
    // Initial statistics
    EXPECT_EQ(inputPort->getTotalMessages(), 0);
    EXPECT_EQ(inputPort->getValidMessages(), 0);
    EXPECT_EQ(inputPort->getInvalidMessages(), 0);
    EXPECT_EQ(outputPort->getTotalMessages(), 0);
    
    // Generate some traffic
    inputPort->writeData(1);
    inputPort->writeData(2);
    inputPort->writeData(3);
    
    outputPort->write(10);
    outputPort->write(20);
    
    EXPECT_EQ(inputPort->getTotalMessages(), 3);
    EXPECT_EQ(inputPort->getValidMessages(), 3);
    EXPECT_EQ(inputPort->getInvalidMessages(), 0);
    EXPECT_EQ(outputPort->getTotalMessages(), 2);
}

TEST_F(ProcessingUnitTest, AllPortsRetrieval) {
    testUnit->initialize();
    
    // Create multiple ports
    testUnit->createInputPort<double>(1, "input1");
    testUnit->createInputPort<int>(2, "input2");
    testUnit->createOutputPort<float>(3, "output1");
    testUnit->createOutputPort<std::string>(4, "output2");
    
    auto allPorts = testUnit->getAllPorts();
    EXPECT_EQ(allPorts.size(), 4);
    
    // Verify all ports are present
    std::set<int> portIds;
    for (const auto* port : allPorts) {
        portIds.insert(port->getId());
    }
    
    std::set<int> expectedIds{1, 2, 3, 4};
    EXPECT_EQ(portIds, expectedIds);
}

// Thread Safety Tests
TEST_F(ProcessingUnitTest, ThreadSafePortAccess) {
    testUnit->initialize();
    auto* outputPort = testUnit->createOutputPort<int>(1, "thread_output");
    auto* inputPort = testUnit->createInputPort<int>(2, "thread_input");
    outputPort->connect(inputPort);
    
    const int numThreads = 4;
    const int operationsPerThread = 50;
    std::vector<std::thread> threads;
    std::atomic<bool> testRunning{true};
    std::atomic<int> readOperations{0};
    std::atomic<int> writeOperations{0};
    
    // Start writer threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < operationsPerThread; ++j) {
                int value = i * operationsPerThread + j;
                outputPort->write(value);
                writeOperations.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }
    
    // Start reader threads (test concurrent read access)
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([&]() {
            while (testRunning.load()) {
                if (inputPort->hasNewData()) {
                    inputPort->read(); // Test thread-safe reading
                    inputPort->clearNewDataFlag();
                    readOperations.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(5));
            }
        });
    }
    
    // Wait for writers to complete
    for (int i = 0; i < numThreads; ++i) {
        threads[i].join();
    }
    
    // Allow readers to finish current operations
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    testRunning.store(false);
    
    // Wait for readers to complete
    for (int i = numThreads; i < threads.size(); ++i) {
        threads[i].join();
    }
    
    // Verify operations completed without crashes (thread safety)
    EXPECT_EQ(writeOperations.load(), numThreads * operationsPerThread);
    EXPECT_GT(readOperations.load(), 0); // Some reads should have occurred
    EXPECT_LE(readOperations.load(), writeOperations.load()); // Can't read more than written
    
    // Test that port operations are still functional after concurrent access
    outputPort->write(12345);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 12345);
}

TEST_F(ProcessingUnitTest, ThreadSafeMetricsUpdate) {
    testUnit->initialize();
    testUnit->setSimulatedProcessingTime(std::chrono::microseconds(1));
    
    const int numThreads = 8;
    const int iterationsPerThread = 50;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                testUnit->processSync();
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto metrics = testUnit->getPerformanceMetrics();
    EXPECT_EQ(metrics.executionCount, numThreads * iterationsPerThread);
    EXPECT_GT(metrics.averageExecutionTime.count(), 0);
}

// Performance Tests
TEST_F(ProcessingUnitTest, PortPerformance) {
    testUnit->initialize();
    auto* outputPort = testUnit->createOutputPort<double>(1, "perf_output");
    auto* inputPort = testUnit->createInputPort<double>(2, "perf_input");
    outputPort->connect(inputPort);
    
    const int numMessages = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numMessages; ++i) {
        outputPort->write(static_cast<double>(i));
        if (inputPort->hasNewData()) {
            inputPort->read();
            inputPort->clearNewDataFlag();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Port performance: " << numMessages << " messages in " 
              << duration.count() << " μs" << std::endl;
    std::cout << "Throughput: " << (numMessages * 1000000.0 / duration.count()) 
              << " messages/sec" << std::endl;
    
    // Should handle at least 1M messages per second
    EXPECT_LT(duration.count(), numMessages); // Less than 1μs per message
}

// Edge Cases and Error Conditions
TEST_F(ProcessingUnitTest, InvalidPortAccess) {
    testUnit->initialize();
    
    // Test non-existent port access
    EXPECT_EQ(testUnit->getPort(999), nullptr);
    EXPECT_EQ(testUnit->getPort("non_existent"), nullptr);
}

TEST_F(ProcessingUnitTest, EmptyNameHandling) {
    EXPECT_THROW(TestProcessingUnit(""), std::invalid_argument);
}

TEST_F(ProcessingUnitTest, PortDataTypeInformation) {
    testUnit->initialize();
    
    auto* doublePort = testUnit->createInputPort<double>(1, "double_port");
    auto* intPort = testUnit->createInputPort<int>(2, "int_port");
    auto* stringPort = testUnit->createInputPort<std::string>(3, "string_port");
    
    // Test data type name retrieval
    EXPECT_FALSE(doublePort->getDataTypeName().empty());
    EXPECT_FALSE(intPort->getDataTypeName().empty());
    EXPECT_FALSE(stringPort->getDataTypeName().empty());
    
    // Different types should have different names
    EXPECT_NE(doublePort->getDataTypeName(), intPort->getDataTypeName());
    EXPECT_NE(doublePort->getDataTypeName(), stringPort->getDataTypeName());
}

// Main function for Google Test
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 