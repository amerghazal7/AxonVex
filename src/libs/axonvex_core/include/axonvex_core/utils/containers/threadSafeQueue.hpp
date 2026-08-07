#pragma once

#include <algorithm>
#include <atomic>
#include <axonvex_core/utils/alignedNew.hpp>
#include <axonvex_core/utils/optional.hpp>
#include <chrono>
#include <memory>
#include <new>
#include <thread>
#include <type_traits>

namespace axonvex::utils::containers {

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

template <typename T>
class ThreadSafeQueue {
  public:
    // Cache-line-aligned members make this type over-aligned; C++14's plain
    // new does not honour that (see alignedNew.hpp).
    AXONVEX_ALIGNED_NEW(ThreadSafeQueue)

    static constexpr size_t DEFAULT_CAPACITY = 1024;
    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr size_t MAX_CAPACITY = 1024 * 1024;

    explicit ThreadSafeQueue(size_t capacity = DEFAULT_CAPACITY);
    ~ThreadSafeQueue();

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    bool enqueue(const T& item) noexcept;
    bool enqueue(T&& item) noexcept;
    axonvex::optional<T> dequeue() noexcept;
    axonvex::optional<T> tryDequeue(const std::chrono::nanoseconds& timeout) noexcept;
    bool isEmpty() const noexcept;
    bool isFull() const noexcept;
    size_t size() const noexcept;
    size_t capacity() const noexcept;
    const QueueStatistics& getStatistics() const noexcept;
    void resetStatistics() noexcept;
    void clear() noexcept;

  private:
    struct alignas(64) Slot {
        // Allocated as Slot[]; the array form of new ignores over-alignment in
        // C++14 the same way the scalar form does. Trivially destructible, so
        // no array cookie shifts the elements off the cache line.
        AXONVEX_ALIGNED_NEW(Slot)

        std::atomic<uint64_t> sequence{0};
        // Aligned-storage idiom: this is a byte buffer sized to hold one T,
        // and T is legitimately a pointer-to-aggregate for some
        // instantiations. The check's "pointer where a pointee size was
        // probably meant" heuristic doesn't apply to a template's storage
        // buffer.
        // NOLINTNEXTLINE(bugprone-sizeof-expression)
        alignas(T) char storage[sizeof(T)];
        Slot() = default;
        ~Slot() = default;
        T* data() noexcept {
            return reinterpret_cast<T*>(storage);
        }
        const T* data() const noexcept {
            return reinterpret_cast<const T*>(storage);
        }
    };

    static_assert(std::is_trivially_destructible<Slot>::value,
                  "Slot must stay trivially destructible: a non-trivial destructor makes array "
                  "new emit a cookie, which shifts every element off its cache line");
    static_assert(alignof(Slot) >= 64, "Slot must stay cache-line aligned");

    const size_t capacity_;
    const size_t capacity_mask_;
    std::unique_ptr<Slot[]> slots_;
    alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
    alignas(64) std::atomic<uint64_t> dequeue_pos_{0};
    mutable QueueStatistics stats_;

    static size_t nextPowerOf2(size_t value) noexcept;
    static constexpr std::memory_order acquire = std::memory_order_acquire;
    static constexpr std::memory_order release = std::memory_order_release;
    static constexpr std::memory_order relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order acq_rel = std::memory_order_acq_rel;
    static constexpr std::memory_order seq_cst = std::memory_order_seq_cst;
};

// Implementation
template <typename T>
ThreadSafeQueue<T>::ThreadSafeQueue(size_t capacity)
    : capacity_(std::max(MIN_CAPACITY, std::min(MAX_CAPACITY, nextPowerOf2(capacity)))),
      capacity_mask_(capacity_ - 1), slots_(std::make_unique<Slot[]>(capacity_)) {
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
            if (seq == pos) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    new (slot.data()) T(item);
                    slot.sequence.store(pos + 1, release);
                    stats_.enqueue_count.fetch_add(1, relaxed);
                    return true;
                }
            } else if (seq < pos) {
                stats_.enqueue_failures.fetch_add(1, relaxed);
                stats_.max_size_reached.fetch_add(1, relaxed);
                return false;
            } else {
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
            if (seq == pos) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    new (slot.data()) T(std::move(item));
                    slot.sequence.store(pos + 1, release);
                    stats_.enqueue_count.fetch_add(1, relaxed);
                    return true;
                }
            } else if (seq < pos) {
                stats_.enqueue_failures.fetch_add(1, relaxed);
                stats_.max_size_reached.fetch_add(1, relaxed);
                return false;
            } else {
                pos = enqueue_pos_.load(relaxed);
            }
        }
    } catch (...) {
        stats_.enqueue_failures.fetch_add(1, relaxed);
        return false;
    }
}

template <typename T>
axonvex::optional<T> ThreadSafeQueue<T>::dequeue() noexcept {
    try {
        uint64_t pos = dequeue_pos_.load(relaxed);
        for (;;) {
            Slot& slot = slots_[pos & capacity_mask_];
            uint64_t seq = slot.sequence.load(acquire);
            if (seq == pos + 1) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, relaxed)) {
                    T result = std::move(*slot.data());
                    slot.data()->~T();
                    slot.sequence.store(pos + capacity_, release);
                    stats_.dequeue_count.fetch_add(1, relaxed);
                    return result;
                }
            } else if (seq < pos + 1) {
                stats_.dequeue_failures.fetch_add(1, relaxed);
                return axonvex::nullopt;
            } else {
                pos = dequeue_pos_.load(relaxed);
            }
        }
    } catch (...) {
        stats_.dequeue_failures.fetch_add(1, relaxed);
        return axonvex::nullopt;
    }
}

template <typename T>
axonvex::optional<T> ThreadSafeQueue<T>::tryDequeue(
    const std::chrono::nanoseconds& timeout) noexcept {
    auto start_time = std::chrono::high_resolution_clock::now();
    while (true) {
        auto result = dequeue();
        if (result.has_value())
            return result;
        auto current_time = std::chrono::high_resolution_clock::now();
        if (current_time - start_time >= timeout)
            return axonvex::nullopt;
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
    uint64_t enq_pos = enqueue_pos_.load(relaxed);
    uint64_t deq_pos = dequeue_pos_.load(relaxed);
    for (uint64_t i = deq_pos; i < enq_pos; ++i) {
        Slot& slot = slots_[i & capacity_mask_];
        slot.data()->~T();
    }
    enqueue_pos_.store(0, relaxed);
    dequeue_pos_.store(0, relaxed);
    for (size_t i = 0; i < capacity_; ++i) {
        slots_[i].sequence.store(i, relaxed);
    }
}

template <typename T>
size_t ThreadSafeQueue<T>::nextPowerOf2(size_t value) noexcept {
    if (value == 0) {
        return 1;
    }
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= (value >> 16) >> 16; // two shifts: '>> 32' is UB when size_t is 32-bit
    return ++value;
}

template <typename T>
constexpr size_t ThreadSafeQueue<T>::DEFAULT_CAPACITY;
template <typename T>
constexpr size_t ThreadSafeQueue<T>::MIN_CAPACITY;
template <typename T>
constexpr size_t ThreadSafeQueue<T>::MAX_CAPACITY;

} // namespace axonvex::utils::containers
