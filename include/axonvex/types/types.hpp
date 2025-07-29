/**
 * @file types.hpp
 * @brief AxonVex Types Module - Enhanced type system for real-time applications
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

// Types Module - Enhanced type system for real-time applications
// Part of Phase 3A: Foundation Modules

// Primitive types
#include <axonvex/types/primitives/primitives.hpp>

// Collection types
#include <axonvex/types/collections/collections.hpp>

// Signal types
#include <axonvex/types/signals/signals.hpp>

// Geometry types
#include <axonvex/types/geometry/geometry.hpp>

// Time types (placeholder for future implementation)
// #include <axonvex/types/time/time.hpp>

// Unit types (placeholder for future implementation)
// #include <axonvex/types/units/units.hpp>

namespace axonvex::types {
    
    // Re-export commonly used types for convenience
    using namespace primitives;
    using namespace collections;
    
    // Type system version information
    constexpr int TYPES_MODULE_VERSION_MAJOR = 1;
    constexpr int TYPES_MODULE_VERSION_MINOR = 0;
    constexpr int TYPES_MODULE_VERSION_PATCH = 0;
    
    // Module information
    inline const char* getTypesModuleVersion() {
        return "1.0.0";
    }
    
    inline const char* getTypesModuleDescription() {
        return "AxonVex Types Module - Enhanced type system and data structures for real-time applications";
    }
    
    /**
     * @brief Initialize all type systems
     */
    inline void initialize() {
        // Initialize any global type system components if needed
    }

    /**
     * @brief Cleanup all type systems
     */
    inline void cleanup() {
        // Cleanup any global type system components if needed
    }

} // namespace axonvex::types