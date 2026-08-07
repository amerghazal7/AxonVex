#pragma once

#include <algorithm>
#include <atomic>
#include <axonvex_core/utils/alignedNew.hpp>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace axonvex::utils::containers {

struct MemoryPoolStatistics {
    std::atomic<uint64_t> allocations{0};
    std::atomic<uint64_t> deallocations{0};
    std::atomic<uint64_t> allocation_failures{0};
    std::atomic<uint64_t> deallocation_failures{0};
    std::atomic<uint64_t> pool_exhausted{0};
    std::atomic<uint64_t> peak_usage{0};
    std::atomic<uint64_t> current_usage{0};

    uint64_t getAllocations() const noexcept {
        return allocations.load();
    }
    uint64_t getDeallocations() const noexcept {
        return deallocations.load();
    }
    uint64_t getAllocationFailures() const noexcept {
        return allocation_failures.load();
    }
    uint64_t getDeallocationFailures() const noexcept {
        return deallocation_failures.load();
    }
    uint64_t getPoolExhausted() const noexcept {
        return pool_exhausted.load();
    }
    uint64_t getPeakUsage() const noexcept {
        return peak_usage.load();
    }
    uint64_t getCurrentUsage() const noexcept {
        return current_usage.load();
    }

    void reset() noexcept {
        allocations.store(0);
        deallocations.store(0);
        allocation_failures.store(0);
        deallocation_failures.store(0);
        pool_exhausted.store(0);
        peak_usage.store(0);
        current_usage.store(0);
    }
};

template <typename T>
class MemoryPool {
  public:
    // Cache-line-aligned members make this type over-aligned; C++14's plain
    // new does not honour that (see alignedNew.hpp).
    AXONVEX_ALIGNED_NEW(MemoryPool)

    static constexpr size_t DEFAULT_POOL_SIZE = 1024;
    static constexpr size_t MIN_POOL_SIZE = 16;
    static constexpr size_t MAX_POOL_SIZE = 1024 * 1024;

    explicit MemoryPool(size_t pool_size = DEFAULT_POOL_SIZE);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    T* allocate() noexcept;
    T* allocateObject() noexcept;
    T* allocateObject(const T& value) noexcept;
    T* allocateObject(T&& value) noexcept;
    bool deallocate(T* ptr) noexcept;
    bool deallocateObject(T* ptr) noexcept;
    size_t getUsage() const noexcept;
    size_t getCapacity() const noexcept;
    size_t getAvailable() const noexcept;
    bool isEmpty() const noexcept;
    bool isFull() const noexcept;
    double getUtilization() const noexcept;
    const MemoryPoolStatistics& getStatistics() const noexcept;
    void resetStatistics() noexcept;
    bool validate() const noexcept;
    void clear() noexcept;

  private:
    struct alignas(64) Block {
        // Allocated as Block[]; the array form of new ignores over-alignment
        // in C++14 the same way the scalar form does. Trivially destructible,
        // so no array cookie shifts the elements off the cache line.
        AXONVEX_ALIGNED_NEW(Block)

        std::atomic<uint32_t> next{0}; // index of next free block, NULL_INDEX terminates
        std::atomic<bool> is_allocated{false};
        alignas(T) char storage[sizeof(T)];
        Block() = default;
        ~Block() = default;
        T* data() noexcept {
            return reinterpret_cast<T*>(storage);
        }
        const T* data() const noexcept {
            return reinterpret_cast<const T*>(storage);
        }
    };

    static_assert(std::is_trivially_destructible<Block>::value,
                  "Block must stay trivially destructible: a non-trivial destructor makes array "
                  "new emit a cookie, which shifts every element off its cache line");
    static_assert(alignof(Block) >= 64, "Block must stay cache-line aligned");

    // C3 ABA fix: the free-list head packs {tag:32, index:32} into one 64-bit
    // atomic. Every successful pop/push increments the tag, so a CAS with a
    // stale head (same index recycled through pop→push) cannot succeed and a
    // stale `next` is never installed. Index-based because blocks_ is a
    // contiguous array (MAX_POOL_SIZE ≤ 2^20 << 2^32). The 32-bit tag would
    // need 4 billion pops between one thread's load and CAS to false-match —
    // not reachable in practice.
    static constexpr uint32_t NULL_INDEX = 0xFFFFFFFFu;
    static uint64_t packHead(uint32_t tag, uint32_t index) noexcept {
        return (static_cast<uint64_t>(tag) << 32) | index;
    }
    static uint32_t headIndex(uint64_t head) noexcept {
        return static_cast<uint32_t>(head & 0xFFFFFFFFu);
    }
    static uint32_t headTag(uint64_t head) noexcept {
        return static_cast<uint32_t>(head >> 32);
    }

    const size_t pool_size_;
    std::unique_ptr<Block[]> blocks_;
    alignas(64) std::atomic<uint64_t> free_head_{0};
    alignas(64) std::atomic<size_t> allocated_count_{0};
    mutable MemoryPoolStatistics stats_;

    static size_t nextPowerOf2(size_t value) noexcept;
    bool isValidPointer(const T* ptr) const noexcept;
    Block* getBlockFromPointer(const T* ptr) const noexcept;

    static constexpr std::memory_order acquire = std::memory_order_acquire;
    static constexpr std::memory_order release = std::memory_order_release;
    static constexpr std::memory_order relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order acq_rel = std::memory_order_acq_rel;
    static constexpr std::memory_order seq_cst = std::memory_order_seq_cst;
};

// Implementation
template <typename T>
MemoryPool<T>::MemoryPool(size_t pool_size)
    : pool_size_(std::max(MIN_POOL_SIZE, std::min(MAX_POOL_SIZE, nextPowerOf2(pool_size)))),
      blocks_(std::make_unique<Block[]>(pool_size_)) {
    for (size_t i = 0; i < pool_size_ - 1; ++i) {
        blocks_[i].next.store(static_cast<uint32_t>(i + 1), relaxed);
        blocks_[i].is_allocated.store(false, relaxed);
    }
    blocks_[pool_size_ - 1].next.store(NULL_INDEX, relaxed);
    blocks_[pool_size_ - 1].is_allocated.store(false, relaxed);
    free_head_.store(packHead(0, 0), relaxed);
}

template <typename T>
MemoryPool<T>::~MemoryPool() {
    clear();
}

template <typename T>
T* MemoryPool<T>::allocate() noexcept {
    try {
        // Tagged Treiber-stack pop (C3). acquire on the load pairs with the
        // release CAS in deallocate() so the popped block's `next` (written by
        // the pusher) is visible. The tag increment makes a stale head value
        // fail the CAS even if the same index is back on top (ABA).
        uint64_t head = free_head_.load(acquire);
        while (headIndex(head) != NULL_INDEX) {
            Block& block = blocks_[headIndex(head)];
            uint32_t next = block.next.load(relaxed);
            uint64_t new_head = packHead(headTag(head) + 1, next);
            if (free_head_.compare_exchange_weak(head, new_head, acq_rel, acquire)) {
                block.is_allocated.store(true, relaxed);
                size_t current_count = allocated_count_.fetch_add(1, relaxed) + 1;
                // peak_usage is atomic<uint64_t>; compare_exchange_weak's expected
                // parameter is a reference requiring an exact type match. size_t and
                // uint64_t are the same type on Linux/Windows but distinct on Apple
                // platforms (unsigned long vs unsigned long long) — a size_t local
                // here fails to bind and breaks the macOS build.
                uint64_t peak = stats_.peak_usage.load(relaxed);
                while (current_count > peak &&
                       !stats_.peak_usage.compare_exchange_weak(peak, current_count, relaxed)) {}
                stats_.current_usage.store(current_count, relaxed);
                stats_.allocations.fetch_add(1, relaxed);
                return block.data();
            }
            // CAS failure reloaded `head` with the current value
        }
        stats_.allocation_failures.fetch_add(1, relaxed);
        stats_.pool_exhausted.fetch_add(1, relaxed);
        return nullptr;
    } catch (...) {
        stats_.allocation_failures.fetch_add(1, relaxed);
        return nullptr;
    }
}

template <typename T>
T* MemoryPool<T>::allocateObject() noexcept {
    T* ptr = allocate();
    if (ptr != nullptr) {
        try {
            new (ptr) T{};
            return ptr;
        } catch (...) {
            deallocate(ptr);
            return nullptr;
        }
    }
    return nullptr;
}

template <typename T>
T* MemoryPool<T>::allocateObject(const T& value) noexcept {
    T* ptr = allocate();
    if (ptr != nullptr) {
        try {
            new (ptr) T(value);
            return ptr;
        } catch (...) {
            deallocate(ptr);
            return nullptr;
        }
    }
    return nullptr;
}

template <typename T>
T* MemoryPool<T>::allocateObject(T&& value) noexcept {
    T* ptr = allocate();
    if (ptr != nullptr) {
        try {
            new (ptr) T(std::move(value));
            return ptr;
        } catch (...) {
            deallocate(ptr);
            return nullptr;
        }
    }
    return nullptr;
}

template <typename T>
bool MemoryPool<T>::deallocate(T* ptr) noexcept {
    try {
        if (!isValidPointer(ptr)) {
            stats_.deallocation_failures.fetch_add(1, relaxed);
            return false;
        }
        Block* block = getBlockFromPointer(ptr);
        bool expected_allocated = true;
        if (!block->is_allocated.compare_exchange_strong(expected_allocated, false, acq_rel,
                                                         relaxed)) {
            stats_.deallocation_failures.fetch_add(1, relaxed);
            return false;
        }
        // Tagged push (C3): release CAS publishes block->next to the acquiring
        // pop in allocate(); tag increment prevents ABA on the head.
        uint32_t block_index = static_cast<uint32_t>(block - blocks_.get());
        uint64_t head = free_head_.load(relaxed);
        uint64_t new_head;
        do {
            block->next.store(headIndex(head), relaxed);
            new_head = packHead(headTag(head) + 1, block_index);
        } while (!free_head_.compare_exchange_weak(head, new_head, release, relaxed));
        size_t current_count = allocated_count_.fetch_sub(1, relaxed) - 1;
        stats_.current_usage.store(current_count, relaxed);
        stats_.deallocations.fetch_add(1, relaxed);
        return true;
    } catch (...) {
        stats_.deallocation_failures.fetch_add(1, relaxed);
        return false;
    }
}

template <typename T>
bool MemoryPool<T>::deallocateObject(T* ptr) noexcept {
    if (ptr != nullptr) {
        try {
            ptr->~T();
            return deallocate(ptr);
        } catch (...) { return deallocate(ptr); }
    }
    return false;
}

template <typename T>
size_t MemoryPool<T>::getUsage() const noexcept {
    return allocated_count_.load(acquire);
}

template <typename T>
size_t MemoryPool<T>::getCapacity() const noexcept {
    return pool_size_;
}

template <typename T>
size_t MemoryPool<T>::getAvailable() const noexcept {
    return pool_size_ - allocated_count_.load(acquire);
}

template <typename T>
bool MemoryPool<T>::isEmpty() const noexcept {
    return allocated_count_.load(acquire) == 0;
}

template <typename T>
bool MemoryPool<T>::isFull() const noexcept {
    return allocated_count_.load(acquire) == pool_size_;
}

template <typename T>
double MemoryPool<T>::getUtilization() const noexcept {
    return static_cast<double>(allocated_count_.load(acquire)) / static_cast<double>(pool_size_);
}

template <typename T>
const MemoryPoolStatistics& MemoryPool<T>::getStatistics() const noexcept {
    return stats_;
}

template <typename T>
void MemoryPool<T>::resetStatistics() noexcept {
    stats_.reset();
}

template <typename T>
bool MemoryPool<T>::validate() const noexcept {
    try {
        size_t allocated = allocated_count_.load(acquire);
        if (allocated > pool_size_) {
            return false;
        }
        size_t free_count = 0;
        uint32_t current = headIndex(free_head_.load(acquire));
        while (current != NULL_INDEX && free_count <= pool_size_) {
            free_count++;
            current = blocks_[current].next.load(relaxed);
        }
        return (free_count + allocated == pool_size_);
    } catch (...) { return false; }
}

template <typename T>
void MemoryPool<T>::clear() noexcept {
    for (size_t i = 0; i < pool_size_ - 1; ++i) {
        blocks_[i].next.store(static_cast<uint32_t>(i + 1), relaxed);
        blocks_[i].is_allocated.store(false, relaxed);
    }
    blocks_[pool_size_ - 1].next.store(NULL_INDEX, relaxed);
    blocks_[pool_size_ - 1].is_allocated.store(false, relaxed);
    free_head_.store(packHead(0, 0), relaxed);
    allocated_count_.store(0, relaxed);
    stats_.current_usage.store(0, relaxed);
}

template <typename T>
size_t MemoryPool<T>::nextPowerOf2(size_t value) noexcept {
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
bool MemoryPool<T>::isValidPointer(const T* ptr) const noexcept {
    if (ptr == nullptr)
        return false;
    const char* char_ptr = reinterpret_cast<const char*>(ptr);
    const char* blocks_start = reinterpret_cast<const char*>(blocks_.get());
    const char* blocks_end = blocks_start + (pool_size_ * sizeof(Block));
    return (char_ptr >= blocks_start && char_ptr < blocks_end);
}

template <typename T>
typename MemoryPool<T>::Block* MemoryPool<T>::getBlockFromPointer(const T* ptr) const noexcept {
    const char* char_ptr = reinterpret_cast<const char*>(ptr);
    const char* blocks_start = reinterpret_cast<const char*>(blocks_.get());
    size_t offset = char_ptr - blocks_start;
    size_t block_index = offset / sizeof(Block);
    return &blocks_[block_index];
}

template <typename T>
constexpr uint32_t MemoryPool<T>::NULL_INDEX;
template <typename T>
constexpr size_t MemoryPool<T>::DEFAULT_POOL_SIZE;
template <typename T>
constexpr size_t MemoryPool<T>::MIN_POOL_SIZE;
template <typename T>
constexpr size_t MemoryPool<T>::MAX_POOL_SIZE;

} // namespace axonvex::utils::containers
