# AxonVex Examples

# Example executable: PrecisionTimer demonstration
add_executable(precision_timer_example precision_timer_example.cpp)

# Link to the AxonVex library
target_link_libraries(precision_timer_example PRIVATE axonvex)

# Set properties
set_target_properties(precision_timer_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Apply optimization flags
target_compile_options(precision_timer_example PRIVATE
    ${AXONVEX_WARNING_FLAGS}
    ${AXONVEX_RT_FLAGS}
    $<$<CONFIG:Debug>:${AXONVEX_RT_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${AXONVEX_RT_RELEASE_FLAGS}>
)

# Copy to build directory for easy execution
add_custom_command(TARGET precision_timer_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:precision_timer_example> ${CMAKE_BINARY_DIR}/
    COMMENT "Copying precision_timer_example to build directory"
)

# Install the example
install(TARGETS precision_timer_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)

# Additional examples can be added here as we implement more components

# Example executable: ThreadSafeQueue demonstration
add_executable(thread_safe_queue_example threadSafeQueueExample.cpp)

# Link to the AxonVex library
target_link_libraries(thread_safe_queue_example PRIVATE axonvex)

# Set properties
set_target_properties(thread_safe_queue_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Apply optimization flags
target_compile_options(thread_safe_queue_example PRIVATE
    ${AXONVEX_WARNING_FLAGS}
    ${AXONVEX_RT_FLAGS}
    $<$<CONFIG:Debug>:${AXONVEX_RT_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${AXONVEX_RT_RELEASE_FLAGS}>
)

# Copy to build directory for easy execution
add_custom_command(TARGET thread_safe_queue_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:thread_safe_queue_example> ${CMAKE_BINARY_DIR}/
    COMMENT "Copying thread_safe_queue_example to build directory"
)

# Install the example
install(TARGETS thread_safe_queue_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
)

# Example executable: Logger Stream Interface demonstration
add_executable(logger_stream_example logger_stream_example.cpp)

# Link to the AxonVex library
target_link_libraries(logger_stream_example PRIVATE axonvex)

# Set properties
set_target_properties(logger_stream_example PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Apply optimization flags
target_compile_options(logger_stream_example PRIVATE
    ${AXONVEX_WARNING_FLAGS}
    ${AXONVEX_RT_FLAGS}
    $<$<CONFIG:Debug>:${AXONVEX_RT_DEBUG_FLAGS}>
    $<$<CONFIG:Release>:${AXONVEX_RT_RELEASE_FLAGS}>
)

# Copy to build directory for easy execution
add_custom_command(TARGET logger_stream_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:logger_stream_example> ${CMAKE_BINARY_DIR}/
    COMMENT "Copying logger_stream_example to build directory"
)

# Install the example
install(TARGETS logger_stream_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
) 