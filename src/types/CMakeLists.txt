# AxonVex Types Module CMakeLists.txt
# Phase 3A: Foundation Modules - Types Module
# Enhanced type system and data structures for real-time applications
# Dependencies: None (only system libraries)

set(AXONVEX_TYPES_SOURCES
    primitives/primitives.cpp
)

set(AXONVEX_TYPES_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/types.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/primitives/primitives.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/collections/collections.hpp
)

# Add types sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${AXONVEX_TYPES_SOURCES}
    ${AXONVEX_TYPES_HEADERS}
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

source_group("Headers\\Types\\Core" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/types.hpp
)

source_group("Headers\\Types\\Primitives" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/primitives/primitives.hpp
)

source_group("Headers\\Types\\Collections" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/types/collections/collections.hpp
)

message(STATUS "Types module configured successfully")