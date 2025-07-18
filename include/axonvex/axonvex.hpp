/**
 * @file axonvex.hpp
 * @brief Main header file for the AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

// Include standard library headers first
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <optional>

/**
 * @namespace axonvex
 * @brief Main namespace for the AxonVex Framework
 * 
 * The AxonVex Framework is a high-performance real-time processing framework
 * designed for applications requiring microsecond-level precision and 
 * deterministic behavior.
 * 
 * Key features:
 * - Real-time processing with <1μs timing accuracy
 * - Lock-free data structures for maximum performance
 * - Modular architecture with plugin system
 * - Web-based visualization and monitoring
 * - Cross-platform compatibility
 * 
 * @example Basic usage:
 * @code{.cpp}
 * #include <axonvex/axonvex.hpp>
 * 
 * int main() {
 *     axonvex::PrecisionTimer timer;
 *     timer.start();
 *     
 *     // Your real-time processing code here
 *     
 *     timer.stop();
 *     std::cout << "Elapsed: " << timer.getElapsedNanoseconds() << " ns\n";
 *     return 0;
 * }
 * @endcode
 */

// Version information
#define AXONVEX_VERSION_MAJOR 1
#define AXONVEX_VERSION_MINOR 0  
#define AXONVEX_VERSION_PATCH 0
#define AXONVEX_VERSION_STRING "1.0.0"

// Compiler and platform detection
#ifdef _MSC_VER
    #define AXONVEX_COMPILER_MSVC
#elif defined(__GNUC__)
    #define AXONVEX_COMPILER_GCC
#elif defined(__clang__)
    #define AXONVEX_COMPILER_CLANG
#endif

#ifdef _WIN32
    #define AXONVEX_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define AXONVEX_PLATFORM_LINUX
#elif defined(__APPLE__)
    #define AXONVEX_PLATFORM_MACOS
#endif

// API export/import macros
#ifdef AXONVEX_PLATFORM_WINDOWS
    #ifdef AXONVEX_EXPORTS
        #define AXONVEX_API __declspec(dllexport)
    #else
        #define AXONVEX_API __declspec(dllimport)
    #endif
#else
    #define AXONVEX_API
#endif

// Compiler-specific optimizations
#ifdef AXONVEX_COMPILER_MSVC
    #define AXONVEX_FORCE_INLINE __forceinline
    #define AXONVEX_LIKELY(x) (x)
    #define AXONVEX_UNLIKELY(x) (x)
#elif defined(AXONVEX_COMPILER_GCC) || defined(AXONVEX_COMPILER_CLANG)
    #define AXONVEX_FORCE_INLINE __attribute__((always_inline)) inline
    #define AXONVEX_LIKELY(x) __builtin_expect(!!(x), 1)
    #define AXONVEX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define AXONVEX_FORCE_INLINE inline
    #define AXONVEX_LIKELY(x) (x)
    #define AXONVEX_UNLIKELY(x) (x)
#endif

// Real-time safety macros
#define AXONVEX_RT_SAFE [[nodiscard]]
#define AXONVEX_RT_CRITICAL [[nodiscard]]

// Core utilities (will be enabled as we implement them)
#include <axonvex/core/precisionTimer.hpp>
#include <axonvex/core/threadSafeQueue.hpp>
#include <axonvex/core/circularBuffer.hpp>
#include <axonvex/core/memoryPool.hpp>
#include <axonvex/core/logger.hpp>
// #include <axonvex/core/thread_safe_queue.hpp>
// #include <axonvex/core/circular_buffer.hpp>
// #include <axonvex/core/memory_pool.hpp>
// #include <axonvex/core/configuration.hpp>

// Core runtime engine (will be enabled as we implement them)
// #include <axonvex/core/processing_unit.hpp>
// #include <axonvex/core/port_system.hpp>
// #include <axonvex/core/timing_controller.hpp>
// #include <axonvex/core/resource_manager.hpp>
// #include <axonvex/core/system_manager.hpp>
// #include <axonvex/core/performance_monitor.hpp>

// Convenience using declarations - bring core types to main namespace
namespace axonvex {
    // Core utilities
    using PrecisionTimer = core::PrecisionTimer;
    using TimingStatistics = core::TimingStatistics;
    template<typename T> using ThreadSafeQueue = core::ThreadSafeQueue<T>;
    using QueueStatistics = core::QueueStatistics;
    template<typename T> using CircularBuffer = core::CircularBuffer<T>;
    using CircularBufferStatistics = core::CircularBufferStatistics;
    template<typename T> using MemoryPool = core::MemoryPool<T>;
    using MemoryPoolStatistics = core::MemoryPoolStatistics;
    using Logger = core::Logger;
    using LogLevel = core::LogLevel;
    using LogStatistics = core::LogStatistics;
    using LogOutput = core::LogOutput;
    using ConsoleOutput = core::ConsoleOutput;
    using FileOutput = core::FileOutput;
    
    // Future core types will be added here as they're implemented
    // using Configuration = core::Configuration;
}

// Welcome message
namespace axonvex {
    /**
     * @brief Print welcome message with version information
     */
    inline void printWelcome() {
        std::cout << "AxonVex Framework v" << AXONVEX_VERSION_STRING << std::endl;
        std::cout << "High-Performance Real-Time Processing Framework" << std::endl;
        std::cout << "Built with real-time optimizations for microsecond precision" << std::endl;
        std::cout << "Ready for real-time processing..." << std::endl;
    }
}

// Include standard library headers commonly used with AxonVex
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <optional>

// Make sure iostream is included before the function definitions
namespace axonvex {
    namespace core {
        // Forward declare core types
        class PrecisionTimer;
        struct TimingStatistics;
        template<typename T> class ThreadSafeQueue;
        struct QueueStatistics;
        template<typename T> class CircularBuffer;
        struct CircularBufferStatistics;
        template<typename T> class MemoryPool;
        struct MemoryPoolStatistics;
    }
} 