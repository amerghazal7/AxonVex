#pragma once

#include <algorithm>
#include <atomic>
#include <axonvex/core/circularBuffer.hpp>
#include <chrono>
#include <memory>
#include <new>
#include <optional>
#include <thread>

namespace axonvex::core {

/**
 * @brief Queue statistics for monitoring and debugging
 */
struct QueueStatistics {
    std::atomic<uint64_t> enqueue_count{0};
    std::atomic<uint64_t> dequeue_count{0};
    std::atomic<uint64_t> enqueue_failures{0};
    std::atomic<uint64_t> dequeue_failures{0};
    std::atomic<uint64_t> max_size_reached{0};

    uint64_t getEnqueueCount() const noexcept {
        return enqueue_count.load();
    }
    uint64_t getDequeueCount() const noexcept {
        return dequeue_count.load();
    }
    uint64_t getEnqueueFailures() const noexcept {
        return enqueue_failures.load();
    }
    uint64_t getDequeueFailures() const noexcept {
        return dequeue_failures.load();
    }
    uint64_t getMaxSizeReached() const noexcept {
        return max_size_reached.load();
    }

    void reset() noexcept {
        enqueue_count.store(0);
        dequeue_count.store(0);
        enqueue_failures.store(0);
        dequeue_failures.store(0);
        max_size_reached.store(0);
    }
};

/**
 * @brief High-performance thread-safe queue for real-time applications
 *
 * Lock-free implementation using sequence numbers for strict FIFO ordering.
 * Leverages CircularBuffer's proven design patterns for storage management
 * while adding a sequence-based synchronization layer for thread safety.
 *
 * Features:
 * - Lock-free operations with strict FIFO guarantee
 * - Sequence-based approach eliminates race conditions
 * - Uses CircularBuffer patterns for efficient storage management
 * - Memory ordering optimizations for weak memory models
 * - Configurable capacity with power-of-2 optimization
 * - Real-time safe (no dynamic memory allocation during operation)
 * - Comprehensive statistics and monitoring
 * - Template-based for type safety and performance
 *
 * Performance characteristics:
 * - Enqueue/dequeue: O(1) with proper ordering
 * - Memory usage: Fixed size, no fragmentation
 * - Contention: Optimized for multi-producer/multi-consumer
 * - Latency: Sub-microsecond on modern hardware
 *
 * @tparam T Element type (must be move-constructible)
 */
template <typename T>
class ThreadSafeQueue {
  public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;
    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr size_t MAX_CAPACITY = 1024 * 1024;

    /**
     * @brief Construct a new ThreadSafeQueue
     *
     * @param capacity Queue capacity (will be rounded up to next power of 2)
     */
    explicit ThreadSafeQueue(size_t capacity = DEFAULT_CAPACITY);

    /**
     * @brief Destructor
     */
    ~ThreadSafeQueue();

    // Non-copyable, non-moveable for thread safety
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    /**
     * @brief Enqueue an element (non-blocking)
     *
     * @param item Element to enqueue
     * @return true if successful, false if queue is full
     */
    bool enqueue(const T& item) noexcept;

    /**
     * @brief Enqueue an element with move semantics (non-blocking)
     *
     * @param item Element to enqueue
     * @return true if successful, false if queue is full
     */
    bool enqueue(T&& item) noexcept;

    /**
     * @brief Dequeue an element (non-blocking)
     *
     * @return Element if available, std::nullopt if queue is empty
     */
    std::optional<T> dequeue() noexcept;

    /**
     * @brief Try to dequeue an element with timeout
     *
     * @param timeout Maximum time to wait
     * @return Element if available within timeout, std::nullopt otherwise
     */
    std::optional<T> tryDequeue(const std::chrono::nanoseconds& timeout) noexcept;

    /**
     * @brief Check if queue is empty
     *
     * @return true if empty (approximate, may change immediately)
     */
    bool isEmpty() const noexcept;

    /**
     * @brief Check if queue is full
     *
     * @return true if full (approximate, may change immediately)
     */
    bool isFull() const noexcept;

    /**
     * @brief Get current queue size
     *
     * @return Approximate current size
     */
    size_t size() const noexcept;

    /**
     * @brief Get queue capacity
     *
     * @return Maximum queue capacity
     */
    size_t capacity() const noexcept;

    /**
     * @brief Get queue statistics
     *
     * @return Reference to statistics object
     */
    const QueueStatistics& getStatistics() const noexcept;

    /**
     * @brief Reset queue statistics
     */
    void resetStatistics() noexcept;

    /**
     * @brief Clear all elements from queue
     *
     * Note: This is not thread-safe and should only be called when
     * no other threads are accessing the queue.
     */
    void clear() noexcept;

  private:
    // Sequence-based slot structure inspired by CircularBuffer design
    struct alignas(64) Slot {
        std::atomic<uint64_t> sequence{0};
        alignas(T) char storage[sizeof(T)];

        Slot() = default;
        ~Slot() = default;

        // Get data pointer (only when sequence is valid)
        T* data() noexcept {
            return reinterpret_cast<T*>(storage);
        }

        const T* data() const noexcept {
            return reinterpret_cast<const T*>(storage);
        }
    };

    // Queue configuration (following CircularBuffer patterns)
    const size_t capacity_;
    const size_t capacity_mask_;

    // Storage using CircularBuffer-inspired design
    std::unique_ptr<Slot[]> slots_;

    // Sequence counters with cache line alignment
    alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
    alignas(64) std::atomic<uint64_t> dequeue_pos_{0};

    // Statistics (compatible with CircularBuffer statistics)
    mutable QueueStatistics stats_;

    // Helper methods (reused from CircularBuffer)
    static size_t nextPowerOf2(size_t value) noexcept;

    // Memory ordering (following CircularBuffer patterns)
    static constexpr std::memory_order acquire = std::memory_order_acquire;
    static constexpr std::memory_order release = std::memory_order_release;
    static constexpr std::memory_order relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order acq_rel = std::memory_order_acq_rel;
    static constexpr std::memory_order seq_cst = std::memory_order_seq_cst;
};

// Implementation following CircularBuffer patterns
template <typename T>
ThreadSafeQueue<T>::ThreadSafeQueue(size_t capacity)
    : capacity_(std::max(MIN_CAPACITY, std::min(MAX_CAPACITY, nextPowerOf2(capacity)))),
      capacity_mask_(capacity_ - 1), slots_(std::make_unique<Slot[]>(capacity_)) {

    // Initialize sequence numbers for synchronization
    for (size_t i = 0; i < capacity_; ++i) {
        slots_[i].sequence.store(i, relaxed);
    }
}

template <typename T>
ThreadSafeQueue<T>::~ThreadSafeQueue() {
    clear();
}

template <typename T>
bool ThreadSafeQueue<T>::enqueue(const T& item) noexcept {
    try {
        uint64_t pos = enqueue_pos_.load(relaxed);

        for (;;) {
            Slot& slot = slots_[pos & capacity_mask_];
            uint64_t seq = slot.sequence.load(acquire);

            // Check if this slot is ready for writing
            if (seq == pos) {
                // Try to claim this position
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    // Successfully claimed, construct the item
                    new (slot.data()) T(item);

                    // Release the slot for reading
                    slot.sequence.store(pos + 1, release);

                    stats_.enqueue_count.fetch_add(1, relaxed);
                    return true;
                }
            } else if (seq < pos) {
                // This slot is from a previous cycle, queue is full
                stats_.enqueue_failures.fetch_add(1, relaxed);
                stats_.max_size_reached.fetch_add(1, relaxed);
                return false;
            } else {
                // This slot is ahead, reload position
                pos = enqueue_pos_.load(relaxed);
            }
        }
    } catch (...) {
        stats_.enqueue_failures.fetch_add(1, relaxed);
        return false;
    }
}

template <typename T>
bool ThreadSafeQueue<T>::enqueue(T&& item) noexcept {
    try {
        uint64_t pos = enqueue_pos_.load(relaxed);

        for (;;) {
            Slot& slot = slots_[pos & capacity_mask_];
            uint64_t seq = slot.sequence.load(acquire);

            // Check if this slot is ready for writing
            if (seq == pos) {
                // Try to claim this position
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    // Successfully claimed, construct the item
                    new (slot.data()) T(std::move(item));

                    // Release the slot for reading
                    slot.sequence.store(pos + 1, release);

                    stats_.enqueue_count.fetch_add(1, relaxed);
                    return true;
                }
            } else if (seq < pos) {
                // This slot is from a previous cycle, queue is full
                stats_.enqueue_failures.fetch_add(1, relaxed);
                stats_.max_size_reached.fetch_add(1, relaxed);
                return false;
            } else {
                // This slot is ahead, reload position
                pos = enqueue_pos_.load(relaxed);
            }
        }
    } catch (...) {
        stats_.enqueue_failures.fetch_add(1, relaxed);
        return false;
    }
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::dequeue() noexcept {
    try {
        uint64_t pos = dequeue_pos_.load(relaxed);

        for (;;) {
            Slot& slot = slots_[pos & capacity_mask_];
            uint64_t seq = slot.sequence.load(acquire);

            // Check if this slot is ready for reading
            if (seq == pos + 1) {
                // Try to claim this position
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    // Successfully claimed, extract the item
                    T result = std::move(*slot.data());
                    slot.data()->~T();

                    // Release the slot for writing (next cycle)
                    slot.sequence.store(pos + capacity_, release);

                    stats_.dequeue_count.fetch_add(1, relaxed);
                    return result;
                }
            } else if (seq < pos + 1) {
                // This slot is empty, queue is empty
                stats_.dequeue_failures.fetch_add(1, relaxed);
                return std::nullopt;
            } else {
                // This slot is ahead, reload position
                pos = dequeue_pos_.load(relaxed);
            }
        }
    } catch (...) {
        stats_.dequeue_failures.fetch_add(1, relaxed);
        return std::nullopt;
    }
}

template <typename T>
std::optional<T> ThreadSafeQueue<T>::tryDequeue(const std::chrono::nanoseconds& timeout) noexcept {
    auto start_time = std::chrono::high_resolution_clock::now();

    while (true) {
        auto result = dequeue();
        if (result.has_value()) {
            return result;
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        if (current_time - start_time >= timeout) {
            return std::nullopt;
        }

        // Small backoff to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }
}

template <typename T>
bool ThreadSafeQueue<T>::isEmpty() const noexcept {
    uint64_t enq_pos = enqueue_pos_.load(acquire);
    uint64_t deq_pos = dequeue_pos_.load(acquire);
    return enq_pos == deq_pos;
}

template <typename T>
bool ThreadSafeQueue<T>::isFull() const noexcept {
    uint64_t enq_pos = enqueue_pos_.load(acquire);
    uint64_t deq_pos = dequeue_pos_.load(acquire);
    return (enq_pos - deq_pos) >= capacity_;
}

template <typename T>
size_t ThreadSafeQueue<T>::size() const noexcept {
    uint64_t enq_pos = enqueue_pos_.load(acquire);
    uint64_t deq_pos = dequeue_pos_.load(acquire);
    return static_cast<size_t>(enq_pos - deq_pos);
}

template <typename T>
size_t ThreadSafeQueue<T>::capacity() const noexcept {
    return capacity_;
}

template <typename T>
const QueueStatistics& ThreadSafeQueue<T>::getStatistics() const noexcept {
    return stats_;
}

template <typename T>
void ThreadSafeQueue<T>::resetStatistics() noexcept {
    stats_.reset();
}

template <typename T>
void ThreadSafeQueue<T>::clear() noexcept {
    // This is not thread-safe, should only be called when no other threads are accessing
    uint64_t enq_pos = enqueue_pos_.load(relaxed);
    uint64_t deq_pos = dequeue_pos_.load(relaxed);

    // Destroy all constructed objects
    for (uint64_t i = deq_pos; i < enq_pos; ++i) {
        Slot& slot = slots_[i & capacity_mask_];
        slot.data()->~T();
    }

    // Reset positions and sequences
    enqueue_pos_.store(0, relaxed);
    dequeue_pos_.store(0, relaxed);

    for (size_t i = 0; i < capacity_; ++i) {
        slots_[i].sequence.store(i, relaxed);
    }
}

template <typename T>
size_t ThreadSafeQueue<T>::nextPowerOf2(size_t value) noexcept {
    if (value == 0)
        return 1;
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return ++value;
}

} // namespace axonvex::core
