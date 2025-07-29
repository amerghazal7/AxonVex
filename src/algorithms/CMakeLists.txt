# AxonVex Algorithms Module - Header-only implementation for optimal performance
# This module provides high-performance mathematical algorithms optimized for real-time applications

# Create interface library for header-only algorithms
add_library(axonvex_algorithms INTERFACE)


target_link_libraries(axonvex_algorithms
    INTERFACE
        axonvex
)

# Set compile features for C++17
target_compile_features(axonvex_algorithms INTERFACE cxx_std_17)

# Add compiler-specific optimizations for algorithms
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(axonvex_algorithms INTERFACE
        -Wall -Wextra -O3 -march=native -ffast-math
    )
endif()

# Install rules for header-only library
install(TARGETS axonvex_algorithms
    EXPORT AxonVexTargets
)

# Install algorithm headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/axonvex/algorithms
    DESTINATION include/axonvex
    FILES_MATCHING PATTERN "*.hpp"
)