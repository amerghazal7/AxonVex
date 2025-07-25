#pragma once

// Types Module - Enhanced type system for real-time applications
// Part of Phase 3A: Foundation Modules

#include <axonvex/types/primitives/primitives.hpp>
#include <axonvex/types/collections/collections.hpp>

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
    
} // namespace axonvex::types