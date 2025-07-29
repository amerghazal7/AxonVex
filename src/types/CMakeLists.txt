# AxonVex Types Module CMakeLists.txt
# Phase 3A: Foundation Modules - Types Module
# Enhanced type system and data structures for real-time applications
# Dependencies: None (only system libraries)

set(TYPES_SOURCES
    primitives/primitives.cpp
    collections/collections.cpp
    signals/signals.cpp
    geometry/geometry.cpp
)

# Add types sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${TYPES_SOURCES}
)

# Define module availability
target_compile_definitions(axonvex 
    PUBLIC
        AXONVEX_TYPES_MODULE_AVAILABLE
)

# Create source groups for IDE organization
source_group("Types\\Primitives" FILES
    primitives/primitives.cpp
)

source_group("Types\\Collections" FILES
    collections/collections.cpp
)

source_group("Types\\Signals" FILES
    signals/signals.cpp
)

source_group("Types\\Geometry" FILES
    geometry/geometry.cpp
)

message(STATUS "Types module configured successfully")