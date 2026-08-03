#pragma once

#include <algorithm>
#include <atomic>
#include <axonvex_core/coreUtilities.hpp>
#include <axonvex_core/performanceStatistics.hpp>
#include <axonvex_core/utils/alignedNew.hpp>
#include <axonvex_core/utils/optional.hpp>
#include <chrono>
#include <cstring>
#include <memory>

namespace axonvex::utils::containers {

/**
 * @brief Enhanced statistics for ring buffer with common interface
 */
class RingBufferStatistics : public axonvex::core::PerformanceStatisticsBase<RingBufferStatistics> {
  public:
    std::atomic<uint64_t> write_count{0};
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> write_failures{0};
    std::atomic<uint64_t> read_failures{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> total_write_time_ns{0};
    std::atomic<uint64_t> total_read_time_ns{0};

    uint64_t getWriteCount() const noexcept {
        return safeLoad(write_count);
    }
    uint64_t getReadCount() const noexcept {
        return safeLoad(read_count);
    }
    uint64_t getWriteFailures() const noexcept {
        return safeLoad(write_failures);
    }
    uint64_t getReadFailures() const noexcept {
        return safeLoad(read_failures);
    }
    uint64_t getOverruns() const noexcept {
        return safeLoad(overruns);
    }
    uint64_t getUnderruns() const noexcept {
        return safeLoad(underruns);
    }

    void recordWrite(std::chrono::nanoseconds duration = std::chrono::nanoseconds{0}) noexcept {
        safeIncrement(write_count);
        if (duration.count() > 0) {
            safeIncrement(total_write_time_ns, static_cast<uint64_t>(duration.count()));
        }
        updateLastAccess();
    }

    void recordRead(std::chrono::nanoseconds duration = std::chrono::nanoseconds{0}) noexcept {
        safeIncrement(read_count);
        if (duration.count() > 0) {
            safeIncrement(total_read_time_ns, static_cast<uint64_t>(duration.count()));
        }
        updateLastAccess();
    }

    void recordWriteFailure() noexcept {
        safeIncrement(write_failures);
        safeIncrement(overruns);
        updateLastAccess();
    }

    void recordReadFailure() noexcept {
        safeIncrement(read_failures);
        safeIncrement(underruns);
        updateLastAccess();
    }

    std::chrono::nanoseconds getAverageWriteTime() const noexcept {
        uint64_t writes = safeLoad(write_count);
        uint64_t total_time = safeLoad(total_write_time_ns);
        return writes > 0 ? std::chrono::nanoseconds{total_time / writes}
                          : std::chrono::nanoseconds{0};
    }

    std::chrono::nanoseconds getAverageReadTime() const noexcept {
        uint64_t reads = safeLoad(read_count);
        uint64_t total_time = safeLoad(total_read_time_ns);
        return reads > 0 ? std::chrono::nanoseconds{total_time / reads}
                         : std::chrono::nanoseconds{0};
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
        report += "  Success Rate: " +
                  std::to_string(calculatePercentage(
                      getWriteCount() + getReadCount(),
                      getWriteCount() + getReadCount() + getWriteFailures() + getReadFailures())) +
                  "%\n";
        report += "  Runtime: " + std::to_string(elapsed.count()) + " seconds\n";
        return report;
    }

    std::string getComponentName() const override {
        return "RingBuffer";
    }
};

/**
 * @brief High-performance lock-free ring buffer for real-time applications
 */
template <typename T>
class RingBuffer {
  public:
    // Cache-line-aligned members make this type over-aligned; C++14's plain
    // new does not honour that (see alignedNew.hpp).
    AXONVEX_ALIGNED_NEW(RingBuffer)

    static constexpr size_t DEFAULT_CAPACITY =
        axonvex::core::CapacityUtils::Defaults::DEFAULT_CAPACITY;
    static constexpr size_t MIN_CAPACITY = axonvex::core::CapacityUtils::Defaults::MIN_CAPACITY;
    static constexpr size_t MAX_CAPACITY = axonvex::core::CapacityUtils::Defaults::MAX_CAPACITY;

    explicit RingBuffer(size_t capacity = DEFAULT_CAPACITY);
    ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    bool write(const T& item) noexcept;
    bool write(T&& item) noexcept;
    axonvex::optional<T> read() noexcept;
    axonvex::optional<T> peek() const noexcept;
    size_t writeMany(const T* items, size_t count) noexcept;
    size_t readMany(T* items, size_t count) noexcept;
    bool isEmpty() const noexcept;
    bool isFull() const noexcept;
    size_t size() const noexcept;
    size_t capacity() const noexcept;
    size_t available() const noexcept;
    const RingBufferStatistics& getStatistics() const noexcept;
    void resetStatistics() noexcept;
    void clear() noexcept;
    double getUtilization() const noexcept;

  private:
    const size_t capacity_;
    const size_t capacity_mask_;
    std::unique_ptr<T[]> buffer_;
    alignas(64) std::atomic<size_t> write_index_{0};
    alignas(64) std::atomic<size_t> read_index_{0};
    mutable RingBufferStatistics stats_;

    size_t getNextWriteIndex() const noexcept;
    size_t getNextReadIndex() const noexcept;
};

template <typename T>
RingBuffer<T>::RingBuffer(size_t capacity)
    : capacity_(axonvex::core::CapacityUtils::validateCapacity(capacity, MIN_CAPACITY, MAX_CAPACITY,
                                                               true)),
      capacity_mask_(capacity_ - 1), buffer_(std::make_unique<T[]>(capacity_)) {
    for (size_t i = 0; i < capacity_; ++i) {
        new (&buffer_[i]) T{};
    }
}

template <typename T>
bool RingBuffer<T>::write(const T& item) noexcept {
    const size_t current_write = write_index_.load(axonvex::core::MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;
    if (next_write == read_index_.load(axonvex::core::MemoryOrdering::acquire)) {
        stats_.recordWriteFailure();
        return false;
    }
    buffer_[current_write] = item;
    write_index_.store(next_write, axonvex::core::MemoryOrdering::release);
    stats_.recordWrite();
    return true;
}

template <typename T>
bool RingBuffer<T>::write(T&& item) noexcept {
    const size_t current_write = write_index_.load(axonvex::core::MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;
    if (next_write == read_index_.load(axonvex::core::MemoryOrdering::acquire)) {
        stats_.recordWriteFailure();
        return false;
    }
    buffer_[current_write] = std::move(item);
    write_index_.store(next_write, axonvex::core::MemoryOrdering::release);
    stats_.recordWrite();
    return true;
}

template <typename T>
axonvex::optional<T> RingBuffer<T>::read() noexcept {
    const size_t current_read = read_index_.load(axonvex::core::MemoryOrdering::relaxed);
    if (current_read == write_index_.load(axonvex::core::MemoryOrdering::acquire)) {
        stats_.recordReadFailure();
        return axonvex::nullopt;
    }
    T item = std::move(buffer_[current_read]);
    const size_t next_read = (current_read + 1) & capacity_mask_;
    read_index_.store(next_read, axonvex::core::MemoryOrdering::release);
    stats_.recordRead();
    return item;
}

template <typename T>
axonvex::optional<T> RingBuffer<T>::peek() const noexcept {
    const size_t current_read = read_index_.load(axonvex::core::MemoryOrdering::relaxed);
    if (current_read == write_index_.load(axonvex::core::MemoryOrdering::acquire)) {
        return axonvex::nullopt;
    }
    return buffer_[current_read];
}

template <typename T>
size_t RingBuffer<T>::writeMany(const T* items, size_t count) noexcept {
    if (!items || count == 0)
        return 0;
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!write(items[i]))
            break;
        ++written;
    }
    return written;
}

template <typename T>
size_t RingBuffer<T>::readMany(T* items, size_t count) noexcept {
    if (!items || count == 0)
        return 0;
    size_t read_count = 0;
    for (size_t i = 0; i < count; ++i) {
        auto item = read();
        if (!item.has_value())
            break;
        items[i] = std::move(item.value());
        ++read_count;
    }
    return read_count;
}

template <typename T>
bool RingBuffer<T>::isEmpty() const noexcept {
    return read_index_.load(axonvex::core::MemoryOrdering::acquire) ==
           write_index_.load(axonvex::core::MemoryOrdering::acquire);
}

template <typename T>
bool RingBuffer<T>::isFull() const noexcept {
    const size_t current_write = write_index_.load(axonvex::core::MemoryOrdering::relaxed);
    const size_t next_write = (current_write + 1) & capacity_mask_;
    return next_write == read_index_.load(axonvex::core::MemoryOrdering::acquire);
}

template <typename T>
size_t RingBuffer<T>::size() const noexcept {
    const size_t write_idx = write_index_.load(axonvex::core::MemoryOrdering::acquire);
    const size_t read_idx = read_index_.load(axonvex::core::MemoryOrdering::acquire);
    return (write_idx - read_idx) & capacity_mask_;
}

template <typename T>
size_t RingBuffer<T>::capacity() const noexcept {
    return capacity_;
}

template <typename T>
size_t RingBuffer<T>::available() const noexcept {
    return capacity_ - size() - 1;
}

template <typename T>
const RingBufferStatistics& RingBuffer<T>::getStatistics() const noexcept {
    return stats_;
}

template <typename T>
void RingBuffer<T>::resetStatistics() noexcept {
    stats_.reset();
}

template <typename T>
void RingBuffer<T>::clear() noexcept {
    read_index_.store(0, axonvex::core::MemoryOrdering::relaxed);
    write_index_.store(0, axonvex::core::MemoryOrdering::relaxed);
}

template <typename T>
double RingBuffer<T>::getUtilization() const noexcept {
    return static_cast<double>(size()) / static_cast<double>(capacity_);
}

template <typename T>
size_t RingBuffer<T>::getNextWriteIndex() const noexcept {
    return (write_index_.load(axonvex::core::MemoryOrdering::relaxed) + 1) & capacity_mask_;
}

template <typename T>
size_t RingBuffer<T>::getNextReadIndex() const noexcept {
    return (read_index_.load(axonvex::core::MemoryOrdering::relaxed) + 1) & capacity_mask_;
}

} // namespace axonvex::utils::containers
