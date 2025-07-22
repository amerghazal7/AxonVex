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
#include <axonvex/core/precisionTimer.hpp>

// High-performance data structures
#include <axonvex/core/circularBuffer.hpp>
#include <axonvex/core/threadSafeQueue.hpp>

// Memory management
#include <axonvex/core/memoryPool.hpp>

// Logging system with dual interface
#include <axonvex/core/logger.hpp>

// Configuration management
#include <axonvex/core/configuration.hpp>

// Cross-platform path management
#include <axonvex/core/path.hpp>

// Core runtime engine
#include <axonvex/core/processingUnit.hpp>
#include <axonvex/core/timingController.hpp>

// System management and orchestration
#include <axonvex/core/system.hpp>

// Advanced port system
#include <axonvex/core/ports.hpp>

// Callback system for event-driven programming
#include <axonvex/core/callback.hpp>
#include <axonvex/core/caller.hpp>
#include <axonvex/core/callerKeyed.hpp>

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
 *
 * Contains the essential components that form the foundation of the
 * AxonVex framework, including timing, memory management, logging,
 * configuration, system management, and high-performance data structures.
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
