cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME "axonvex")

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_ENABLE_EXPORTS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Install prefix for native libs (used before project() for RPATH)
set(AXONVEX_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install/libs")

# Set RPATH so installed shared libs can find each other
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH
    "${AXONVEX_INSTALL_PREFIX}/axonvex_core/libs"
    "${AXONVEX_INSTALL_PREFIX}/axonvex_plugins/libs"
    "${AXONVEX_INSTALL_PREFIX}/axonvex_interfaces/libs"
    "${AXONVEX_INSTALL_PREFIX}/axonvex_safety/libs"
    "${AXONVEX_INSTALL_PREFIX}/axonvex_io/libs"
    "${AXONVEX_INSTALL_PREFIX}/axonvex_visualization/libs"
)

project("${PROJECT_NAME}"
    VERSION 1.0.0
    DESCRIPTION "A Real-Time Framework Built for Scalable, Precise Execution"
    LANGUAGES CXX)

# Set install prefix to project-local directory
set(CMAKE_INSTALL_PREFIX "${AXONVEX_INSTALL_PREFIX}" CACHE PATH "" FORCE)

option(BUILD_TESTING "Build unit tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_PLUGINS "Build middleware plugins (axonvex_ros2, etc.)" ON)

##################### Build AxonVex Libs ##########################
add_subdirectory(src/libs)

##################### Build Plugins (optional) ##########################
if(BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

##################### CMake Package Export ##########################
include(CMakePackageConfigHelpers)

install(EXPORT axonvexTargets
    NAMESPACE axonvex::
    DESTINATION lib/cmake/axonvex
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AxonVexConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/axonvexConfig.cmake"
    INSTALL_DESTINATION lib/cmake/axonvex
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/axonvexConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/axonvexConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/axonvexConfigVersion.cmake"
    DESTINATION lib/cmake/axonvex
)

##################### Build Tests ##########################
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_compile_definitions(BUILD_TESTING)
        enable_testing()
        add_executable(test_core
            tests/core/precisionTimerTest.cpp
            tests/core/circularBufferTest.cpp
            tests/core/threadSafeQueueTest.cpp
            tests/core/memoryPoolTest.cpp
            tests/core/loggerTest.cpp
            tests/core/configurationTest.cpp
            tests/core/pathTest.cpp
            tests/core/timingControllerTest.cpp
            tests/core/systemTest.cpp
            tests/core/callbackTest.cpp
            tests/core/portsTest.cpp
            tests/core/processingUnitTest.cpp
            tests/core/memoryPoolPortsTest.cpp
            tests/types/uuidTest.cpp
            tests/types/geometryTest.cpp
            tests/utils/lruCacheTest.cpp
            tests/utils/optionalTest.cpp
            tests/utils/ringBufferTest.cpp
            tests/utils/jsonSerializerTest.cpp
            tests/io/fileManagerTest.cpp
            tests/plugins/pluginSample.cpp
            tests/safety/watchdogTest.cpp
            tests/safety/safetyManagerTest.cpp
            tests/interfaces/protocolInterfacesContractTest.cpp
            tests/core/interfaceUnitsTest.cpp
            tests/core/missionTest.cpp
            tests/core/traceReplayTest.cpp
            tests/core/soakScalabilityTest.cpp
            tests/adapters/adapterContractTest.cpp
        )
        target_link_libraries(test_core
            PRIVATE
                axonvex_core
                axonvex_plugins
                axonvex_interfaces
                axonvex_safety
                axonvex_io
                axonvex_visualization
                axonvex_adapters
                GTest::GTest
                GTest::Main
        )
        # Register a single aggregated test plus per-test discovery for better reporting.
        add_test(NAME core_tests COMMAND test_core)
        include(GoogleTest)
        gtest_discover_tests(test_core
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            DISCOVERY_TIMEOUT 30
        )
        add_custom_target(run_tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS test_core
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    else()
        message(STATUS "GTest not found; skipping tests.")
    endif()
endif()

##################### Build Examples ##########################
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
