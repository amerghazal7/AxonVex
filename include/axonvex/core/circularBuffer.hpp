#pragma once

#include <atomic>
#include <memory>
#include <chrono>
#include <optional>
#include <algorithm>
#include <cstring>
#include <axonvex/core/performanceStatistics.hpp>
#include <axonvex/core/coreUtilities.hpp>

namespace axonvex::core {

/**
 * @brief Enhanced statistics for circular buffer with common interface
 */
class CircularBufferStatistics : public PerformanceStatisticsBase<CircularBufferStatistics> {
public:
    std::atomic<uint64_t> write_count{0};
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> write_failures{0};
    std::atomic<uint64_t> read_failures{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> total_write_time_ns{0};
    std::atomic<uint64_t> total_read_time_ns{0};

    uint64_t getWriteCount() const noexcept { return safeLoad(write_count); }
    uint64_t getReadCount() const noexcept { return safeLoad(read_count); }
    uint64_t getWriteFailures() const noexcept { return safeLoad(write_failures); }
    uint64_t getReadFailures() const noexcept { return safeLoad(read_failures); }
    uint64_t getOverruns() const noexcept { return safeLoad(overruns); }
    uint64_t getUnderruns() const noexcept { return safeLoad(underruns); }

    /**
     * @brief Record a successful write operation
     */
    void recordWrite(std::chrono::nanoseconds duration = std::chrono::nanoseconds{0}) noexcept {
        safeIncrement(write_count);
        if (duration.count() > 0) {
            safeIncrement(total_write_time_ns, static_cast<uint64_t>(duration.count()));
        }
        updateLastAccess();
    }

    /**
     * @brief Record a successful read operation
     */
    void recordRead(std::chrono::nanoseconds duration = std::chrono::nanoseconds{0}) noexcept {
        safeIncrement(read_count);
        if (duration.count() > 0) {
            safeIncrement(total_read_time_ns, static_cast<uint64_t>(duration.count()));
        }
        updateLastAccess();
    }

    /**
     * @brief Record a write failure
     */
    void recordWriteFailure() noexcept {
        safeIncrement(write_failures);
        safeIncrement(overruns);
        updateLastAccess();
    }

    /**
     * @brief Record a read failure
     */
    void recordReadFailure() noexcept {
        safeIncrement(read_failures);
        safeIncrement(underruns);
        updateLastAccess();
    }

    /**
     * @brief Get average write time
     */
    std::chrono::nanoseconds getAverageWriteTime() const noexcept {
        uint64_t writes = safeLoad(write_count);
        uint64_t total_time = safeLoad(total_write_time_ns);
        return writes > 0 ? std::chrono::nanoseconds{total_time / writes} : std::chrono::nanoseconds{0};
    }

    /**
     * @brief Get average read time
     */
    std::chrono::nanoseconds getAverageReadTime() const noexcept {
        uint64_t reads = safeLoad(read_count);
        uint64_t total_time = safeLoad(total_read_time_ns);
        return reads > 0 ? std::chrono::nanoseconds{total_time / reads} : std::chrono::nanoseconds{0};
    }

    void reset() noexcept override {
        safeStore(write_count, 0UL);
        safeStore(read_count, 0UL);
        safeStore(write_failures, 0UL);
        safeStore(read_failures, 0UL);
        safeStore(overruns, 0UL);
        safeStore(underruns, 0UL);
        safeStore(total_write_time_ns, 0UL);
        safeStore(total_read_time_ns, 0UL);
    }

    std::string getReport() const override {
        auto elapsed = getElapsedTime();
        std::string report = getComponentName() + " Performance Statistics:\n";
        report += "  Writes: " + std::to_string(getWriteCount()) +
                 " (avg: " + formatDuration(getAverageWriteTime()) + ")\n";
        report += "  Reads: " + std::to_string(getReadCount()) +
                 " (avg: " + formatDuration(getAverageReadTime()) + ")\n";
        report += "  Write Failures: " + std::to_string(getWriteFailures()) + "\n";
        report += "  Read Failures: " + std::to_string(getReadFailures()) + "\n";
        report += "  Overruns: " + std::to_string(getOverruns()) + "\n";
        report += "  Underruns: " + std::to_string(getUnderruns()) + "\n";
        report += "  Success Rate: " + std::to_string(calculatePercentage(
            getWriteCount() + getReadCount(),
            getWriteCount() + getReadCount() + getWriteFailures() + getReadFailures())) + "%\n";
        report += "  Runtime: " + std::to_string(elapsed.count()) + " seconds\n";
        return report;
    }

    std::string getComponentName() const override {
        return "CircularBuffer";
    }
};

/**
 * @brief High-performance lock-free circular buffer for real-time applications
 *
 * Optimized for single-producer/single-consumer scenarios with exceptional performance.
 * Uses atomic operations for thread-safe access without locks or blocking operations.
 *
 * Features:
 * - Lock-free implementation using atomic indices
 * - Memory-efficient with power-of-2 capacity optimization
 * - Real-time safe (no dynamic allocation during operation)
 * - Template-based for type safety and performance
 * - Cache-aligned data structures to prevent false sharing
 * - Comprehensive statistics and monitoring
 * - Support for both copy and move semantics
 *
 * Performance characteristics:
 * - Write/read: O(1) constant time
 * - Memory usage: Fixed size, no fragmentation
 * - Latency: Sub-microsecond on modern hardware
 * - Throughput: Millions of operations per second
 *
 * @tparam T Element type (must be default constructible)
 */
template<typename T>
class CircularBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = CapacityUtils::Defaults::DEFAULT_CAPACITY;
    static constexpr size_t MIN_CAPACITY = CapacityUtils::Defaults::MIN_CAPACITY;
    static constexpr size_t MAX_CAPACITY = CapacityUtils::Defaults::MAX_CAPACITY;

    /**
     * @brief Construct a new CircularBuffer
     *
     * @param capacity Buffer capacity (will be rounded up to next power of 2)
     */
    explicit CircularBuffer(size_t capacity = DEFAULT_CAPACITY);

    /**
     * @brief Destructor
     */
    ~CircularBuffer() = default;

    // Non-copyable, non-moveable for thread safety
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;
    CircularBuffer(CircularBuffer&&) = delete;
    CircularBuffer& operator=(CircularBuffer&&) = delete;

    /**
     * @brief Write an element to the buffer (non-blocking)
     *
     * @param item Element to write
     * @return true if successful, false if buffer is full
     */
    bool write(const T& item) noexcept;

    /**
     * @brief Write an element with move semantics (non-blocking)
     *
     * @param item Element to write
     * @return true if successful, false if buffer is full
     */
    bool write(T&& item) noexcept;

    /**
     * @brief Read an element from the buffer (non-blocking)
     *
     * @return Element if available, std::nullopt if buffer is empty
     */
    std::optional<T> read() noexcept;

    /**
     * @brief Peek at the next element without removing it
     *
     * @return Element if available, std::nullopt if buffer is empty
     */
    std::optional<T> peek() const noexcept;

    /**
     * @brief Write multiple elements to the buffer
     *
     * @param items Pointer to array of elements
     * @param count Number of elements to write
     * @return Number of elements actually written
     */
    size_t writeMany(const T* items, size_t count) noexcept;

    /**
     * @brief Read multiple elements from the buffer
     *
     * @param items Pointer to array to store elements
     * @param count Maximum number of elements to read
     * @return Number of elements actually read
     */
    size_t readMany(T* items, size_t count) noexcept;

    /**
     * @brief Check if buffer is empty
     *
     * @return true if empty (approximate, may change immediately)
     */
    bool isEmpty() const noexcept;

    /**
     * @brief Check if buffer is full
     *
     * @return true if full (approximate, may change immediately)
     */
    bool isFull() const noexcept;

    /**
     * @brief Get current buffer size
     *
     * @return Approximate current size
     */
    size_t size() const noexcept;

    /**
     * @brief Get buffer capacity
     *
     * @return Maximum buffer capacity
     */
    size_t capacity() const noexcept;

    /**
     * @brief Get available space for writing
     *
     * @return Number of elements that can be written
     */
    size_t available() const noexcept;

    /**
     * @brief Get buffer statistics
     *
     * @return Reference to statistics object
     */
    const CircularBufferStatistics& getStatistics() const noexcept;

    /**
     * @brief Reset buffer statistics
     */
    void resetStatistics() noexcept;

    /**
     * @brief Clear all elements from buffer
     *
     * Note: This is not thread-safe and should only be called when
     * no other threads are accessing the buffer.
     */
    void clear() noexcept;

    /**
     * @brief Get buffer utilization percentage
     *
     * @return Utilization as percentage (0.0 to 1.0)
     */
    double getUtilization() const noexcept;

private:
    // Buffer configuration (must be initialized first)
    const size_t capacity_;
    const size_t capacity_mask_;  // For power-of-2 optimization

    // Pre-allocated buffer storage
    std::unique_ptr<T[]> buffer_;

    // Cache-aligned atomic indices to prevent false sharing
    alignas(64) std::atomic<size_t> write_index_{0};
    alignas(64) std::atomic<size_t> read_index_{0};

    // Statistics
    mutable CircularBufferStatistics stats_;

    // Helper methods (using shared utilities)
    size_t getNextWriteIndex() const noexcept;
    size_t getNextReadIndex() const noexcept;
};

// Implementation (header-only for templates)
template<typename T>
CircularBuffer<T>::CircularBuffer(size_t capacity)
    : capacity_(CapacityUtils::validateCapacity(capacity, MIN_CAPACITY, MAX_CAPACITY, true))
    , capacity_mask_(capacity_ - 1)
    , buffer_(std::make_unique<T[]>(capacity_)) {

    // Initialize buffer with default values
    for (size_t i = 0; i < capacity_; ++i) {
        new (&buffer_[i]) T{};
    }
}

template<typename T>
bool CircularBuffer<T>::write(const T& item) noexcept {
    const size_t current_write = write_index_.load(MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;

    // Check if buffer is full
    if (next_write == read_index_.load(MemoryOrdering::acquire)) {
        stats_.recordWriteFailure();
        return false;
    }

    // Write data
    buffer_[current_write] = item;

    // Update write index (release semantics to ensure write completes first)
    write_index_.store(next_write, MemoryOrdering::release);

    stats_.recordWrite();
    return true;
}

template<typename T>
bool CircularBuffer<T>::write(T&& item) noexcept {
    const size_t current_write = write_index_.load(MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;

    // Check if buffer is full
    if (next_write == read_index_.load(MemoryOrdering::acquire)) {
        stats_.recordWriteFailure();
        return false;
    }

    // Write data with move semantics
    buffer_[current_write] = std::move(item);

    // Update write index (release semantics)
    write_index_.store(next_write, MemoryOrdering::release);

    stats_.recordWrite();
    return true;
}

template<typename T>
std::optional<T> CircularBuffer<T>::read() noexcept {
    const size_t current_read = read_index_.load(MemoryOrdering::relaxed);

    // Check if buffer is empty
    if (current_read == write_index_.load(MemoryOrdering::acquire)) {
        stats_.recordReadFailure();
        return std::nullopt;
    }

    // Read data
    T item = std::move(buffer_[current_read]);

    // Update read index (release semantics)
    const size_t next_read = (current_read + 1) & capacity_mask_;
    read_index_.store(next_read, MemoryOrdering::release);

    stats_.recordRead();
    return item;
}

template<typename T>
std::optional<T> CircularBuffer<T>::peek() const noexcept {
    const size_t current_read = read_index_.load(MemoryOrdering::relaxed);

    // Check if buffer is empty
    if (current_read == write_index_.load(MemoryOrdering::acquire)) {
        return std::nullopt;
    }

    // Return copy of data without updating read index
    return buffer_[current_read];
}

template<typename T>
size_t CircularBuffer<T>::writeMany(const T* items, size_t count) noexcept {
    if (!items || count == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!write(items[i])) {
            break;
        }
        ++written;
    }

    return written;
}

template<typename T>
size_t CircularBuffer<T>::readMany(T* items, size_t count) noexcept {
    if (!items || count == 0) {
        return 0;
    }

    size_t read_count = 0;
    for (size_t i = 0; i < count; ++i) {
        auto item = read();
        if (!item.has_value()) {
            break;
        }
        items[i] = std::move(item.value());
        ++read_count;
    }

    return read_count;
}

template<typename T>
bool CircularBuffer<T>::isEmpty() const noexcept {
    return read_index_.load(MemoryOrdering::acquire) == write_index_.load(MemoryOrdering::acquire);
}

template<typename T>
bool CircularBuffer<T>::isFull() const noexcept {
    const size_t current_write = write_index_.load(MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;
    return next_write == read_index_.load(MemoryOrdering::acquire);
}

template<typename T>
size_t CircularBuffer<T>::size() const noexcept {
    const size_t write_idx = write_index_.load(MemoryOrdering::acquire);
    const size_t read_idx = read_index_.load(MemoryOrdering::acquire);
    return (write_idx - read_idx) & capacity_mask_;
}

template<typename T>
size_t CircularBuffer<T>::capacity() const noexcept {
    return capacity_;
}

template<typename T>
size_t CircularBuffer<T>::available() const noexcept {
    return capacity_ - size() - 1; // -1 because we can't fill completely
}

template<typename T>
const CircularBufferStatistics& CircularBuffer<T>::getStatistics() const noexcept {
    return stats_;
}

template<typename T>
void CircularBuffer<T>::resetStatistics() noexcept {
    stats_.reset();
}

template<typename T>
void CircularBuffer<T>::clear() noexcept {
    read_index_.store(0, MemoryOrdering::relaxed);
    write_index_.store(0, MemoryOrdering::relaxed);
}

template<typename T>
double CircularBuffer<T>::getUtilization() const noexcept {
    return static_cast<double>(size()) / static_cast<double>(capacity_);
}

template<typename T>
size_t CircularBuffer<T>::getNextWriteIndex() const noexcept {
    return (write_index_.load(MemoryOrdering::relaxed) + 1) & capacity_mask_;
}

template<typename T>
size_t CircularBuffer<T>::getNextReadIndex() const noexcept {
    return (read_index_.load(MemoryOrdering::relaxed) + 1) & capacity_mask_;
}

} // namespace axonvex::core
