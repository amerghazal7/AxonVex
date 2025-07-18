# AxonVex Framework Source Directory

# Core library sources (only include implemented files)
set(AXONVEX_CORE_SOURCES
    # Core utilities (implemented)
    core/precisionTimer.cpp
    # core/threadSafeQueue.cpp  # Header-only template
    # core/circularBuffer.cpp   # Header-only template  
    # core/memoryPool.cpp       # Header-only template
    # core/logger.cpp           # Header-only template
    
    # Core utilities (to be implemented)
    # core/configuration.cpp
    
    # Core runtime engine (to be implemented)
    # core/processing_unit.cpp
    # core/port_system.cpp
    # core/timing_controller.cpp
    # core/resource_manager.cpp
    # core/system_manager.cpp
    # core/performance_monitor.cpp
)

# Core library headers (only include implemented files)
set(AXONVEX_CORE_HEADERS
    # Core utilities (implemented)
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/precisionTimer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/threadSafeQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/circularBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/memoryPool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/logger.hpp
    
    # Core utilities (to be implemented)
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/configuration.hpp
    
    # Core runtime engine (to be implemented)
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/processing_unit.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/port_system.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/timing_controller.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/resource_manager.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/system_manager.hpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/performance_monitor.hpp
    
    # Main header
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/axonvex.hpp
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
    # core/threadSafeQueue.cpp  # Header-only template
    # core/thread_safe_queue.cpp
    # core/circular_buffer.cpp
    # core/memory_pool.cpp
    # core/logger.cpp
    # core/configuration.cpp
)

source_group("Core Runtime" FILES
    # core/processing_unit.cpp
    # core/port_system.cpp
    # core/timing_controller.cpp
    # core/resource_manager.cpp
    # core/system_manager.cpp
    # core/performance_monitor.cpp
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