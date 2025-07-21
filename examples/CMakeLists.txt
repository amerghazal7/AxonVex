# Examples for AxonVex Framework

# PrecisionTimer example
add_executable(precision_timer_example precision_timer_example.cpp)
target_link_libraries(precision_timer_example axonvex)

# ThreadSafeQueue example
add_executable(threadSafeQueue_example threadSafeQueueExample.cpp)
target_link_libraries(threadSafeQueue_example axonvex)

# Logger example (with stream interface)
add_executable(logger_stream_example logger_stream_example.cpp)
target_link_libraries(logger_stream_example axonvex)

# Configuration example
add_executable(configuration_example configuration_example.cpp)
target_link_libraries(configuration_example axonvex)

# Path example
add_executable(path_example path_example.cpp)
target_link_libraries(path_example axonvex)

# Timing controller example (temporarily disabled due to implementation complexity)
# add_executable(timing_controller_example timing_controller_example.cpp)
# target_link_libraries(timing_controller_example axonvex)

# System Management example - re-enabled with basic fixes 
add_executable(system_example_simple system_example_simple.cpp)
target_link_libraries(system_example_simple axonvex)

# Subsystem Composition example (temporarily disabled due to namespace ambiguity)
# add_executable(subsystem_example subsystem_example.cpp)
# target_link_libraries(subsystem_example axonvex) 

# Re-enabling optimization demo with simplified error handling
add_executable(optimization_demo optimization_demo.cpp)
target_link_libraries(optimization_demo axonvex) 