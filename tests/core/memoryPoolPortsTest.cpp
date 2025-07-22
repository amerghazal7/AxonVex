/**
 * @file memoryPoolPortsTest.cpp
 * @brief Tests for MemoryPool integration with the port system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/ports.hpp>
#include <axonvex/core/processingUnit.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace axonvex::core;

class MemoryPoolPortsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock processing unit for port ownership
        processingUnit = std::make_unique<MockProcessingUnit>();
    }

    void TearDown() override {
        processingUnit.reset();
    }

    class MockProcessingUnit : public ProcessingUnit {
    public:
        MockProcessingUnit() : ProcessingUnit("MockProcessingUnit") {}
        ~MockProcessingUnit() override = default;

        void processSync() override {}
        void processAsync() override {}
        void finalize() override { setState(ExecutionState::INITIALIZED); }
        void initialize() override {}
        void reset() override { setState(ExecutionState::INITIALIZED); }
        std::string getTypeDescription() override { return "MockProcessingUnit"; }
    };

    std::unique_ptr<MockProcessingUnit> processingUnit;
};

// Test basic MemoryPool functionality with InputPort
TEST_F(MemoryPoolPortsTest, InputPortMemoryPoolBasics) {
    auto inputPort = std::make_unique<InputPort<int>>(1, "test_input", processingUnit.get());
    
    // Initially not thread-safe
    EXPECT_FALSE(inputPort->isThreadSafe());
    EXPECT_EQ(inputPort->getMemoryPoolSize(), MemoryPool<int>::DEFAULT_POOL_SIZE);
    
    // Enable thread safety - should create MemoryPool
    inputPort->setThreadSafe(true);
    EXPECT_TRUE(inputPort->isThreadSafe());
    
    // Test data storage with MemoryPool
    inputPort->writeData(42);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 42);
    
    // Test custom pool size
    inputPort->setMemoryPoolSize(256);
    EXPECT_EQ(inputPort->getMemoryPoolSize(), 256);
    
    // Disable thread safety - should cleanup MemoryPool
    inputPort->setThreadSafe(false);
    EXPECT_FALSE(inputPort->isThreadSafe());
}

// Test MemoryPool with OutputPort
TEST_F(MemoryPoolPortsTest, OutputPortMemoryPoolBasics) {
    auto outputPort = std::make_unique<OutputPort<double>>(2, "test_output", processingUnit.get());
    auto inputPort = std::make_unique<InputPort<double>>(3, "test_input", processingUnit.get());
    
    // Connect ports
    outputPort->connect(inputPort.get());
    
    // Enable thread safety for both ports
    outputPort->setThreadSafe(true);
    inputPort->setThreadSafe(true);
    
    // Test data flow
    outputPort->write(3.14159);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_DOUBLE_EQ(inputPort->read(), 3.14159);
}

// Test MemoryPool with AsyncInputPort
TEST_F(MemoryPoolPortsTest, AsyncInputPortMemoryPoolBasics) {
    auto asyncInput = std::make_unique<AsyncInputPort<std::string>>(4, "async_input", processingUnit.get());
    
    // Enable thread safety
    asyncInput->setThreadSafe(true);
    EXPECT_TRUE(asyncInput->isThreadSafe());
    
    // Test async data operations
    asyncInput->update("Hello MemoryPool!");
    EXPECT_TRUE(asyncInput->wasUpdated());
    
    std::string result;
    asyncInput->read(result);
    EXPECT_EQ(result, "Hello MemoryPool!");
    EXPECT_FALSE(asyncInput->wasUpdated());
}

// Test MemoryPool with AsyncOutputPort
TEST_F(MemoryPoolPortsTest, AsyncOutputPortMemoryPoolBasics) {
    auto asyncOutput = std::make_unique<AsyncOutputPort<int>>(5, "async_output", processingUnit.get());
    auto asyncInput = std::make_unique<AsyncInputPort<int>>(6, "async_input", processingUnit.get());
    
    // Connect async ports
    asyncOutput->connect(asyncInput.get());
    
    // Enable thread safety
    asyncOutput->setThreadSafe(true);
    asyncInput->setThreadSafe(true);
    
    // Test async data flow
    asyncOutput->write(999);
    EXPECT_TRUE(asyncInput->wasUpdated());
    EXPECT_EQ(asyncInput->read(), 999);
}

// Test thread safety performance with MemoryPool vs mutex
TEST_F(MemoryPoolPortsTest, ThreadSafetyPerformanceComparison) {
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 1000;
    
    auto inputPort = std::make_unique<InputPort<int>>(7, "perf_test", processingUnit.get());
    inputPort->setThreadSafe(true);
    inputPort->setMemoryPoolSize(512); // Larger pool for performance test
    
    std::atomic<int> writeCounter{0};
    std::atomic<int> readCounter{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    
    // Create writer threads
    for (int t = 0; t < NUM_THREADS / 2; ++t) {
        threads.emplace_back([&inputPort, &writeCounter, OPERATIONS_PER_THREAD]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                inputPort->writeData(writeCounter.fetch_add(1));
                std::this_thread::yield();
            }
        });
    }
    
    // Create reader threads
    for (int t = 0; t < NUM_THREADS / 2; ++t) {
        threads.emplace_back([&inputPort, &readCounter, OPERATIONS_PER_THREAD]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                if (inputPort->hasNewData()) {
                    inputPort->read();
                    readCounter.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Verify operations completed
    EXPECT_EQ(writeCounter.load(), NUM_THREADS * OPERATIONS_PER_THREAD / 2);
    EXPECT_GT(readCounter.load(), 0); // Some reads should have happened
    
    std::cout << "MemoryPool thread safety test completed in " << duration.count() << " microseconds\n";
    std::cout << "Total writes: " << writeCounter.load() << ", Total reads: " << readCounter.load() << "\n";
}

// Test MemoryPool exhaustion handling
TEST_F(MemoryPoolPortsTest, MemoryPoolExhaustionHandling) {
    auto inputPort = std::make_unique<InputPort<int>>(8, "exhaustion_test", processingUnit.get());
    
    // Set small pool size to test exhaustion
    inputPort->setMemoryPoolSize(16);
    inputPort->setThreadSafe(true);
    
    // Fill the pool beyond capacity
    for (int i = 0; i < 100; ++i) {
        inputPort->writeData(i);
        // Should fallback to mutex-based storage when pool is exhausted
    }
    
    // Verify data is still accessible (fallback mechanism)
    EXPECT_TRUE(inputPort->hasNewData());
    int lastValue = inputPort->read();
    EXPECT_EQ(lastValue, 99);
}

// Test reset functionality with MemoryPool
TEST_F(MemoryPoolPortsTest, ResetWithMemoryPool) {
    auto inputPort = std::make_unique<InputPort<int>>(9, "reset_test", processingUnit.get());
    inputPort->setThreadSafe(true);
    
    // Add some data
    inputPort->writeData(123);
    EXPECT_TRUE(inputPort->hasNewData());
    
    // Reset should clear MemoryPool data
    inputPort->reset();
    EXPECT_FALSE(inputPort->hasNewData());
    
    // Verify port is still functional after reset
    inputPort->writeData(456);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 456);
}

// Test MemoryPool with complex data types
TEST_F(MemoryPoolPortsTest, ComplexDataTypes) {
    struct ComplexData {
        int id;
        std::string name;
        std::vector<double> values;
        
        ComplexData() : id(0), name(""), values() {}
        
        ComplexData(int i, const std::string& n, std::vector<double> v)
            : id(i), name(n), values(std::move(v)) {}
        
        bool operator==(const ComplexData& other) const {
            return id == other.id && name == other.name && values == other.values;
        }
    };
    
    auto inputPort = std::make_unique<InputPort<ComplexData>>(10, "complex_test", processingUnit.get());
    inputPort->setThreadSafe(true);
    
    ComplexData testData{42, "TestObject", {1.0, 2.5, 3.14159}};
    
    inputPort->writeData(testData);
    EXPECT_TRUE(inputPort->hasNewData());
    
    ComplexData result = inputPort->read();
    EXPECT_EQ(result, testData);
}

// Test MemoryPool statistics integration
TEST_F(MemoryPoolPortsTest, MemoryPoolStatistics) {
    auto inputPort = std::make_unique<InputPort<int>>(11, "stats_test", processingUnit.get());
    inputPort->setThreadSafe(true);
    
    // Perform several operations
    for (int i = 0; i < 10; ++i) {
        inputPort->writeData(i);
    }
    
    // Verify port statistics are working
    EXPECT_EQ(inputPort->getTotalMessages(), 10);
    EXPECT_EQ(inputPort->getValidMessages(), 10);
    EXPECT_EQ(inputPort->getInvalidMessages(), 0);
}

// Test concurrent access patterns
TEST_F(MemoryPoolPortsTest, ConcurrentAccessPatterns) {
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int MESSAGES_PER_PRODUCER = 100;
    
    auto outputPort = std::make_unique<OutputPort<int>>(12, "producer", processingUnit.get());
    auto inputPort = std::make_unique<InputPort<int>>(13, "consumer", processingUnit.get());
    
    outputPort->connect(inputPort.get());
    outputPort->setThreadSafe(true);
    inputPort->setThreadSafe(true);
    inputPort->setMemoryPoolSize(256);
    
    std::atomic<int> totalProduced{0};
    std::atomic<int> totalConsumed{0};
    std::vector<std::thread> threads;
    
    // Producer threads
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        threads.emplace_back([&outputPort, &totalProduced, MESSAGES_PER_PRODUCER, p]() {
            for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i) {
                int value = p * MESSAGES_PER_PRODUCER + i;
                outputPort->write(value);
                totalProduced.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }
    
    // Consumer threads
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        threads.emplace_back([&inputPort, &totalConsumed]() {
            while (totalConsumed.load() < NUM_PRODUCERS * MESSAGES_PER_PRODUCER) {
                if (inputPort->hasNewData()) {
                    inputPort->read();
                    totalConsumed.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(totalProduced.load(), NUM_PRODUCERS * MESSAGES_PER_PRODUCER);
    EXPECT_EQ(totalConsumed.load(), NUM_PRODUCERS * MESSAGES_PER_PRODUCER);
} 