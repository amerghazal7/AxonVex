# Standalone consumer of an *installed* AxonVex package via find_package().
# Deliberately not add_subdirectory()'d from the root CMakeLists — that would
# build against in-tree targets and never exercise the installed headers /
# axonvexConfig.cmake / SONAME that a real downstream project depends on.
#
# Usage against a plain `cmake --install` (no conan):
#   cmake -B build -DCMAKE_PREFIX_PATH=<axonvex install prefix>/lib/cmake/axonvex
#   cmake --build build
#   ./build/downstream_consumer
#
# Usage via conan (once conan is available in the environment): this is also
# the recipe's `test_package`, invoked automatically by `conan create ..`.
cmake_minimum_required(VERSION 3.20)
project(axonvex_downstream_consumer CXX)

find_package(axonvex REQUIRED COMPONENTS core)

add_executable(downstream_consumer downstream_consumer.cpp)
target_link_libraries(downstream_consumer PRIVATE axonvex::core)
target_compile_features(downstream_consumer PRIVATE cxx_std_14)
