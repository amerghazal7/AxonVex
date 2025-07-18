cmake_minimum_required(VERSION 3.20)

# AxonVex Framework Project
project(AxonVex
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "A Real-Time Framework Built for Scalable, Precise Execution"
    HOMEPAGE_URL "https://github.com/axonvex/axonvex-framework"
)

# Project configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(AXONVEX_BUILD_TESTS "Build AxonVex tests" ON)
option(AXONVEX_BUILD_EXAMPLES "Build AxonVex examples" ON)
option(AXONVEX_BUILD_DOCS "Build AxonVex documentation" OFF)
option(AXONVEX_BUILD_PLUGINS "Build AxonVex plugins" ON)
option(AXONVEX_ENABLE_REAL_TIME "Enable real-time optimizations" ON)
option(AXONVEX_ENABLE_PROFILING "Enable performance profiling" OFF)

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Platform-specific configurations
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(AXONVEX_PLATFORM_LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(AXONVEX_PLATFORM_WINDOWS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(AXONVEX_PLATFORM_MACOS ON)
endif()

# Compiler-specific configurations
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(AXONVEX_COMPILER_GCC ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(AXONVEX_COMPILER_CLANG ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(AXONVEX_COMPILER_MSVC ON)
endif()

# Real-time optimization flags
if(AXONVEX_ENABLE_REAL_TIME)
    if(AXONVEX_COMPILER_GCC OR AXONVEX_COMPILER_CLANG)
        # GCC/Clang real-time optimizations
        set(AXONVEX_RT_FLAGS
            -O3                     # High optimization level
            -march=native           # Use native CPU instructions
            -mtune=native           # Tune for native CPU
            -ffast-math             # Fast math operations
            -funroll-loops          # Unroll loops
            -finline-functions      # Inline functions
            -fomit-frame-pointer    # Omit frame pointer
            # -fno-exceptions         # Keep exceptions for error handling
            -fno-rtti               # Disable RTTI for RT
        )
        
        # Additional flags for debug/release
        set(AXONVEX_RT_DEBUG_FLAGS
            -g                      # Debug symbols
            -DDEBUG                 # Debug macro
        )
        
        set(AXONVEX_RT_RELEASE_FLAGS
            -DNDEBUG                # Release macro
            -flto                   # Link-time optimization
        )
        
        # Thread-related flags
        if(AXONVEX_PLATFORM_LINUX)
            list(APPEND AXONVEX_RT_FLAGS -pthread)
        endif()
        
    elseif(AXONVEX_COMPILER_MSVC)
        # MSVC real-time optimizations
        set(AXONVEX_RT_FLAGS
            /O2                     # Optimize for speed
            /Ot                     # Favor fast code
            /Oy                     # Omit frame pointers
            /GL                     # Whole program optimization
            /GS-                    # Disable buffer security checks
            /fp:fast                # Fast floating point
        )
        
        set(AXONVEX_RT_DEBUG_FLAGS
            /Zi                     # Debug symbols
            /DDEBUG                 # Debug macro
        )
        
        set(AXONVEX_RT_RELEASE_FLAGS
            /DNDEBUG                # Release macro
            /LTCG                   # Link-time code generation
        )
    endif()
endif()

# Warning flags
if(AXONVEX_COMPILER_GCC OR AXONVEX_COMPILER_CLANG)
    set(AXONVEX_WARNING_FLAGS
        -Wall                   # Enable all warnings
        -Wextra                 # Extra warnings
        -Wpedantic              # Pedantic warnings
        -Werror                 # Treat warnings as errors
        -Wno-unused-parameter   # Ignore unused parameters
        -Wno-missing-field-initializers  # Ignore missing field initializers
    )
elseif(AXONVEX_COMPILER_MSVC)
    set(AXONVEX_WARNING_FLAGS
        /W4                     # Warning level 4
        /WX                     # Treat warnings as errors
    )
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Conan integration (if using Conan)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake")
    include(${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake)
endif()

# Create main AxonVex library
add_library(axonvex STATIC)

# Set target properties
set_target_properties(axonvex PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Apply compiler flags
target_compile_options(axonvex PRIVATE
    ${AXONVEX_WARNING_FLAGS}
    ${AXONVEX_RT_FLAGS}
    $<$<CONFIG:Debug>:${AXONVEX_RT_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${AXONVEX_RT_RELEASE_FLAGS}>
)

# Include directories
target_include_directories(axonvex
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(axonvex
    PUBLIC
        Threads::Threads
    PRIVATE
        $<$<PLATFORM_ID:Linux>:rt>      # Real-time library on Linux
        $<$<PLATFORM_ID:Windows>:winmm> # Windows multimedia library
)

# Preprocessor definitions
target_compile_definitions(axonvex
    PRIVATE
        AXONVEX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        AXONVEX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        AXONVEX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        $<$<BOOL:${AXONVEX_ENABLE_REAL_TIME}>:AXONVEX_REAL_TIME_ENABLED>
        $<$<BOOL:${AXONVEX_ENABLE_PROFILING}>:AXONVEX_PROFILING_ENABLED>
        $<$<PLATFORM_ID:Linux>:AXONVEX_PLATFORM_LINUX>
        $<$<PLATFORM_ID:Windows>:AXONVEX_PLATFORM_WINDOWS>
        $<$<PLATFORM_ID:Darwin>:AXONVEX_PLATFORM_MACOS>
)

# Add source subdirectories
add_subdirectory(src)

# Testing
if(AXONVEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(AXONVEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Documentation
if(AXONVEX_BUILD_DOCS)
    add_subdirectory(docs)
endif()

# Plugins
if(AXONVEX_BUILD_PLUGINS)
    add_subdirectory(src/plugins)
endif()

# Installation
include(GNUInstallDirs)

# Install targets
install(TARGETS axonvex
    EXPORT AxonVexTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/axonvex
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install export file
install(EXPORT AxonVexTargets
    FILE AxonVexTargets.cmake
    NAMESPACE AxonVex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxonVex
)

# Create config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AxonVexConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AxonVexConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxonVex
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/AxonVexConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/AxonVexConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/AxonVexConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AxonVex
)

# Package configuration
set(CPACK_PACKAGE_NAME "AxonVex")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A Real-Time Framework Built for Scalable, Precise Execution")
set(CPACK_PACKAGE_VENDOR "AxonVex Framework")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

include(CPack)

# Status output
message(STATUS "AxonVex Framework v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Real-time optimizations: ${AXONVEX_ENABLE_REAL_TIME}")
message(STATUS "Building tests: ${AXONVEX_BUILD_TESTS}")
message(STATUS "Building examples: ${AXONVEX_BUILD_EXAMPLES}")
message(STATUS "Building documentation: ${AXONVEX_BUILD_DOCS}")
message(STATUS "Building plugins: ${AXONVEX_BUILD_PLUGINS}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}") 