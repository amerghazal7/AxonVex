/**
 * @file system_example_simple.cpp
 * @brief Simplified AxonVex System Management Example
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * This simplified example demonstrates:
 * - Basic ProcessingUnit implementation  
 * - Processing unit lifecycle
 * - Simple data processing pipeline
 */

#include <axonvex/axonvex.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <random>

using namespace axonvex::core;

using namespace axonvex;

// =================================================================
// SIMPLE PROCESSING UNITS
// =================================================================

/**
 * @brief Simple Data Generator ProcessingUnit
 */
class SimpleDataGenerator : public ProcessingUnit {
private:
    std::atomic<uint64_t> generatedCount_{0};
    std::mt19937 generator_;
    std::uniform_real_distribution<> distribution_;

public:
    explicit SimpleDataGenerator(const std::string& name)
        : ProcessingUnit(name)
        , generator_(std::chrono::steady_clock::now().time_since_epoch().count())
        , distribution_(-10.0, 10.0) {
    }
    
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Initialized: " << getName() << std::endl;
    }
    
    void processSync() override {
        // Start the timer
        executionTimer_.start();

        setState(ExecutionState::RUNNING);
        
        // Generate random data
        double value = distribution_(generator_);
        generatedCount_.fetch_add(1);
        
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        if (generatedCount_.load() % 100 == 0) {
            std::cout << getName() << " generated " << generatedCount_.load() << " values" << std::endl;
        }

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void processAsync() override {
        // Start the timer
        executionTimer_.start();
        
        setState(ExecutionState::RUNNING);
        
        // Generate random data
        double value = distribution_(generator_);
        generatedCount_.fetch_add(1);
        
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        if (generatedCount_.load() % 100 == 0) {
            std::cout << getName() << " generated " << generatedCount_.load() << " values (async)" << std::endl;
        }

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateAsyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void reset() override {
        generatedCount_.store(0);
        setState(ExecutionState::INITIALIZED);
        std::cout << "Reset: " << getName() << std::endl;
    }
    
    std::string getTypeDescription() override {
        return "SimpleDataGenerator";
    }
    
    uint64_t getGeneratedCount() const { return generatedCount_.load(); }
    
    // Expose performance metrics
    axonvex::core::ProcessingUnit::PerformanceMetrics getMetrics() const {
        return getPerformanceMetrics();
    }
    
    void finalize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Finalized: " << getName() << " (Generated: " << generatedCount_.load() << ")" << std::endl;
    }
};

/**
 * @brief Simple Data Processor ProcessingUnit
 */
class SimpleDataProcessor : public ProcessingUnit {
private:
    std::atomic<uint64_t> processedCount_{0};
    double runningSum_{0.0};
    mutable std::mutex sumMutex_;

public:
    explicit SimpleDataProcessor(const std::string& name)
        : ProcessingUnit(name) {
    }
    
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Initialized: " << getName() << std::endl;
    }
    
    void processSync() override {
        // Start the timer
        executionTimer_.start();
        
        setState(ExecutionState::RUNNING);
        
        // Simulate processing some data
        {
            std::lock_guard<std::mutex> lock(sumMutex_);
            runningSum_ += processedCount_.load() * 0.1;
        }
        processedCount_.fetch_add(1);
        
        // Simulate processing time
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        
        if (processedCount_.load() % 150 == 0) {
            std::cout << getName() << " processed " << processedCount_.load() << " values" << std::endl;
        }

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void processAsync() override {
        // Start the timer
        executionTimer_.start();
        
        setState(ExecutionState::RUNNING);
        
        // Simulate processing some data
        {
            std::lock_guard<std::mutex> lock(sumMutex_);
            runningSum_ += processedCount_.load() * 0.1;
        }
        processedCount_.fetch_add(1);
        
        // Simulate processing time
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        
        if (processedCount_.load() % 150 == 0) {
            std::cout << getName() << " processed " << processedCount_.load() << " values (async)" << std::endl;
        }

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateAsyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void reset() override {
        processedCount_.store(0);
        {
            std::lock_guard<std::mutex> lock(sumMutex_);
            runningSum_ = 0.0;
        }
        setState(ExecutionState::INITIALIZED);
        std::cout << "Reset: " << getName() << std::endl;
    }
    
    std::string getTypeDescription() override {
        return "SimpleDataProcessor";
    }
    
    uint64_t getProcessedCount() const { return processedCount_.load(); }
    
    double getRunningSum() const { 
        std::lock_guard<std::mutex> lock(sumMutex_);
        return runningSum_; 
    }
    
    void finalize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Finalized: " << getName() << " (Processed: " << processedCount_.load() << ")" << std::endl;
    }
};

/**
 * @brief Simple Monitor ProcessingUnit
 */
class SimpleMonitor : public ProcessingUnit {
private:
    std::atomic<uint64_t> monitoringCycles_{0};

public:
    explicit SimpleMonitor(const std::string& name)
        : ProcessingUnit(name) {
    }
    
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Initialized: " << getName() << std::endl;
    }
    
    void processSync() override {
        // Start the timer
        executionTimer_.start();
        
        setState(ExecutionState::RUNNING);
        
        monitoringCycles_.fetch_add(1);
        
        // Periodic monitoring output
        if (monitoringCycles_.load() % 50 == 0) {
            std::cout << getName() << " monitoring cycle: " << monitoringCycles_.load() << std::endl;
        }
        
        // Simulate monitoring time
        std::this_thread::sleep_for(std::chrono::microseconds(25));

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void processAsync() override {
        // Start the timer
        executionTimer_.start();
        
        setState(ExecutionState::RUNNING);
        
        monitoringCycles_.fetch_add(1);
        
        // Periodic monitoring output
        if (monitoringCycles_.load() % 50 == 0) {
            std::cout << getName() << " monitoring cycle: " << monitoringCycles_.load() << " (async)" << std::endl;
        }
        
        // Simulate monitoring time
        std::this_thread::sleep_for(std::chrono::microseconds(25));

        // Stop the timer and update metrics
        executionTimer_.stop();
        updateAsyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }
    
    void reset() override {
        monitoringCycles_.store(0);
        setState(ExecutionState::INITIALIZED);
        std::cout << "Reset: " << getName() << std::endl;
    }
    
    std::string getTypeDescription() override {
        return "SimpleMonitor";
    }
    
    uint64_t getMonitoringCycles() const { return monitoringCycles_.load(); }
    
    void finalize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "Finalized: " << getName() << " (Cycles: " << monitoringCycles_.load() << ")" << std::endl;
    }
};

// =================================================================
// DEMONSTRATION FUNCTIONS  
// =================================================================

/**
 * @brief Demonstrate basic processing unit lifecycle
 */
void demonstrateBasicLifecycle() {
    std::cout << "\n=== Basic ProcessingUnit Lifecycle ===\n";
    
    auto generator = std::make_unique<SimpleDataGenerator>("DataGen");
    
    std::cout << "Initial state: " << static_cast<int>(generator->getState()) << std::endl;
    
    // Initialize
    generator->initialize();
    std::cout << "After initialize: " << static_cast<int>(generator->getState()) << std::endl;
    
    // Process for a while
    std::cout << "Processing for 2 seconds..." << std::endl;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        generator->processSync();
    }
    
    // Reset and finalize
    generator->reset();
    generator->finalize();
    
    std::cout << "Lifecycle demonstration completed!" << std::endl;
}

/**
 * @brief Demonstrate multiple processing units working together
 */  
void demonstrateMultipleUnits() {
    std::cout << "\n=== Multiple ProcessingUnits Working Together ===\n";
    
    // Create processing units
    auto generator = std::make_unique<SimpleDataGenerator>("Generator");
    auto processor = std::make_unique<SimpleDataProcessor>("Processor"); 
    auto monitor = std::make_unique<SimpleMonitor>("Monitor");
    
    // Store pointers before moving
    auto* genPtr = generator.get();
    auto* procPtr = processor.get();
    auto* monPtr = monitor.get();
    
    // Initialize all units
    generator->initialize();
    processor->initialize();
    monitor->initialize();
    
    std::cout << "Running all units for 3 seconds..." << std::endl;
    
    // Run in separate threads to simulate concurrent processing
    std::atomic<bool> shouldStop{false};
    
    std::thread genThread([genPtr, &shouldStop]() {
        while (!shouldStop.load()) {
            genPtr->processSync();
        }
    });
    
    std::thread procThread([procPtr, &shouldStop]() {
        while (!shouldStop.load()) {
            procPtr->processSync();
        }
    });
    
    std::thread monThread([monPtr, &shouldStop]() {
        while (!shouldStop.load()) {
            monPtr->processSync();
        }
    });
    
    // Let them run for 3 seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));
    shouldStop.store(true);
    
    // Wait for threads to finish
    genThread.join();
    procThread.join();
    monThread.join();
    
    // Display final statistics
    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "Generator: " << genPtr->getGeneratedCount() << " values generated\n";
    std::cout << "Processor: " << procPtr->getProcessedCount() << " values processed\n"; 
    std::cout << "          Running sum: " << procPtr->getRunningSum() << "\n";
    std::cout << "Monitor: " << monPtr->getMonitoringCycles() << " monitoring cycles\n";
    
    // Finalize all units
    generator->finalize();
    processor->finalize();
    monitor->finalize();
    
    std::cout << "Multiple units demonstration completed!" << std::endl;
}

/**
 * @brief Demonstrate processing unit state transitions
 */
void demonstrateStateTransitions() {
    std::cout << "\n=== ProcessingUnit State Transitions ===\n";
    
    auto unit = std::make_unique<SimpleDataProcessor>("StateDemo");
    
    auto printState = [&](const std::string& phase) {
        std::cout << phase << " - State: " << static_cast<int>(unit->getState());
        std::cout << ", Running: " << (unit->isRunning() ? "Yes" : "No");  
        std::cout << ", Initialized: " << (unit->isInitialized() ? "Yes" : "No") << std::endl;
    };
    
    printState("Created");
    
    unit->initialize();
    printState("Initialized");
    
    // Process a few times
    for (int i = 0; i < 5; ++i) {
        unit->processSync();
    }
    printState("After Processing");
    
    unit->reset();
    printState("After Reset");
    
    unit->finalize();
    printState("After Finalize");
    
    std::cout << "State transitions demonstration completed!" << std::endl;
}

/**
 * @brief Demonstrate performance characteristics
 */
void demonstratePerformance() {
    std::cout << "\n=== Performance Characteristics ===\n";
    
    auto generator = std::make_unique<SimpleDataGenerator>("PerfGen");
    generator->initialize();
    
    const int iterations = 1000;
    
    // Measure processing time
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        generator->processSync();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Get and display performance metrics from the unit
    axonvex::core::ProcessingUnit::PerformanceMetrics metrics = generator->getMetrics();
    
    std::cout << "Performance Results (from internal timer):\n";
    std::cout << "  Iterations: " << metrics.executionCount << "\n";
    std::cout << "  Total Time (sum of samples): " << (metrics.averageExecutionTime.count() * metrics.executionCount) << " µs\n";
    std::cout << "  Average per iteration: " << metrics.averageExecutionTime.count() << " µs\n";
    std::cout << "  Min Time: " << metrics.minExecutionTime.count() << " µs\n";
    std::cout << "  Max Time: " << metrics.maxExecutionTime.count() << " µs\n";
    
    generator->finalize();
    
    std::cout << "Performance demonstration completed!" << std::endl;
}

// =================================================================
// MAIN DEMONSTRATION
// =================================================================

int main() {
    try {
        std::cout << "🚀 AxonVex Simple System Management Example\n";
        std::cout << "==========================================\n";
        
        // Run all demonstrations
        demonstrateBasicLifecycle();
        demonstrateMultipleUnits();
        demonstrateStateTransitions();
        demonstratePerformance();
        
        std::cout << "\n✅ All demonstrations completed successfully!\n";
        std::cout << "\nKey Concepts Demonstrated:\n";
        std::cout << "  ✅ ProcessingUnit lifecycle (initialize → process → reset → finalize)\n";
        std::cout << "  ✅ State management and transitions\n";
        std::cout << "  ✅ Concurrent processing unit execution\n";
        std::cout << "  ✅ Pure virtual method implementation (processAsync, reset)\n";
        std::cout << "  ✅ Performance measurement and statistics\n";
        std::cout << "  ✅ Thread-safe operations\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Example failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Example failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
} 