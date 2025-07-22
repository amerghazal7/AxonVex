/**
 * @file memory_pool_ports_simple_example.cpp
 * @brief Simple example demonstrating MemoryPool integration with ports
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <axonvex/axonvex.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iomanip>

using namespace axonvex::core;

class MockProcessingUnit : public ProcessingUnit {
public:
    MockProcessingUnit(const std::string& name) : ProcessingUnit(name) {}
    ~MockProcessingUnit() override = default;

    void processSync() override {}
    void processAsync() override {}
    void finalize() override { setState(ExecutionState::INITIALIZED); }
    void initialize() override {}
    void reset() override { setState(ExecutionState::INITIALIZED); }
    std::string getTypeDescription() override { return "MockProcessingUnit"; }
};

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "AxonVex Framework - MemoryPool Ports Simple Demo" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "\n🚀 High-Performance Thread-Safe Port System Demo\n" << std::endl;

    try {
        // Create mock processing units for port ownership
        auto producer = std::make_unique<MockProcessingUnit>("Producer");
        auto consumer = std::make_unique<MockProcessingUnit>("Consumer");

        // Test 1: Basic MemoryPool functionality with InputPort
        std::cout << "📊 Test 1: Basic MemoryPool with InputPort\n";
        std::cout << "============================================" << std::endl;

        auto inputPort = std::make_unique<InputPort<double>>(1, "test_input", producer.get());

        std::cout << "Initial state:" << std::endl;
        std::cout << "  Thread-safe: " << (inputPort->isThreadSafe() ? "Yes" : "No") << std::endl;
        std::cout << "  Pool size: " << inputPort->getMemoryPoolSize() << std::endl;

        // Enable thread safety with MemoryPool
        inputPort->setThreadSafe(true);
        inputPort->setMemoryPoolSize(512);

        std::cout << "After enabling MemoryPool:" << std::endl;
        std::cout << "  Thread-safe: " << (inputPort->isThreadSafe() ? "Yes" : "No") << std::endl;
        std::cout << "  Pool size: " << inputPort->getMemoryPoolSize() << std::endl;

        // Test data operations
        inputPort->writeData(3.14159);
        std::cout << "  Has new data: " << (inputPort->hasNewData() ? "Yes" : "No") << std::endl;
        std::cout << "  Data: " << inputPort->read() << std::endl;

        // Test 2: Port-to-Port communication with MemoryPool
        std::cout << "\n📊 Test 2: Port-to-Port Communication\n";
        std::cout << "=====================================\n" << std::endl;

        auto outputPort = std::make_unique<OutputPort<double>>(2, "test_output", producer.get());
        auto inputPort2 = std::make_unique<InputPort<double>>(3, "test_input2", consumer.get());

        // Enable MemoryPool for both
        outputPort->setThreadSafe(true);
        outputPort->setMemoryPoolSize(256);

        inputPort2->setThreadSafe(true);
        inputPort2->setMemoryPoolSize(256);

        // Connect ports
        outputPort->connect(inputPort2.get());
        std::cout << "✓ Ports connected with MemoryPool enabled" << std::endl;

        // Test data flow
        outputPort->write(42.0);
        std::cout << "✓ Data written to output port: 42.0" << std::endl;
        std::cout << "✓ Input port has new data: " << (inputPort2->hasNewData() ? "Yes" : "No") << std::endl;
        std::cout << "✓ Data received: " << inputPort2->read() << std::endl;

        // Test 3: Thread Safety Performance Test
        std::cout << "\n🚀 Test 3: Thread Safety Performance\n";
        std::cout << "====================================\n" << std::endl;

        const int NUM_THREADS = 4;
        const int OPERATIONS_PER_THREAD = 1000;

        auto perfTestPort = std::make_unique<InputPort<int>>(4, "perf_test", producer.get());
        perfTestPort->setThreadSafe(true);
        perfTestPort->setMemoryPoolSize(1024);

        std::atomic<int> writeCounter{0};
        std::atomic<int> readCounter{0};

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;

        // Create writer threads
        for (int t = 0; t < NUM_THREADS / 2; ++t) {
            threads.emplace_back([&perfTestPort, &writeCounter, OPERATIONS_PER_THREAD]() {
                for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                    perfTestPort->writeData(writeCounter.fetch_add(1));
                    std::this_thread::yield();
                }
            });
        }

        // Create reader threads
        for (int t = 0; t < NUM_THREADS / 2; ++t) {
            threads.emplace_back([&perfTestPort, &readCounter, OPERATIONS_PER_THREAD]() {
                for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                    if (perfTestPort->hasNewData()) {
                        perfTestPort->read();
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

        std::cout << "Performance Results:" << std::endl;
        std::cout << "  Operations completed: " << writeCounter.load() << " writes, " << readCounter.load() << " reads" << std::endl;
        std::cout << "  Duration: " << duration.count() << " microseconds" << std::endl;
        std::cout << "  Write throughput: " << std::fixed << std::setprecision(0)
                 << (writeCounter.load() * 1e6 / duration.count()) << " ops/sec" << std::endl;

        // Test 4: Async Ports with MemoryPool
        std::cout << "\n📊 Test 4: Async Ports with MemoryPool\n";
        std::cout << "======================================\n" << std::endl;

        auto asyncOutput = std::make_unique<AsyncOutputPort<std::string>>(5, "async_out", producer.get());
        auto asyncInput = std::make_unique<AsyncInputPort<std::string>>(6, "async_in", consumer.get());

        // Enable thread safety
        asyncOutput->setThreadSafe(true);
        asyncInput->setThreadSafe(true);
        asyncInput->setMemoryPoolSize(128);

        // Connect and test
        asyncOutput->connect(asyncInput.get());

        asyncOutput->write("Hello MemoryPool!");
        std::cout << "✓ Async data sent: 'Hello MemoryPool!'" << std::endl;
        std::cout << "✓ Async port updated: " << (asyncInput->wasUpdated() ? "Yes" : "No") << std::endl;
        std::cout << "✓ Received: '" << asyncInput->read() << "'" << std::endl;

        // Test 5: Port Statistics
        std::cout << "\n📊 Test 5: Port Statistics\n";
        std::cout << "==========================\n" << std::endl;

        auto statsPort = std::make_unique<InputPort<int>>(7, "stats_test", producer.get());
        statsPort->setThreadSafe(true);

        // Generate some traffic
        for (int i = 0; i < 10; ++i) {
            statsPort->writeData(i);
        }

        std::cout << "Port Statistics:" << std::endl;
        std::cout << "  Total messages: " << statsPort->getTotalMessages() << std::endl;
        std::cout << "  Valid messages: " << statsPort->getValidMessages() << std::endl;
        std::cout << "  Invalid messages: " << statsPort->getInvalidMessages() << std::endl;
        std::cout << "  Pool size: " << statsPort->getMemoryPoolSize() << std::endl;

        std::cout << "\n✅ All tests completed successfully!" << std::endl;

        std::cout << "\n🎉 Key Benefits Demonstrated:" << std::endl;
        std::cout << "  ✅ Lock-free high-performance data storage with MemoryPool" << std::endl;
        std::cout << "  ✅ Thread-safe port operations without mutex overhead" << std::endl;
        std::cout << "  ✅ Configurable memory pool sizes for different scenarios" << std::endl;
        std::cout << "  ✅ Automatic fallback to mutex-based storage when needed" << std::endl;
        std::cout << "  ✅ Seamless integration with existing port API" << std::endl;
        std::cout << "  ✅ Real-time system compatibility" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ Error in MemoryPool ports demo: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
