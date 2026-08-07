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
    value |= (value >> 16) >> 16; // two shifts: '>> 32' is UB when size_t is 32-bit
    return ++value;
}

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
    size_t capacity = requested_capacity < min_capacity
                          ? min_capacity
                          : (requested_capacity > max_capacity ? max_capacity : requested_capacity);
    return force_power_of_2 ? nextPowerOf2(capacity) : capacity;
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
    static constexpr bool value = std::is_trivially_copyable<T>::value &&
                                  std::is_trivially_destructible<T>::value &&
                                  (sizeof(T) <= sizeof(void*) * 2); // Reasonable size limit
};

template <typename T>
constexpr bool is_lockfree_suitable_v = is_lockfree_suitable<T>::value;

/**
 * @brief Check if type is suitable for memory pool allocation
 */
template <typename T>
struct is_pool_suitable {
    static constexpr bool value = std::is_destructible<T>::value && !std::is_abstract<T>::value &&
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
} // namespace TimingUtils

} // namespace axonvex::core
