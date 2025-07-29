#pragma once

#include <axonvex/core/performanceStatistics.hpp>
#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>

namespace axonvex::core {

/**
 * @brief Unified statistics structure for all AxonVex components
 * Replaces the multiple incompatible statistics classes throughout the codebase
 */
class UnifiedStatistics : public PerformanceStatisticsBase<UnifiedStatistics> {
public:
    // Core operation counters
    std::atomic<uint64_t> total_operations{0};
    std::atomic<uint64_t> successful_operations{0};
    std::atomic<uint64_t> failed_operations{0};
    std::atomic<uint64_t> dropped_operations{0};

    // Memory statistics  
    std::atomic<uint64_t> memory_allocations{0};
    std::atomic<uint64_t> memory_deallocations{0};
    std::atomic<uint64_t> memory_allocation_failures{0};
    std::atomic<uint64_t> current_memory_usage{0};
    std::atomic<uint64_t> peak_memory_usage{0};

    // Timing statistics
    std::atomic<uint64_t> total_processing_time_ns{0};
    std::atomic<uint64_t> min_processing_time_ns{UINT64_MAX};
    std::atomic<uint64_t> max_processing_time_ns{0};

    // Queue/Buffer statistics
    std::atomic<uint64_t> enqueue_count{0};
    std::atomic<uint64_t> dequeue_count{0};
    std::atomic<uint64_t> buffer_overruns{0};
    std::atomic<uint64_t> buffer_underruns{0};
    std::atomic<uint64_t> peak_queue_size{0};

    // Error and recovery statistics
    std::atomic<uint64_t> error_count{0};
    std::atomic<uint64_t> recovery_attempts{0};
    std::atomic<uint64_t> successful_recoveries{0};

    // Deadline and scheduling statistics
    std::atomic<uint64_t> missed_deadlines{0};
    std::atomic<uint64_t> preemptions{0};
    std::atomic<uint64_t> scheduling_jitter_ns{0};

    /**
     * @brief Record a successful operation with timing
     */
    void recordSuccessfulOperation(std::chrono::nanoseconds duration = {}) noexcept {
        safeIncrement(total_operations);
        safeIncrement(successful_operations);
        
        if (duration.count() > 0) {
            safeIncrement(total_processing_time_ns, static_cast<uint64_t>(duration.count()));
            updateMinimum(min_processing_time_ns, static_cast<uint64_t>(duration.count()));
            updateMaximum(max_processing_time_ns, static_cast<uint64_t>(duration.count()));
        }
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
     * @brief Record a memory allocation
     */
    void recordMemoryAllocation(size_t bytes) noexcept {
        safeIncrement(memory_allocations);
        uint64_t new_usage = current_memory_usage.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        updateMaximum(peak_memory_usage, new_usage);
        updateLastAccess();
    }

    /**
     * @brief Record a memory deallocation
     */
    void recordMemoryDeallocation(size_t bytes) noexcept {
        safeIncrement(memory_deallocations);
        current_memory_usage.fetch_sub(bytes, std::memory_order_relaxed);
        updateLastAccess();
    }

    /**
     * @brief Record an enqueue operation
     */
    void recordEnqueue() noexcept {
        safeIncrement(enqueue_count);
        updateLastAccess();
    }

    /**
     * @brief Record a dequeue operation
     */
    void recordDequeue() noexcept {
        safeIncrement(dequeue_count);
        updateLastAccess();
    }

    /**
     * @brief Record a buffer overrun
     */
    void recordOverrun() noexcept {
        safeIncrement(buffer_overruns);
        updateLastAccess();
    }

    /**
     * @brief Record a buffer underrun
     */
    void recordUnderrun() noexcept {
        safeIncrement(buffer_underruns);
        updateLastAccess();
    }

    /**
     * @brief Record a missed deadline
     */
    void recordMissedDeadline() noexcept {
        safeIncrement(missed_deadlines);
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
        return operations > 0 ? std::chrono::nanoseconds{total_time / operations}
                              : std::chrono::nanoseconds{0};
    }

    /**
     * @brief Get operations per second
     */
    double getOperationsPerSecond() const noexcept {
        auto elapsed = getElapsedTime();
        uint64_t operations = safeLoad(successful_operations);
        return elapsed.count() > 0 ? operations / elapsed.count() : 0.0;
    }

    /**
     * @brief Get memory utilization percentage
     */
    double getMemoryUtilization(size_t capacity_bytes) const noexcept {
        return calculatePercentage(safeLoad(current_memory_usage), capacity_bytes);
    }

    void reset() noexcept override {
        // Reset all counters
        safeStore(total_operations, 0UL);
        safeStore(successful_operations, 0UL);
        safeStore(failed_operations, 0UL);
        safeStore(dropped_operations, 0UL);
        
        safeStore(memory_allocations, 0UL);
        safeStore(memory_deallocations, 0UL);
        safeStore(memory_allocation_failures, 0UL);
        safeStore(current_memory_usage, 0UL);
        safeStore(peak_memory_usage, 0UL);
        
        safeStore(total_processing_time_ns, 0UL);
        safeStore(min_processing_time_ns, UINT64_MAX);
        safeStore(max_processing_time_ns, 0UL);
        
        safeStore(enqueue_count, 0UL);
        safeStore(dequeue_count, 0UL);
        safeStore(buffer_overruns, 0UL);
        safeStore(buffer_underruns, 0UL);
        safeStore(peak_queue_size, 0UL);
        
        safeStore(error_count, 0UL);
        safeStore(recovery_attempts, 0UL);
        safeStore(successful_recoveries, 0UL);
        
        safeStore(missed_deadlines, 0UL);
        safeStore(preemptions, 0UL);
        safeStore(scheduling_jitter_ns, 0UL);
    }

    std::string getReport() const override {
        auto elapsed = getElapsedTime();
        std::string report = getComponentName() + " Unified Statistics:\n";
        
        // Operations
        report += "  Total Operations: " + std::to_string(safeLoad(total_operations)) + "\n";
        report += "  Successful: " + std::to_string(safeLoad(successful_operations)) + "\n";
        report += "  Failed: " + std::to_string(safeLoad(failed_operations)) + "\n";
        report += "  Success Rate: " + std::to_string(getSuccessRate()) + "%\n";
        
        // Performance
        report += "  Operations/Second: " + std::to_string(getOperationsPerSecond()) + "\n";
        report += "  Average Processing Time: " + formatDuration(getAverageProcessingTime()) + "\n";
        
        // Memory
        uint64_t mem_allocs = safeLoad(memory_allocations);
        if (mem_allocs > 0) {
            report += "  Memory Allocations: " + std::to_string(mem_allocs) + "\n";
            report += "  Current Usage: " + formatBytes(safeLoad(current_memory_usage)) + "\n";
            report += "  Peak Usage: " + formatBytes(safeLoad(peak_memory_usage)) + "\n";
        }
        
        // Queue/Buffer
        uint64_t enqueues = safeLoad(enqueue_count);
        if (enqueues > 0) {
            report += "  Enqueue Operations: " + std::to_string(enqueues) + "\n";
            report += "  Dequeue Operations: " + std::to_string(safeLoad(dequeue_count)) + "\n";
            report += "  Buffer Overruns: " + std::to_string(safeLoad(buffer_overruns)) + "\n";
            report += "  Buffer Underruns: " + std::to_string(safeLoad(buffer_underruns)) + "\n";
        }
        
        // Errors and timing
        uint64_t errors = safeLoad(error_count);
        if (errors > 0) {
            report += "  Errors: " + std::to_string(errors) + "\n";
            report += "  Recovery Success Rate: " + 
                      std::to_string(calculatePercentage(safeLoad(successful_recoveries), 
                                                       safeLoad(recovery_attempts))) + "%\n";
        }
        
        uint64_t deadlines = safeLoad(missed_deadlines);
        if (deadlines > 0) {
            report += "  Missed Deadlines: " + std::to_string(deadlines) + "\n";
        }
        
        report += "  Runtime: " + std::to_string(elapsed.count()) + " seconds\n";
        return report;
    }

    std::string getComponentName() const override {
        return component_name_.empty() ? "UnifiedComponent" : component_name_;
    }
    
    /**
     * @brief Set component name for reporting
     */
    void setComponentName(const std::string& name) {
        component_name_ = name;
    }

private:
    std::string component_name_;
    
    /**
     * @brief Helper to safely update minimum value
     */
    template <typename T>
    static void updateMinimum(std::atomic<T>& atomic_min, T new_value) noexcept {
        T current_min = atomic_min.load(std::memory_order_relaxed);
        while (new_value < current_min && !atomic_min.compare_exchange_weak(
                                              current_min, new_value, std::memory_order_relaxed)) {
            // Loop until successful update or a lower value is found
        }
    }
};

/**
 * @brief Type aliases for backward compatibility
 * These replace the multiple incompatible statistics types
 */
using CircularBufferStatistics = UnifiedStatistics;
using MemoryPoolStatistics = UnifiedStatistics;
using QueueStatistics = UnifiedStatistics;
using LogStatistics = UnifiedStatistics;
using TimingStatistics = UnifiedStatistics;

/**
 * @brief Specialized statistics for specific component types
 */
class BufferStatistics : public UnifiedStatistics {
public:
    std::string getComponentName() const override { return "Buffer"; }
    
    void recordWrite() noexcept { recordEnqueue(); }
    void recordRead() noexcept { recordDequeue(); }
    void recordWriteFailure() noexcept { recordOverrun(); }
    void recordReadFailure() noexcept { recordUnderrun(); }
};

class PoolStatistics : public UnifiedStatistics {
public:
    std::string getComponentName() const override { return "MemoryPool"; }
    
    void recordAllocation(size_t bytes) noexcept { recordMemoryAllocation(bytes); }
    void recordDeallocation(size_t bytes) noexcept { recordMemoryDeallocation(bytes); }
    void recordAllocationFailure() noexcept { 
        safeIncrement(memory_allocation_failures);
        recordFailedOperation();
    }
};

class SchedulerStatistics : public UnifiedStatistics {
public:
    std::string getComponentName() const override { return "Scheduler"; }
    
    void recordExecution(std::chrono::nanoseconds duration, bool success = true) noexcept {
        if (success) {
            recordSuccessfulOperation(duration);
        } else {
            recordFailedOperation();
        }
    }
    
    void recordDeadlineMiss() noexcept { recordMissedDeadline(); }
    void recordPreemption() noexcept { safeIncrement(preemptions); }
};

} // namespace axonvex::core