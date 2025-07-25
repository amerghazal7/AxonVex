/**
 * @file utils.hpp
 * @brief AxonVex Utils Module - Advanced utilities and helpers
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * The Utils module provides advanced utilities that extend core functionality
 * with specialized tools for mathematical operations, container management,
 * serialization, and performance profiling.
 */

#pragma once

// Math utilities
#include <axonvex/utils/math/matrix.hpp>
#include <axonvex/utils/math/vector.hpp>
#include <axonvex/utils/math/statistics.hpp>

// Container utilities
#include <axonvex/utils/containers/lockFreeStack.hpp>
#include <axonvex/utils/containers/concurrentMap.hpp>

// Serialization utilities
#include <axonvex/utils/serialization/binarySerializer.hpp>
#include <axonvex/utils/serialization/jsonSerializer.hpp>

// Validation utilities
#include <axonvex/utils/validation/schemaValidator.hpp>

// Caching utilities
#include <axonvex/utils/caching/lruCache.hpp>

// Profiling utilities
#include <axonvex/utils/profiling/performanceProfiler.hpp>

/**
 * @namespace axonvex::utils
 * @brief Advanced utilities and helpers extending core functionality
 *
 * The utils namespace provides specialized tools for mathematical operations,
 * high-performance containers, serialization, validation, caching, and
 * performance profiling.
 *
 * Key components:
 * - High-performance matrix and vector operations with SIMD optimization
 * - Lock-free data structures for concurrent programming
 * - Binary and JSON serialization with validation
 * - LRU caching for memory efficiency
 * - Performance profiling tools for optimization
 */
namespace axonvex::utils {

/**
 * @namespace axonvex::utils::math
 * @brief Mathematical operations and data structures
 */
namespace math {
// Math utilities are defined in their respective headers
}

/**
 * @namespace axonvex::utils::containers
 * @brief High-performance container implementations
 */
namespace containers {
// Container utilities are defined in their respective headers
}

/**
 * @namespace axonvex::utils::serialization
 * @brief Serialization and deserialization utilities
 */
namespace serialization {
// Serialization utilities are defined in their respective headers
}

/**
 * @namespace axonvex::utils::validation
 * @brief Data validation and schema utilities
 */
namespace validation {
// Validation utilities are defined in their respective headers
}

/**
 * @namespace axonvex::utils::caching
 * @brief Caching mechanisms and strategies
 */
namespace caching {
// Caching utilities are defined in their respective headers
}

/**
 * @namespace axonvex::utils::profiling
 * @brief Performance profiling and measurement tools
 */
namespace profiling {
// Profiling utilities are defined in their respective headers
}

} // namespace axonvex::utils