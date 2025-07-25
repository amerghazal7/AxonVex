# AxonVex Core Module CMakeLists.txt
# Core module - Foundation of the AxonVex framework
# Dependencies: None (only system libraries)

set(AXONVEX_CORE_SOURCES
    # Foundation layer (implemented)
    precisionTimer.cpp
    configuration.cpp
    path.cpp
    logger.cpp
    errorHandler.cpp
    # Core runtime engine (implemented)
    processingUnit.cpp
    timingController.cpp
    # System management (implemented)
    system.cpp
    # Advanced port system
    ports.cpp
)

set(AXONVEX_CORE_HEADERS
    # Foundation layer headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/precisionTimer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/threadSafeQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/circularBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/memoryPool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/logger.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/configuration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/path.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/errorHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/performanceStatistics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/coreUtilities.hpp
    # Core runtime engine headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/processingUnit.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/timingController.hpp
    # System management headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/system.hpp
    # Advanced port system headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/ports.hpp
    # Callback system headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/callback.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/caller.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/callerKeyed.hpp
)

# Add core sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${AXONVEX_CORE_SOURCES}
    ${AXONVEX_CORE_HEADERS}
)

# Define module availability
target_compile_definitions(axonvex 
    PUBLIC
        AXONVEX_CORE_MODULE_AVAILABLE
)

# Create source groups for IDE organization
source_group("Core\\Foundation" FILES
    precisionTimer.cpp
    configuration.cpp
    path.cpp
    logger.cpp
    errorHandler.cpp
)

source_group("Core\\Runtime" FILES
    processingUnit.cpp
    timingController.cpp
)

source_group("Core\\System" FILES
    system.cpp
    ports.cpp
)

source_group("Headers\\Core\\Foundation" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/precisionTimer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/threadSafeQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/circularBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/memoryPool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/logger.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/configuration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/path.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/errorHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/performanceStatistics.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/coreUtilities.hpp
)

source_group("Headers\\Core\\Runtime" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/processingUnit.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/timingController.hpp
)

source_group("Headers\\Core\\System" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/system.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/ports.hpp
)

source_group("Headers\\Core\\Callbacks" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/callback.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/caller.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/core/callerKeyed.hpp
)

message(STATUS "Core module configured successfully")