# AxonVex Utils Module CMakeLists.txt
# Phase 3A: Foundation Modules - Utils Module

# Utils Module - Add sources to main axonvex library
# Dependencies: None (only system libraries)

set(AXONVEX_UTILS_SOURCES
    math/matrix.cpp
    math/vector.cpp
    math/statistics.cpp
    containers/lockFreeStack.cpp
    containers/concurrentMap.cpp
    serialization/binarySerializer.cpp
    serialization/jsonSerializer.cpp
    validation/schemaValidator.cpp
    caching/lruCache.cpp
    profiling/performanceProfiler.cpp
)

set(AXONVEX_UTILS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/matrix.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/vector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/statistics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/containers/lockFreeStack.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/containers/concurrentMap.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/serialization/binarySerializer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/serialization/jsonSerializer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/validation/schemaValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/caching/lruCache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/profiling/performanceProfiler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/utils.hpp
)

# Add utils sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${AXONVEX_UTILS_SOURCES}
    ${AXONVEX_UTILS_HEADERS}
)

# Define module availability
target_compile_definitions(axonvex 
    PUBLIC
        AXONVEX_UTILS_MODULE_AVAILABLE
)

# Create source groups for IDE organization
source_group("Utils\\Math" FILES
    math/matrix.cpp
    math/vector.cpp
    math/statistics.cpp
)

source_group("Utils\\Containers" FILES
    containers/lockFreeStack.cpp
    containers/concurrentMap.cpp
)

source_group("Utils\\Serialization" FILES
    serialization/binarySerializer.cpp
    serialization/jsonSerializer.cpp
)

source_group("Utils\\Validation" FILES
    validation/schemaValidator.cpp
)

source_group("Utils\\Caching" FILES
    caching/lruCache.cpp
)

source_group("Utils\\Profiling" FILES
    profiling/performanceProfiler.cpp
)

source_group("Headers\\Utils\\Math" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/matrix.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/vector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/math/statistics.hpp
)

source_group("Headers\\Utils\\Containers" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/containers/lockFreeStack.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/containers/concurrentMap.hpp
)

source_group("Headers\\Utils\\Other" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/serialization/binarySerializer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/serialization/jsonSerializer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/validation/schemaValidator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/caching/lruCache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/profiling/performanceProfiler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/utils/utils.hpp
)

message(STATUS "Utils module configured successfully")