message("############# AxonVex ROS2 Plugin CMake ##################")

find_package(rclcpp QUIET)
if(NOT rclcpp_FOUND)
    message(STATUS "rclcpp not found — skipping axonvex_ros2 plugin")
    return()
endif()

message(STATUS "rclcpp found — building axonvex_ros2 plugin")

add_library(axonvex_ros2 INTERFACE)
set_target_properties(axonvex_ros2 PROPERTIES EXPORT_NAME ros2)

target_include_directories(axonvex_ros2
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_ros2/include>
)

target_link_libraries(axonvex_ros2
    INTERFACE
        axonvex_core
        axonvex_adapters
        axonvex_plugins
        rclcpp::rclcpp
)

install(TARGETS axonvex_ros2
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_ros2/include)
