# AxonVex Framework Source Directory

# ---------------------------------------------------------------------------
# Phase 3 modules: add utils, types, io, interfaces, plugins, algorithms,
# visualization, safety, deployment as part of the main axonvex target.
# These sections are resilient: if folders are empty, globs evaluate to empty.
# ---------------------------------------------------------------------------

# Core module
file(GLOB_RECURSE AXONVEX_CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp)
file(GLOB_RECURSE AXONVEX_CORE_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/core/*.hpp)
# Always add core sources/headers; this ensures the target has C++ sources
# so CMake can determine the linker language.
target_sources(axonvex PRIVATE ${AXONVEX_CORE_SOURCES} ${AXONVEX_CORE_HEADERS})


# Utils module
file(GLOB_RECURSE AXONVEX_UTILS_SOURCES CONFIGURE_DEPENDS utils/*.cpp)
file(GLOB_RECURSE AXONVEX_UTILS_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/utils/*.hpp)
if(AXONVEX_UTILS_SOURCES OR AXONVEX_UTILS_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_UTILS_SOURCES} ${AXONVEX_UTILS_HEADERS})
endif()

# Types module
file(GLOB_RECURSE AXONVEX_TYPES_SOURCES CONFIGURE_DEPENDS types/*.cpp)
file(GLOB_RECURSE AXONVEX_TYPES_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/types/*.hpp)
if(AXONVEX_TYPES_SOURCES OR AXONVEX_TYPES_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_TYPES_SOURCES} ${AXONVEX_TYPES_HEADERS})
endif()

# IO module
file(GLOB_RECURSE AXONVEX_IO_SOURCES CONFIGURE_DEPENDS io/*.cpp)
file(GLOB_RECURSE AXONVEX_IO_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/io/*.hpp)
if(AXONVEX_IO_SOURCES OR AXONVEX_IO_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_IO_SOURCES} ${AXONVEX_IO_HEADERS})
endif()

# Interfaces module
file(GLOB_RECURSE AXONVEX_INTERFACES_SOURCES CONFIGURE_DEPENDS interfaces/*.cpp)
file(GLOB_RECURSE AXONVEX_INTERFACES_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/interfaces/*.hpp)
if(AXONVEX_INTERFACES_SOURCES OR AXONVEX_INTERFACES_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_INTERFACES_SOURCES} ${AXONVEX_INTERFACES_HEADERS})
endif()

# Plugins module
file(GLOB_RECURSE AXONVEX_PLUGINS_SOURCES CONFIGURE_DEPENDS plugins/*.cpp)
file(GLOB_RECURSE AXONVEX_PLUGINS_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/plugins/*.hpp)
if(AXONVEX_PLUGINS_SOURCES OR AXONVEX_PLUGINS_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_PLUGINS_SOURCES} ${AXONVEX_PLUGINS_HEADERS})
endif()

# Algorithms module
file(GLOB_RECURSE AXONVEX_ALGORITHMS_SOURCES CONFIGURE_DEPENDS algorithms/*.cpp)
file(GLOB_RECURSE AXONVEX_ALGORITHMS_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/algorithms/*.hpp)
if(AXONVEX_ALGORITHMS_SOURCES OR AXONVEX_ALGORITHMS_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_ALGORITHMS_SOURCES} ${AXONVEX_ALGORITHMS_HEADERS})
endif()

# Visualization module
file(GLOB_RECURSE AXONVEX_VISUALIZATION_SOURCES CONFIGURE_DEPENDS visualization/*.cpp)
file(GLOB_RECURSE AXONVEX_VISUALIZATION_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/visualization/*.hpp)
if(AXONVEX_VISUALIZATION_SOURCES OR AXONVEX_VISUALIZATION_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_VISUALIZATION_SOURCES} ${AXONVEX_VISUALIZATION_HEADERS})
endif()

# Safety module
file(GLOB_RECURSE AXONVEX_SAFETY_SOURCES CONFIGURE_DEPENDS safety/*.cpp)
file(GLOB_RECURSE AXONVEX_SAFETY_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/safety/*.hpp)
if(AXONVEX_SAFETY_SOURCES OR AXONVEX_SAFETY_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_SAFETY_SOURCES} ${AXONVEX_SAFETY_HEADERS})
endif()

# Deployment module
file(GLOB_RECURSE AXONVEX_DEPLOYMENT_SOURCES CONFIGURE_DEPENDS deployment/*.cpp)
file(GLOB_RECURSE AXONVEX_DEPLOYMENT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/deployment/*.hpp)
if(AXONVEX_DEPLOYMENT_SOURCES OR AXONVEX_DEPLOYMENT_HEADERS)
    target_sources(axonvex PRIVATE ${AXONVEX_DEPLOYMENT_SOURCES} ${AXONVEX_DEPLOYMENT_HEADERS})
endif()

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


# Module source groups (for IDE organization)
source_group("Core" FILES ${AXONVEX_CORE_SOURCES} ${AXONVEX_CORE_HEADERS})
source_group("Utils" FILES ${AXONVEX_UTILS_SOURCES} ${AXONVEX_UTILS_HEADERS})
source_group("Types" FILES ${AXONVEX_TYPES_SOURCES} ${AXONVEX_TYPES_HEADERS})
source_group("IO" FILES ${AXONVEX_IO_SOURCES} ${AXONVEX_IO_HEADERS})
source_group("Interfaces" FILES ${AXONVEX_INTERFACES_SOURCES} ${AXONVEX_INTERFACES_HEADERS})
source_group("Plugins" FILES ${AXONVEX_PLUGINS_SOURCES} ${AXONVEX_PLUGINS_HEADERS})
source_group("Algorithms" FILES ${AXONVEX_ALGORITHMS_SOURCES} ${AXONVEX_ALGORITHMS_HEADERS})
source_group("Visualization" FILES ${AXONVEX_VISUALIZATION_SOURCES} ${AXONVEX_VISUALIZATION_HEADERS})
source_group("Safety" FILES ${AXONVEX_SAFETY_SOURCES} ${AXONVEX_SAFETY_HEADERS})
source_group("Deployment" FILES ${AXONVEX_DEPLOYMENT_SOURCES} ${AXONVEX_DEPLOYMENT_HEADERS})

source_group("Headers" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/axonvex/axonvex.hpp
)