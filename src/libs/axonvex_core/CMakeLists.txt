message("############# AxonVex Core Lib CMake ##################")

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(TBB REQUIRED)

add_library(axonvex_core SHARED
    src/builtinUnits.cpp
    src/configuration.cpp
    src/errorHandler.cpp
    src/eventBus.cpp
    src/healthMonitor.cpp
    src/logger.cpp
    src/path.cpp
    src/ports.cpp
    src/precisionTimer.cpp
    src/processingUnit.cpp
    src/system.cpp
    src/systemPortRegistry.cpp
    src/systemSpec.cpp
    src/timingController.cpp
    src/traceReplay.cpp
    src/unitFactory.cpp
    src/unitMetadata.cpp
    src/unitRegistry.cpp
    src/workerThread.cpp
)

set_target_properties(axonvex_core PROPERTIES
    EXPORT_NAME core
    # SONAME tracks the major version only: 1.0.0 and 1.4.2 keep the same
    # SONAME (libaxonvex_core.so.1) and are ABI-compatible by convention;
    # a breaking change bumps PROJECT_VERSION_MAJOR in the root CMakeLists.
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_definitions(axonvex_core
    PUBLIC
        $<$<PLATFORM_ID:Linux>:AXONVEX_PLATFORM_LINUX>
)

# version.hpp is generated (single source of truth: the project() call above
# this subdirectory in the root CMakeLists), not hand-maintained — see
# version.hpp.in.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/axonvex_core/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/axonvex_core/version.hpp"
    @ONLY
)

target_include_directories(axonvex_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_core/include>
)

target_link_libraries(axonvex_core
    PUBLIC
        Threads::Threads
        TBB::tbb
        nlohmann_json::nlohmann_json
    PRIVATE
        $<$<PLATFORM_ID:Linux>:rt>
)

# C++14 std::experimental::filesystem
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(axonvex_core PUBLIC stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT APPLE)
    target_link_libraries(axonvex_core PUBLIC stdc++fs)
endif()
# Apple libc++ has no std::experimental::filesystem to link a *fs lib for
# (see filesystem_compat.hpp); it lives in libc++ proper from macOS 10.15,
# needing only the raised dialect below, not an extra -l flag. Below that
# deployment target it needs -lc++fs -- unverified here (no Apple toolchain
# available), left as a floor for whoever first ships below 10.15.

# The root CMakeLists.txt raises CMAKE_CXX_STANDARD to 17 under if(APPLE),
# but CMAKE_CXX_STANDARD only affects this directory's own targets, not
# consumers of the *installed* axonvex_core (its INTERFACE_COMPILE_FEATURES
# is otherwise empty). Export the requirement so it propagates through
# find_package(axonvex)/axonvex::core to every downstream consumer -- e.g.
# the conan test_package and examples/apps/axonvex_bridge_app, both pinned
# at cxx_std_14 -- forcing their compile to widen far enough for
# filesystem_compat.hpp's <filesystem> fallback to actually define anything.
if(APPLE)
    target_compile_features(axonvex_core PUBLIC cxx_std_17)
endif()

install(TARGETS axonvex_core
        EXPORT axonvexTargets
        LIBRARY DESTINATION axonvex_core/libs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_core/include
        PATTERN "*.in" EXCLUDE)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/axonvex_core/version.hpp"
        DESTINATION axonvex_core/include/axonvex_core)
