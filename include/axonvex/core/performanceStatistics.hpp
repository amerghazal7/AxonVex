#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <chrono>

namespace axonvex::core {

/**
 * @brief Base interface for performance statistics collection
 *
 * Provides a common interface for all AxonVex components to collect
 * and report performance metrics in a consistent manner.
 */
class IPerformanceStatistics {
public:
    virtual ~IPerformanceStatistics() = default;

    /**
     * @brief Reset all statistics to initial values
     */
    virtual void reset() noexcept = 0;

    /**
     * @brief Get a formatted string report of all statistics
     */
    virtual std::string getReport() const = 0;

    /**
     * @brief Get the component name for this statistics collection
     */
    virtual std::string getComponentName() const = 0;
};

/**
 * @brief Common base implementation for performance statistics
 *
 * Provides shared functionality and patterns used across all
 * AxonVex statistics implementations.
 */
template<typename Derived>
class PerformanceStatisticsBase : public IPerformanceStatistics {
public:
    /**
     * @brief Get creation timestamp
     */
    std::chrono::steady_clock::time_point getCreationTime() const noexcept {
        return creation_time_;
    }

    /**
     * @brief Get elapsed time since creation
     */
    std::chrono::duration<double> getElapsedTime() const noexcept {
        return std::chrono::steady_clock::now() - creation_time_;
    }

    /**
     * @brief Update last access time (thread-safe)
     */
    void updateLastAccess() noexcept {
        last_access_time_.store(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    /**
     * @brief Get last access time
     */
    std::chrono::steady_clock::time_point getLastAccessTime() const noexcept {
        auto count = last_access_time_.load();
        return std::chrono::steady_clock::time_point{
            std::chrono::steady_clock::duration{count}
        };
    }

protected:
    /**
     * @brief Helper to safely load atomic counter
     */
    template<typename T>
    static T safeLoad(const std::atomic<T>& atomic_value) noexcept {
        return atomic_value.load(std::memory_order_relaxed);
    }

    /**
     * @brief Helper to safely increment atomic counter
     */
    template<typename T>
    static void safeIncrement(std::atomic<T>& atomic_value, T increment = 1) noexcept {
        atomic_value.fetch_add(increment, std::memory_order_relaxed);
    }

    /**
     * @brief Helper to safely store atomic value
     */
    template<typename T>
    static void safeStore(std::atomic<T>& atomic_value, T value) noexcept {
        atomic_value.store(value, std::memory_order_relaxed);
    }

    /**
     * @brief Helper to safely update maximum value
     */
    template<typename T>
    static void updateMaximum(std::atomic<T>& atomic_max, T new_value) noexcept {
        T current_max = atomic_max.load(std::memory_order_relaxed);
        while (new_value > current_max &&
               !atomic_max.compare_exchange_weak(current_max, new_value,
                                                std::memory_order_relaxed)) {
            // Loop until successful update or a higher value is found
        }
    }

    /**
     * @brief Format a duration for display
     */
    static std::string formatDuration(std::chrono::nanoseconds duration) {
        if (duration < std::chrono::microseconds{1}) {
            return std::to_string(duration.count()) + " ns";
        } else if (duration < std::chrono::milliseconds{1}) {
            return std::to_string(duration.count() / 1000.0) + " µs";
        } else if (duration < std::chrono::seconds{1}) {
            return std::to_string(duration.count() / 1000000.0) + " ms";
        } else {
            return std::to_string(duration.count() / 1000000000.0) + " s";
        }
    }

    /**
     * @brief Format a byte count for display
     */
    static std::string formatBytes(uint64_t bytes) {
        if (bytes < 1024) {
            return std::to_string(bytes) + " B";
        } else if (bytes < 1024 * 1024) {
            return std::to_string(bytes / 1024.0) + " KB";
        } else if (bytes < 1024 * 1024 * 1024) {
            return std::to_string(bytes / (1024.0 * 1024.0)) + " MB";
        } else {
            return std::to_string(bytes / (1024.0 * 1024.0 * 1024.0)) + " GB";
        }
    }

    /**
     * @brief Calculate percentage safely
     */
    static double calculatePercentage(uint64_t numerator, uint64_t denominator) noexcept {
        return denominator > 0 ? (static_cast<double>(numerator) / denominator) * 100.0 : 0.0;
    }

private:
    const std::chrono::steady_clock::time_point creation_time_{std::chrono::steady_clock::now()};
    std::atomic<int64_t> last_access_time_{std::chrono::steady_clock::now().time_since_epoch().count()};
};

/**
 * @brief Enhanced statistics for components with throughput metrics
 */
class ThroughputStatistics : public PerformanceStatisticsBase<ThroughputStatistics> {
public:
    std::atomic<uint64_t> total_operations{0};
    std::atomic<uint64_t> successful_operations{0};
    std::atomic<uint64_t> failed_operations{0};
    std::atomic<uint64_t> total_processing_time_ns{0};
    std::atomic<uint64_t> peak_operations_per_second{0};

    /**
     * @brief Record a successful operation with timing
     */
    void recordSuccessfulOperation(std::chrono::nanoseconds duration) noexcept {
        safeIncrement(total_operations);
        safeIncrement(successful_operations);
        safeIncrement(total_processing_time_ns, static_cast<uint64_t>(duration.count()));
        updateLastAccess();
    }

    /**
     * @brief Record a failed operation
     */
    void recordFailedOperation() noexcept {
        safeIncrement(total_operations);
        safeIncrement(failed_operations);
        updateLastAccess();
    }

    /**
     * @brief Get success rate as percentage
     */
    double getSuccessRate() const noexcept {
        return calculatePercentage(safeLoad(successful_operations), safeLoad(total_operations));
    }

    /**
     * @brief Get average processing time
     */
    std::chrono::nanoseconds getAverageProcessingTime() const noexcept {
        uint64_t operations = safeLoad(successful_operations);
        uint64_t total_time = safeLoad(total_processing_time_ns);
        return operations > 0 ?
            std::chrono::nanoseconds{total_time / operations} :
            std::chrono::nanoseconds{0};
    }

    /**
     * @brief Calculate current operations per second
     */
    double getOperationsPerSecond() const noexcept {
        auto elapsed = getElapsedTime();
        uint64_t operations = safeLoad(successful_operations);
        return elapsed.count() > 0 ? operations / elapsed.count() : 0.0;
    }

    void reset() noexcept override {
        safeStore(total_operations, 0UL);
        safeStore(successful_operations, 0UL);
        safeStore(failed_operations, 0UL);
        safeStore(total_processing_time_ns, 0UL);
        safeStore(peak_operations_per_second, 0UL);
    }

    std::string getReport() const override {
        auto elapsed = getElapsedTime();
        std::string report = getComponentName() + " Performance Statistics:\n";
        report += "  Total Operations: " + std::to_string(safeLoad(total_operations)) + "\n";
        report += "  Successful: " + std::to_string(safeLoad(successful_operations)) + "\n";
        report += "  Failed: " + std::to_string(safeLoad(failed_operations)) + "\n";
        report += "  Success Rate: " + std::to_string(getSuccessRate()) + "%\n";
        report += "  Average Processing Time: " + formatDuration(getAverageProcessingTime()) + "\n";
        report += "  Operations/Second: " + std::to_string(getOperationsPerSecond()) + "\n";
        report += "  Runtime: " + std::to_string(elapsed.count()) + " seconds\n";
        return report;
    }

    std::string getComponentName() const override {
        return "ThroughputStatistics";
    }
};

/**
 * @brief Enhanced statistics for memory-related components
 */
class MemoryStatistics : public PerformanceStatisticsBase<MemoryStatistics> {
public:
    std::atomic<uint64_t> allocations{0};
    std::atomic<uint64_t> deallocations{0};
    std::atomic<uint64_t> allocation_failures{0};
    std::atomic<uint64_t> current_usage{0};
    std::atomic<uint64_t> peak_usage{0};
    std::atomic<uint64_t> total_bytes_allocated{0};

    /**
     * @brief Record an allocation
     */
    void recordAllocation(size_t bytes) noexcept {
        safeIncrement(allocations);
        safeIncrement(total_bytes_allocated, bytes);
        uint64_t new_usage = current_usage.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        updateMaximum(peak_usage, new_usage);
        updateLastAccess();
    }

    /**
     * @brief Record a deallocation
     */
    void recordDeallocation(size_t bytes) noexcept {
        safeIncrement(deallocations);
        current_usage.fetch_sub(bytes, std::memory_order_relaxed);
        updateLastAccess();
    }

    /**
     * @brief Record an allocation failure
     */
    void recordAllocationFailure() noexcept {
        safeIncrement(allocation_failures);
        updateLastAccess();
    }

    /**
     * @brief Get current utilization percentage
     */
    double getUtilization(size_t capacity_bytes) const noexcept {
        return calculatePercentage(safeLoad(current_usage), capacity_bytes);
    }

    void reset() noexcept override {
        safeStore(allocations, 0UL);
        safeStore(deallocations, 0UL);
        safeStore(allocation_failures, 0UL);
        safeStore(current_usage, 0UL);
        safeStore(peak_usage, 0UL);
        safeStore(total_bytes_allocated, 0UL);
    }

    std::string getReport() const override {
        auto elapsed = getElapsedTime();
        std::string report = getComponentName() + " Memory Statistics:\n";
        report += "  Allocations: " + std::to_string(safeLoad(allocations)) + "\n";
        report += "  Deallocations: " + std::to_string(safeLoad(deallocations)) + "\n";
        report += "  Allocation Failures: " + std::to_string(safeLoad(allocation_failures)) + "\n";
        report += "  Current Usage: " + formatBytes(safeLoad(current_usage)) + "\n";
        report += "  Peak Usage: " + formatBytes(safeLoad(peak_usage)) + "\n";
        report += "  Total Allocated: " + formatBytes(safeLoad(total_bytes_allocated)) + "\n";
        report += "  Runtime: " + std::to_string(elapsed.count()) + " seconds\n";
        return report;
    }

    std::string getComponentName() const override {
        return "MemoryStatistics";
    }
};

} // namespace axonvex::core
