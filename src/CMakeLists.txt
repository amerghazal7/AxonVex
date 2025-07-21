# AxonVex Framework Source Directory

# Core library sources (only include implemented files)
set(AXONVEX_CORE_SOURCES
    # Foundation layer (implemented)
    core/precisionTimer.cpp
    core/configuration.cpp
    core/path.cpp
    core/logger.cpp
    # Core runtime engine (implemented)
    core/processingUnit.cpp
    core/timingController.cpp
    # System management (implemented)
    core/system.cpp
    # Plugins infrastructure (placeholder)
    plugins/CMakeLists.txt
)

# Core library headers (only include implemented files)
set(AXONVEX_CORE_HEADERS
    # Foundation layer headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/precisionTimer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/threadSafeQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/circularBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/memoryPool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/logger.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/configuration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/path.hpp
    # Core runtime engine (implemented)
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/processingUnit.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/timingController.hpp
    # System management (implemented)
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/system.hpp
    # Subsystems (pending)
    # ... other subsystem headers will be added here
)

# Add sources to the main library
target_sources(axonvex PRIVATE
    ${AXONVEX_CORE_SOURCES}
)

# Add headers to the main library (for IDE support)
target_sources(axonvex PRIVATE
    ${AXONVEX_CORE_HEADERS}
)

# Find and link dependencies (only required ones for now)
# find_package(nlohmann_json REQUIRED)
# find_package(Boost REQUIRED COMPONENTS system thread chrono atomic filesystem)
# find_package(spdlog REQUIRED)
# find_package(Eigen3 REQUIRED)
# find_package(TBB REQUIRED)

# Link dependencies (minimal for now)
target_link_libraries(axonvex
    PUBLIC
        # Basic system libraries only
        # nlohmann_json::nlohmann_json
        # Boost::boost
        # Boost::system
        # Boost::thread
        # Boost::chrono
        # Boost::atomic
        # Boost::filesystem
        # spdlog::spdlog
        # Eigen3::Eigen
        # TBB::tbb
)

# Subdirectories (will be added as we implement them)
# add_subdirectory(subsystems)
# add_subdirectory(visualization)
# add_subdirectory(plugins)

# Create source groups for IDE organization
source_group("Core Utilities" FILES
    core/precisionTimer.cpp
    core/configuration.cpp
    core/path.cpp
    core/logger.cpp
    # core/threadSafeQueue.cpp  # Header-only template
    # core/thread_safe_queue.cpp
    # core/circular_buffer.cpp
    # core/memory_pool.cpp
    # core/logger.cpp
)

source_group("Core Runtime" FILES
    core/processingUnit.cpp
    core/timingController.cpp
    # core/port_system.cpp
    # core/resource_manager.cpp
    # core/system_manager.cpp
    # core/performance_monitor.cpp
)

source_group("System Management" FILES
    core/system.cpp
)

source_group("Headers\\Core Utilities" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/precisionTimer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/threadSafeQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/circularBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/memoryPool.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/thread_safe_queue.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/circular_buffer.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/memory_pool.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/logger.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/configuration.hpp
)

source_group("Headers\\Core Runtime" FILES
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/processing_unit.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/port_system.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/timing_controller.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/resource_manager.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/system_manager.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/performance_monitor.hpp
)

source_group("Headers" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/axonvex.hpp
) 