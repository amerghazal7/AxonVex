# Examples for AxonVex Framework

# PrecisionTimer example
add_executable(precision_timer_example precision_timer_example.cpp)
target_link_libraries(precision_timer_example axonvex_core)

# ThreadSafeQueue example
add_executable(threadSafeQueue_example threadSafeQueueExample.cpp)
target_link_libraries(threadSafeQueue_example axonvex_core)

# Logger example (with stream interface)
add_executable(logger_stream_example logger_stream_example.cpp)
target_link_libraries(logger_stream_example axonvex_core)

# Configuration example
add_executable(configuration_example configuration_example.cpp)
target_link_libraries(configuration_example axonvex_core)

# Path example
add_executable(path_example path_example.cpp)
target_link_libraries(path_example axonvex_core)

# Timing controller example
add_executable(timing_controller_example timing_controller_example.cpp)
target_link_libraries(timing_controller_example axonvex_core)

# System Management example
add_executable(system_example_simple system_example_simple.cpp)
target_link_libraries(system_example_simple axonvex_core)

# Subsystem Composition example
add_executable(subsystem_example subsystem_example.cpp)
target_link_libraries(subsystem_example axonvex_core)

# Optimization demo
add_executable(optimization_demo optimization_demo.cpp)
target_link_libraries(optimization_demo axonvex_core)

# Callback system example
add_executable(callback_example callback_example.cpp)
target_link_libraries(callback_example axonvex_core)

# Simple callback demo
add_executable(simple_callback_demo simple_callback_demo.cpp)
target_link_libraries(simple_callback_demo axonvex_core)

# Custom system architecture example
add_executable(custom_system_example custom_system_example.cpp)
target_link_libraries(custom_system_example axonvex_core)

# Full system management example
add_executable(system_example system_example.cpp)
target_link_libraries(system_example axonvex_core)

# Telemetry bus example
add_executable(telemetry_bus_example telemetry_bus_example.cpp)
target_link_libraries(telemetry_bus_example axonvex_core axonvex_visualization)
