/**
 * @file alignedNew.hpp
 * @brief Heap allocation for over-aligned (cache-line separated) types.
 *
 * C++14's global `operator new` only guarantees `alignof(std::max_align_t)`
 * (16 bytes on x86-64) — over-aligned new is C++17. A type whose members are
 * `alignas(64)` therefore comes back misaligned from `new T` /
 * `std::make_unique<T>`: undefined behaviour, flagged by UBSan on every
 * construction, and it defeats the false-sharing separation the alignment
 * exists for in the first place.
 *
 * Types that pad to cache lines put AXONVEX_ALIGNED_NEW(Type) in their class
 * body so `new`/`make_unique` route through the platform's aligned allocator.
 * Revisit if the project ever moves to C++17.
 */

#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace axonvex {
namespace utils {
namespace detail {

/// Allocate @p size bytes aligned to @p alignment. Throws std::bad_alloc on
/// failure, like operator new.
inline void* allocateAligned(std::size_t size, std::size_t alignment) {
#ifdef _MSC_VER
    void* ptr = _aligned_malloc(size, alignment);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
#else
    // posix_memalign requires a power-of-two multiple of sizeof(void*); every
    // alignment used here is a cache-line size, which satisfies both.
    void* ptr = nullptr;
    if (::posix_memalign(&ptr, alignment, size) != 0) {
        throw std::bad_alloc();
    }
    return ptr;
#endif
}

/// Release memory from allocateAligned. Must not be std::free on MSVC.
inline void freeAligned(void* ptr) noexcept {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace detail
} // namespace utils
} // namespace axonvex

/// Class-scope aligned operator new/delete. Use inside the body of an
/// over-aligned type: AXONVEX_ALIGNED_NEW(MyType). The class is complete
/// inside a member function body, so alignof(Type) is valid here.
///
/// The placement forms are re-declared because a class-scope operator new
/// hides the global ones — without them, placement-new on the annotated type
/// stops compiling with an error that points nowhere near this header.
///
/// The fix is per-class, not transitive: embedding an annotated type *by
/// value* in another heap-allocated class re-creates the same misalignment one
/// level up, and that outer class needs the macro too.
#define AXONVEX_ALIGNED_NEW(Type)                                                                  \
    static void* operator new(std::size_t size) {                                                  \
        return ::axonvex::utils::detail::allocateAligned(size, alignof(Type));                     \
    }                                                                                              \
    static void operator delete(void* ptr) noexcept {                                              \
        ::axonvex::utils::detail::freeAligned(ptr);                                                \
    }                                                                                              \
    static void* operator new[](std::size_t size) {                                                \
        return ::axonvex::utils::detail::allocateAligned(size, alignof(Type));                     \
    }                                                                                              \
    static void operator delete[](void* ptr) noexcept {                                            \
        ::axonvex::utils::detail::freeAligned(ptr);                                                \
    }                                                                                              \
    static void* operator new(std::size_t, void* ptr) noexcept {                                   \
        return ptr;                                                                                \
    }                                                                                              \
    static void operator delete(void*, void*) noexcept {}                                          \
    static void* operator new[](std::size_t, void* ptr) noexcept {                                 \
        return ptr;                                                                                \
    }                                                                                              \
    static void operator delete[](void*, void*) noexcept {}
