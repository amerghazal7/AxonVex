/**
 * @file axonvex.hpp
 * @brief Main AxonVex Framework Header
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This is the main header file for the AxonVex real-time framework.
 * Include this file to access all core AxonVex functionality.
 */

#pragma once

/**
 * @file axonvex.hpp
 * @brief Main AxonVex Framework Header
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This is the main header file for the AxonVex real-time framework.
 * Include this file to access all core AxonVex functionality.
 */

#include <iostream>

// Core timing utilities
#include <axonvex_core/precisionTimer.hpp>

// High-performance data structures
// Containers are now part of utils module
// (RingBuffer, ThreadSafeQueue, MemoryPool via axonvex::utils::containers)

// Memory management
// memory pool provided via utils aggregator

// Logging system with dual interface
#include <axonvex_core/logger.hpp>

// Configuration management
#include <axonvex_core/configuration.hpp>

// Cross-platform path management
#include <axonvex_core/path.hpp>

// Core runtime engine
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/timingController.hpp>

// System management and orchestration
#include <axonvex_core/system.hpp>

// Advanced port system
#include <axonvex_core/ports.hpp>

// Callback system for event-driven programming
#include <axonvex_core/callback.hpp>
#include <axonvex_core/caller.hpp>
#include <axonvex_core/callerKeyed.hpp>

// Optional Phase 3 modules (header-first exposure)
#include <axonvex_core/utils/utils.hpp>
#include <axonvex_core/types/types.hpp>

/**
 * @namespace axonvex
 * @brief Main namespace for the AxonVex real-time framework
 *
 * The AxonVex framework provides high-performance, real-time capable
 * utilities and data structures for building scalable, deterministic systems.
 *
 * Key features:
 * - Microsecond-precision timing
 * - Lock-free data structures
 * - Real-time memory management
 * - High-performance async logging
 * - Comprehensive configuration management
 * - Thread-safe operations throughout
 * - Centralized system orchestration and lifecycle management
 *
 * All components are designed for minimal overhead and maximum performance
 * while maintaining ease of use and robust error handling.
 */
namespace axonvex {

/**
 * @namespace axonvex::core
 * @brief Core utilities and fundamental building blocks
 */
namespace core {
// Core utilities are defined in their respective headers
}

/**
 * @brief Print welcome message with version information
 */
inline void printWelcome() {
    std::cout << "AxonVex Framework v1.0.0" << std::endl;
    std::cout << "High-Performance Real-Time Processing Framework" << std::endl;
    std::cout << "Built with real-time optimizations for microsecond precision" << std::endl;
    std::cout << "Ready for real-time processing..." << std::endl;
}

} // namespace axonvex
