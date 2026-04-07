#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace axonvex::core {

/**
 * @brief Memory ordering constants for consistent usage across components
 */
namespace MemoryOrdering {
constexpr std::memory_order relaxed = std::memory_order_relaxed;
constexpr std::memory_order acquire = std::memory_order_acquire;
constexpr std::memory_order release = std::memory_order_release;
constexpr std::memory_order acq_rel = std::memory_order_acq_rel;
constexpr std::memory_order seq_cst = std::memory_order_seq_cst;
} // namespace MemoryOrdering

/**
 * @brief Common mathematical utilities
 */
namespace MathUtils {
/**
 * @brief Calculate next power of 2 for a given value
 *
 * Commonly used across CircularBuffer, ThreadSafeQueue, and MemoryPool
 * for capacity optimization.
 *
 * @param value Input value
 * @return Next power of 2 >= value
 */
constexpr size_t nextPowerOf2(size_t value) noexcept {
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

/**
 * @brief Check if a number is a power of 2
 */
constexpr bool isPowerOf2(size_t value) noexcept {
    return value > 0 && (value & (value - 1)) == 0;
}

/**
 * @brief Clamp a value between min and max
 */
template <typename T>
constexpr T clamp(T value, T min_val, T max_val) noexcept {
    return value < min_val ? min_val : (value > max_val ? max_val : value);
}

/**
 * @brief Align a value to the specified alignment
 */
template <typename T>
constexpr T alignTo(T value, T alignment) noexcept {
    static_assert(std::is_unsigned_v<T>, "Alignment requires unsigned types");
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace MathUtils

/**
 * @brief Atomic operation utilities
 */
namespace AtomicUtils {
/**
 * @brief Safe atomic load with default relaxed ordering
 */
template <typename T>
T safeLoad(const std::atomic<T>& atomic_value,
           std::memory_order order = MemoryOrdering::relaxed) noexcept {
    return atomic_value.load(order);
}

/**
 * @brief Safe atomic store with default relaxed ordering
 */
template <typename T>
void safeStore(std::atomic<T>& atomic_value, T value,
               std::memory_order order = MemoryOrdering::relaxed) noexcept {
    atomic_value.store(value, order);
}

/**
 * @brief Safe atomic increment with default relaxed ordering
 */
template <typename T>
T safeIncrement(std::atomic<T>& atomic_value, T increment = 1,
                std::memory_order order = MemoryOrdering::relaxed) noexcept {
    return atomic_value.fetch_add(increment, order);
}

/**
 * @brief Safe atomic decrement with default relaxed ordering
 */
template <typename T>
T safeDecrement(std::atomic<T>& atomic_value, T decrement = 1,
                std::memory_order order = MemoryOrdering::relaxed) noexcept {
    return atomic_value.fetch_sub(decrement, order);
}

/**
 * @brief Atomic maximum update (commonly used in statistics)
 */
template <typename T>
void updateMaximum(std::atomic<T>& atomic_max, T new_value,
                   std::memory_order order = MemoryOrdering::relaxed) noexcept {
    T current_max = atomic_max.load(order);
    while (new_value > current_max &&
           !atomic_max.compare_exchange_weak(current_max, new_value, order)) {
        // Loop until successful update or a higher value is found
    }
}

/**
 * @brief Atomic minimum update
 */
template <typename T>
void updateMinimum(std::atomic<T>& atomic_min, T new_value,
                   std::memory_order order = MemoryOrdering::relaxed) noexcept {
    T current_min = atomic_min.load(order);
    while (new_value < current_min &&
           !atomic_min.compare_exchange_weak(current_min, new_value, order)) {
        // Loop until successful update or a lower value is found
    }
}
} // namespace AtomicUtils

/**
 * @brief Memory alignment utilities
 */
namespace AlignmentUtils {
/**
 * @brief Cache line size for alignment (typically 64 bytes on modern CPUs)
 */
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Check if a pointer is aligned to specified boundary
 */
template <size_t Alignment>
bool isAligned(const void* ptr) noexcept {
    return reinterpret_cast<uintptr_t>(ptr) % Alignment == 0;
}

/**
 * @brief Align a pointer to specified boundary
 */
template <size_t Alignment>
void* alignPointer(void* ptr) noexcept {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    addr = (addr + Alignment - 1) & ~(Alignment - 1);
    return reinterpret_cast<void*>(addr);
}

/**
 * @brief Calculate aligned size
 */
template <size_t Alignment>
constexpr size_t alignedSize(size_t size) noexcept {
    return (size + Alignment - 1) & ~(Alignment - 1);
}
} // namespace AlignmentUtils

/**
 * @brief Capacity validation utilities used across components
 */
namespace CapacityUtils {
/**
 * @brief Validate and clamp capacity to reasonable bounds
 *
 * Used by CircularBuffer, ThreadSafeQueue, and MemoryPool
 *
 * @param requested_capacity User-requested capacity
 * @param min_capacity Minimum allowed capacity
 * @param max_capacity Maximum allowed capacity
 * @param force_power_of_2 Whether to round up to next power of 2
 * @return Validated capacity
 */
constexpr size_t validateCapacity(size_t requested_capacity, size_t min_capacity,
                                  size_t max_capacity, bool force_power_of_2 = true) noexcept {
    size_t capacity = MathUtils::clamp(requested_capacity, min_capacity, max_capacity);
    return force_power_of_2 ? MathUtils::nextPowerOf2(capacity) : capacity;
}

/**
 * @brief Common capacity constants
 */
namespace Defaults {
constexpr size_t MIN_CAPACITY = 16;
constexpr size_t DEFAULT_CAPACITY = 1024;
constexpr size_t MAX_CAPACITY = 1024 * 1024;
constexpr size_t LARGE_CAPACITY = 16384;
} // namespace Defaults
} // namespace CapacityUtils

/**
 * @brief Type trait helpers for template constraints
 */
namespace TypeTraits {
/**
 * @brief Check if type is suitable for lock-free containers
 */
template <typename T>
struct is_lockfree_suitable {
    static constexpr bool value = std::is_trivially_copyable_v<T> &&
                                  std::is_trivially_destructible_v<T> &&
                                  (sizeof(T) <= sizeof(void*) * 2); // Reasonable size limit
};

template <typename T>
constexpr bool is_lockfree_suitable_v = is_lockfree_suitable<T>::value;

/**
 * @brief Check if type is suitable for memory pool allocation
 */
template <typename T>
struct is_pool_suitable {
    static constexpr bool value = std::is_destructible_v<T> && !std::is_abstract_v<T> &&
                                  (sizeof(T) >= sizeof(void*)); // Must be at least pointer size
};

template <typename T>
constexpr bool is_pool_suitable_v = is_pool_suitable<T>::value;
} // namespace TypeTraits

/**
 * @brief Common timing utilities
 */
namespace TimingUtils {
/**
 * @brief High-resolution timestamp type
 */
using TimePoint = std::chrono::high_resolution_clock::time_point;
using Duration = std::chrono::nanoseconds;

/**
 * @brief Get current high-resolution timestamp
 */
inline TimePoint now() noexcept {
    return std::chrono::high_resolution_clock::now();
}

/**
 * @brief Calculate duration between two time points
 */
inline Duration elapsed(TimePoint start, TimePoint end = now()) noexcept {
    return std::chrono::duration_cast<Duration>(end - start);
}

/**
 * @brief Scoped timer for performance measurements
 */
template <typename Callback>
class ScopedTimer {
  public:
    explicit ScopedTimer(Callback&& callback)
        : callback_(std::forward<Callback>(callback)), start_time_(now()) {}

    ~ScopedTimer() {
        callback_(elapsed(start_time_));
    }

  private:
    Callback callback_;
    TimePoint start_time_;
};

/**
 * @brief Create a scoped timer with callback
 */
template <typename Callback>
auto makeScopedTimer(Callback&& callback) {
    return ScopedTimer<Callback>(std::forward<Callback>(callback));
}
} // namespace TimingUtils

/**
 * @brief Resource management utilities
 */
namespace ResourceUtils {
/**
 * @brief RAII wrapper for automatic resource cleanup
 */
template <typename Resource, typename Deleter>
class UniqueResource {
  public:
    explicit UniqueResource(Resource&& resource, Deleter&& deleter = Deleter{})
        : resource_(std::move(resource)), deleter_(std::move(deleter)), engaged_(true) {}

    ~UniqueResource() {
        if (engaged_) {
            deleter_(resource_);
        }
    }

    // Non-copyable but movable
    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;

    UniqueResource(UniqueResource&& other) noexcept
        : resource_(std::move(other.resource_)), deleter_(std::move(other.deleter_)),
          engaged_(other.engaged_) {
        other.engaged_ = false;
    }

    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) {
            if (engaged_) {
                deleter_(resource_);
            }
            resource_ = std::move(other.resource_);
            deleter_ = std::move(other.deleter_);
            engaged_ = other.engaged_;
            other.engaged_ = false;
        }
        return *this;
    }

    Resource& get() noexcept {
        return resource_;
    }
    const Resource& get() const noexcept {
        return resource_;
    }

    void release() noexcept {
        engaged_ = false;
    }

    explicit operator bool() const noexcept {
        return engaged_;
    }

  private:
    Resource resource_;
    Deleter deleter_;
    bool engaged_;
};

/**
 * @brief Create a unique resource with automatic cleanup
 */
template <typename Resource, typename Deleter>
auto makeUniqueResource(Resource&& resource, Deleter&& deleter) {
    return UniqueResource<std::decay_t<Resource>, std::decay_t<Deleter>>(
        std::forward<Resource>(resource), std::forward<Deleter>(deleter));
}
} // namespace ResourceUtils

/**
 * @brief Debug utilities for development builds
 */
namespace DebugUtils {
#ifdef AXONVEX_DEBUG
constexpr bool DEBUG_ENABLED = true;
#else
constexpr bool DEBUG_ENABLED = false;
#endif

/**
 * @brief Conditional debug assertion
 */
template <typename Condition>
void debugAssert(Condition&& condition, const char* message = "Debug assertion failed") {
    if constexpr (DEBUG_ENABLED) {
        if (!condition()) {
            // In debug builds, you might want to break here or log
            // For now, just provide the interface
            static_cast<void>(message);
        }
    }
}

/**
 * @brief Debug-only execution
 */
template <typename Func>
void debugOnly(Func&& func) {
    if constexpr (DEBUG_ENABLED) {
        func();
    }
}
} // namespace DebugUtils

} // namespace axonvex::core
