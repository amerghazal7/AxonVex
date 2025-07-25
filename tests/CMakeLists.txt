# AxonVex Tests Directory

# Common test settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Google Test configuration
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Precision Timer Tests
add_executable(precision_timer_test 
    core/precisionTimerTest.cpp
)

target_link_libraries(precision_timer_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

# Set test properties
set_target_properties(precision_timer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add test to CTest
add_test(NAME PrecisionTimerTest COMMAND precision_timer_test)
set_tests_properties(PrecisionTimerTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
)

# Circular Buffer Tests
add_executable(circular_buffer_test 
    core/circularBufferTest.cpp
)

target_link_libraries(circular_buffer_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(circular_buffer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME CircularBufferTest COMMAND circular_buffer_test)
set_tests_properties(CircularBufferTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
)

# Thread Safe Queue Tests
add_executable(thread_safe_queue_test 
    core/threadSafeQueueTest.cpp
)

target_link_libraries(thread_safe_queue_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(thread_safe_queue_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME ThreadSafeQueueTest COMMAND thread_safe_queue_test)
set_tests_properties(ThreadSafeQueueTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

# Memory Pool Tests
add_executable(memory_pool_test 
    core/memoryPoolTest.cpp
)

target_link_libraries(memory_pool_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(memory_pool_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME MemoryPoolTest COMMAND memory_pool_test)
set_tests_properties(MemoryPoolTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
)

# Test labels for organization
set_tests_properties(PrecisionTimerTest PROPERTIES LABELS "core;unit")
set_tests_properties(CircularBufferTest PROPERTIES LABELS "core;unit")
set_tests_properties(ThreadSafeQueueTest PROPERTIES LABELS "core;unit")
set_tests_properties(MemoryPoolTest PROPERTIES LABELS "core;unit")

# Enable parallel test execution
# Note: RESOURCE_GROUPS property disabled due to compatibility issues
# if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
#     set_tests_properties(PrecisionTimerTest CircularBufferTest ThreadSafeQueueTest MemoryPoolTest
#         PROPERTIES RESOURCE_GROUPS "CPU:2")
# endif()

# Add custom test target for running all core tests
add_custom_target(test_core
    COMMAND ${CMAKE_CTEST_COMMAND} -L core --output-on-failure
    DEPENDS precision_timer_test circular_buffer_test thread_safe_queue_test memory_pool_test callback_test ports_test processing_unit_v2_test memory_pool_ports_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all core component tests"
)

# Logger Tests
add_executable(logger_test 
    core/loggerTest.cpp
)

target_link_libraries(logger_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

# Set test properties
set_target_properties(logger_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add test to CTest
add_test(NAME LoggerTest COMMAND logger_test)
set_tests_properties(LoggerTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
    LABELS "unit"
)

# Add custom test target for running all unit tests  
add_custom_target(test_unit
    COMMAND ${CMAKE_CTEST_COMMAND} -L unit --output-on-failure
    DEPENDS precision_timer_test circular_buffer_test thread_safe_queue_test memory_pool_test logger_test callback_test ports_test processing_unit_v2_test memory_pool_ports_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
) 

add_executable(configuration_test core/configurationTest.cpp)
target_link_libraries(configuration_test axonvex gtest)

# Path class test
add_executable(path_test core/pathTest.cpp)
target_link_libraries(path_test axonvex gtest)



# TimingController tests  
add_executable(timing_controller_test core/timingControllerTest.cpp)
target_link_libraries(timing_controller_test axonvex gtest)
add_test(NAME TimingControllerTest COMMAND timing_controller_test)

# AxonVexSystem tests
add_executable(system_test core/systemTest.cpp)
target_link_libraries(system_test axonvex gtest)
add_test(NAME SystemTest COMMAND system_test)

# Callback System Tests
add_executable(callback_test 
    core/callbackTest.cpp
)

target_link_libraries(callback_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(callback_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME CallbackTest COMMAND callback_test)
set_tests_properties(CallbackTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
    LABELS "core;unit"
)

# Enhanced tests commented out - replaced by new implementation
# Enhanced Port System Tests
# add_executable(enhanced_ports_test
#     core/enhancedPortsTest.cpp
# )

# target_link_libraries(enhanced_ports_test
#     axonvex
#     GTest::gtest
#     GTest::gtest_main
#     pthread
# )

# set_target_properties(enhanced_ports_test PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     CXX_STANDARD 17
#     CXX_STANDARD_REQUIRED ON
# )

# add_test(NAME EnhancedPortsTest COMMAND enhanced_ports_test)
# set_tests_properties(EnhancedPortsTest PROPERTIES
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     TIMEOUT 30
#     LABELS "core;unit"
# )

# Enhanced Processing Unit Tests
# add_executable(enhanced_processing_unit_test
#     core/enhancedProcessingUnitTest.cpp
# )

# target_link_libraries(enhanced_processing_unit_test
#     axonvex
#     GTest::gtest
#     GTest::gtest_main
#     pthread
# )

# set_target_properties(enhanced_processing_unit_test PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     CXX_STANDARD 17
#     CXX_STANDARD_REQUIRED ON
# )

# add_test(NAME EnhancedProcessingUnitTest COMMAND enhanced_processing_unit_test)
# set_tests_properties(EnhancedProcessingUnitTest PROPERTIES
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     TIMEOUT 30
#     LABELS "core;unit"
# )

# New Port System Tests (replacing enhanced versions)
add_executable(ports_test
    core/portsTest.cpp
)

target_link_libraries(ports_test
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(ports_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME PortsTest COMMAND ports_test)
set_tests_properties(PortsTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
    LABELS "core;unit;v2"
)

# New Processing Unit Tests
add_executable(processing_unit_v2_test
    core/processingUnitTest.cpp
)

target_link_libraries(processing_unit_v2_test
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(processing_unit_v2_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME ProcessingUnitV2Test COMMAND processing_unit_v2_test)
set_tests_properties(ProcessingUnitV2Test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
    LABELS "core;unit;v2"
)

# MemoryPool integration with ports test
add_executable(memory_pool_ports_test
    core/memoryPoolPortsTest.cpp
)

target_link_libraries(memory_pool_ports_test
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(memory_pool_ports_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME MemoryPoolPortsTest COMMAND memory_pool_ports_test)
set_tests_properties(MemoryPoolPortsTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
    LABELS "core;unit;v2;memory"
)

# Types Module Tests
add_executable(types_test 
    types/typesTest.cpp
)

target_link_libraries(types_test 
    axonvex
    GTest::gtest
    GTest::gtest_main
    pthread
)

set_target_properties(types_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME TypesTest COMMAND types_test)
set_tests_properties(TypesTest PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 60
    LABELS "types;unit;phase3"
)