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
#include "core/precisionTimer.hpp"

// High-performance data structures
#include "core/threadSafeQueue.hpp"
#include "core/circularBuffer.hpp"

// Memory management
#include "core/memoryPool.hpp"

// Logging system with dual interface
#include "core/logger.hpp"

// Configuration management
#include "core/configuration.hpp"

// Cross-platform path management
#include "core/path.hpp"

// Core runtime engine
#include "core/processingUnit.hpp"
#include "core/timingController.hpp"

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
 * configuration, and high-performance data structures.
 */
namespace core {
    // Core utilities are defined in their respective headers
}

// Convenience type aliases - bring core types to main namespace
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
using Configuration = core::Configuration;
using ConfigurationStatistics = core::ConfigurationStatistics;
using ValidationError = core::ValidationError;
using Path = core::Path;
using ProcessingUnit = core::ProcessingUnit;
using PerformanceMetrics = core::PerformanceMetrics;
using ProcessPriority = core::ProcessPriority;
using ExecutionState = core::ExecutionState;
template<typename T> using InputPort = core::InputPort<T>;
template<typename T> using OutputPort = core::OutputPort<T>;
using BasePort = core::BasePort;
using TimingController = core::TimingController;
using TimingConstraints = core::TimingConstraints;
using SchedulerPriority = core::SchedulerPriority;
using SchedulingPolicy = core::SchedulingPolicy;
using SchedulerStatistics = core::SchedulerStatistics;

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