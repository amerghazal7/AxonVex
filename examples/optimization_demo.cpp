/**
 * @file optimization_demo.cpp
 * @brief Demonstration of AxonVex Framework optimizations
 *
 * This example showcases the key optimizations implemented:
 * 1. Common PerformanceStatistics interface across components
 * 2. Standardized ErrorHandler for consistent error reporting
 * 3. Shared utilities for common patterns (nextPowerOf2, atomic operations)
 * 4. Enhanced MemoryPool utilization
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

// Include our optimized headers
#include <axonvex_core/coreUtilities.hpp>
#include <axonvex_core/errorHandler.hpp>
#include <axonvex_core/utils/containers/memoryPool.hpp>
#include <axonvex_core/performanceStatistics.hpp>
// Note: CircularBuffer and ThreadSafeQueue updates are in progress

using namespace axonvex::core;
using axonvex::utils::containers::MemoryPool;

/**
 * @brief Example component demonstrating unified statistics collection
 */
class OptimizedProcessor : public PerformanceStatisticsBase<OptimizedProcessor> {
  private:
    std::string name_;
    ErrorHandler error_handler_;
    ThroughputStatistics throughput_stats_;
    MemoryStatistics memory_stats_;

  public:
    explicit OptimizedProcessor(const std::string& name) : name_(name), error_handler_(name) {

        // Set up error handling callbacks
        error_handler_.setErrorCallback([this](const ErrorInfo& error) {
            std::cout << "Error in " << name_ << ": " << error.getFormattedMessage() << std::endl;
        });

        error_handler_.setRecoveryCallback([this](const ErrorInfo& error) -> bool {
            std::cout << "Attempting recovery for: " << error.message << std::endl;
            // Simulate recovery logic
            return error.severity < ErrorSeverity::CRITICAL;
        });

        error_handler_.setAutoRecoveryEnabled(true);
        error_handler_.setMaxRecoveryAttempts(3);
    }

    void processData(const std::vector<int>& data) {
        // Use RAII error context for automatic error reporting
        ErrorContext context(error_handler_, "processData");
        context.addContext("data_size", std::to_string(data.size()));

        auto start_time = TimingUtils::now();

        try {
            // Simulate processing with potential errors
            if (data.empty()) {
                AXONVEX_REPORT_WARNING(error_handler_, "Empty data set provided");
                context.markSuccess(); // This is expected, not an error
                return;
            }

            if (data.size() > 10000) {
                context.reportError(ErrorSeverity::ERROR, "Data set too large",
                                    ErrorCategory::VALIDATION);
                return; // context destructor will report failure
            }

            // Simulate work
            std::this_thread::sleep_for(std::chrono::microseconds(data.size()));

            // Record successful operation
            auto duration = TimingUtils::elapsed(start_time);
            throughput_stats_.recordSuccessfulOperation(duration);

            context.markSuccess();

        } catch (const std::exception& e) {
            context.reportError(ErrorSeverity::CRITICAL, e.what(), ErrorCategory::RUNTIME);
            throughput_stats_.recordFailedOperation();
        }
    }

    void allocateMemory(size_t bytes) {
        auto start_time = TimingUtils::now();

        // Simulate memory allocation
        memory_stats_.recordAllocation(bytes);

        auto duration = TimingUtils::elapsed(start_time);
        if (duration > std::chrono::microseconds(100)) {
            AXONVEX_REPORT_WARNING(error_handler_, "Slow memory allocation: " +
                                                       std::to_string(duration.count()) + " ns");
        }
    }

    void deallocateMemory(size_t bytes) {
        memory_stats_.recordDeallocation(bytes);
    }

    // Implement IPerformanceStatistics interface
    void reset() noexcept override {
        throughput_stats_.reset();
        memory_stats_.reset();
        error_handler_.clearErrors();
    }

    std::string getReport() const override {
        std::string report = "=== " + getComponentName() + " Report ===\n";
        report += throughput_stats_.getReport() + "\n";
        report += memory_stats_.getReport() + "\n";
        report += error_handler_.getErrorReport();
        return report;
    }

    std::string getComponentName() const override {
        return name_;
    }

    const ErrorHandler& getErrorHandler() const {
        return error_handler_;
    }
    const ThroughputStatistics& getThroughputStats() const {
        return throughput_stats_;
    }
    const MemoryStatistics& getMemoryStats() const {
        return memory_stats_;
    }
};

/**
 * @brief Demonstrate capacity utilities and shared constants
 */
void demonstrateSharedUtilities() {
    std::cout << "\n=== Shared Utilities Demo ===\n";

    // Demonstrate MathUtils
    std::cout << "Power of 2 calculations:\n";
    for (size_t value : {100, 500, 1000, 1023, 1024, 2000}) {
        size_t next_pow2 = MathUtils::nextPowerOf2(value);
        bool is_pow2 = MathUtils::isPowerOf2(value);
        std::cout << "  " << value << " -> " << next_pow2
                  << " (is power of 2: " << (is_pow2 ? "yes" : "no") << ")\n";
    }

    // Demonstrate CapacityUtils
    std::cout << "\nCapacity validation:\n";
    for (size_t requested : {10, 50, 1000, 2000000}) {
        size_t validated =
            CapacityUtils::validateCapacity(requested, CapacityUtils::Defaults::MIN_CAPACITY,
                                            CapacityUtils::Defaults::MAX_CAPACITY, true);
        std::cout << "  Requested: " << requested << " -> Validated: " << validated << "\n";
    }

    // Demonstrate atomic utilities
    std::cout << "\nAtomic utilities:\n";
    std::atomic<uint64_t> counter{0};

    AtomicUtils::safeIncrement(counter);
    AtomicUtils::safeIncrement(counter, static_cast<uint64_t>(99)); // Increment by 99 more
    std::cout << "  After increments: " << AtomicUtils::safeLoad(counter) << "\n";

    std::atomic<uint64_t> maximum{50};
    AtomicUtils::updateMaximum(maximum, static_cast<uint64_t>(75));
    AtomicUtils::updateMaximum(maximum, static_cast<uint64_t>(25)); // Should not update
    std::cout << "  Maximum after updates: " << AtomicUtils::safeLoad(maximum) << "\n";
}

/**
 * @brief Demonstrate enhanced memory pool utilization
 */
void demonstrateMemoryPoolOptimization() {
    std::cout << "\n=== Memory Pool Optimization Demo ===\n";

    // Create memory pool using shared utilities
    size_t pool_size = CapacityUtils::validateCapacity(1000, 16, 10000, true);
    MemoryPool<std::string> pool(pool_size);

    std::cout << "Pool capacity: " << pool.getCapacity() << " objects\n";

    // Allocate some objects
    std::vector<std::string*> allocated_objects;

    for (int i = 0; i < 10; ++i) {
        auto* obj = pool.allocateObject("Object " + std::to_string(i));
        if (obj) {
            allocated_objects.push_back(obj);
        }
    }

    std::cout << "Allocated " << allocated_objects.size() << " objects\n";
    std::cout << "Pool usage: " << pool.getUsage() << "/" << pool.getCapacity() << " ("
              << (pool.getUtilization() * 100.0) << "%)\n";

    // Get statistics
    const auto& stats = pool.getStatistics();
    std::cout << "Pool statistics:\n";
    std::cout << "  Allocations: " << stats.getAllocations() << "\n";
    std::cout << "  Current usage: " << stats.getCurrentUsage() << " bytes\n";
    std::cout << "  Peak usage: " << stats.getPeakUsage() << " bytes\n";

    // Clean up
    for (auto* obj : allocated_objects) {
        pool.deallocateObject(obj);
    }

    std::cout << "After cleanup - Pool usage: " << pool.getUsage() << "/" << pool.getCapacity()
              << "\n";
}

/**
 * @brief Demonstrate unified statistics across different components
 */
void demonstrateUnifiedStatistics() {
    std::cout << "\n=== Unified Statistics Demo ===\n";

    // Create components with common statistics interface
    // Note: CircularBuffer and ThreadSafeQueue integration is in progress
    OptimizedProcessor processor1("MainProcessor");
    OptimizedProcessor processor2("BackupProcessor");

    // Simulate some operations on processors
    std::vector<int> test_data = {1, 2, 3, 4, 5};
    processor1.processData(test_data);
    processor1.processData({}); // Empty data (warning)
    processor1.allocateMemory(1024);
    processor1.deallocateMemory(1024);

    // Test processor2 with different data
    std::vector<int> batch_data = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    processor2.processData(batch_data);
    processor2.allocateMemory(2048);
    processor2.deallocateMemory(2048);

    // Display unified statistics
    std::cout << "Processor 1 Statistics:\n" << processor1.getReport() << "\n";
    std::cout << "Processor 2 Statistics:\n" << processor2.getReport() << "\n";

    std::cout << "Both processors demonstrate the unified PerformanceStatisticsBase interface!\n";
}

/**
 * @brief Demonstrate error handling consistency
 */
void demonstrateErrorHandling() {
    std::cout << "\n=== Error Handling Demo ===\n";

    OptimizedProcessor processor("ErrorDemo");

    // Generate various types of errors
    std::vector<int> large_data(20000, 1); // Too large, will cause error
    processor.processData(large_data);

    std::vector<int> empty_data; // Empty data, will cause warning
    processor.processData(empty_data);

    // Manually report some errors
    auto& error_handler = const_cast<ErrorHandler&>(processor.getErrorHandler());

    AXONVEX_REPORT_ERROR(error_handler, ErrorSeverity::WARNING, "This is a test warning",
                         ErrorCategory::VALIDATION);

    AXONVEX_REPORT_CRITICAL(error_handler, "Critical system error detected");

    // Display error statistics
    auto error_stats = error_handler.getStatistics();
    std::cout << "Error Statistics:\n";
    std::cout << "  Total errors: " << error_stats.total_errors << "\n";
    std::cout << "  Warnings: " << error_stats.warning_count << "\n";
    std::cout << "  Errors: " << error_stats.error_count << "\n";
    std::cout << "  Critical: " << error_stats.critical_count << "\n";

    std::cout << "\nError Report:\n" << error_handler.getErrorReport() << "\n";
}

int main() {
    std::cout << "AxonVex Framework Optimization Demonstration\n";
    std::cout << "============================================\n";

    try {
        demonstrateSharedUtilities();
        demonstrateMemoryPoolOptimization();
        demonstrateUnifiedStatistics();
        demonstrateErrorHandling();

        std::cout << "\n=== Optimization Benefits Summary ===\n";
        std::cout << "✓ Unified performance statistics across all components\n";
        std::cout << "✓ Standardized error handling with automatic recovery\n";
        std::cout << "✓ Shared utilities eliminate code duplication\n";
        std::cout << "✓ Enhanced memory pool integration\n";
        std::cout << "✓ Consistent atomic operations and memory ordering\n";
        std::cout << "✓ Common capacity validation and power-of-2 optimization\n";
        std::cout << "✓ RAII-based error contexts for automatic error reporting\n";

    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
