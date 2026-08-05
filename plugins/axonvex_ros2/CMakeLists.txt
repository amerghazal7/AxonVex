message("############# AxonVex ROS2 Plugin CMake ##################")

find_package(rclcpp QUIET)
if(NOT rclcpp_FOUND)
    message(STATUS "rclcpp not found — skipping axonvex_ros2 plugin")
    return()
endif()

message(STATUS "rclcpp found — building axonvex_ros2 plugin")

add_library(axonvex_ros2 SHARED
    src/ros2Plugin.cpp
)
set_target_properties(axonvex_ros2 PROPERTIES EXPORT_NAME ros2)

target_include_directories(axonvex_ros2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_ros2/include>
)

target_link_libraries(axonvex_ros2
    PUBLIC
        axonvex_core
        axonvex_adapters
        axonvex_plugins
        rclcpp::rclcpp
)

# defaultCasters.hpp (Float32<->float, Float64<->double, Bool<->bool,
# String<->std::string) is opt-in on std_msgs actually being present —
# never assume it. When absent, ROS2Adapter::registerDefaultCasters() does
# not exist rather than silently registering nothing (C20).
find_package(std_msgs QUIET)
if(std_msgs_FOUND)
    message(STATUS "std_msgs found — enabling axonvex_ros2 default type casters")
    target_link_libraries(axonvex_ros2 PUBLIC std_msgs::std_msgs__rosidl_generator_cpp)
    target_compile_definitions(axonvex_ros2 PUBLIC AXONVEX_ROS2_HAS_STD_MSGS)
else()
    message(STATUS "std_msgs not found — axonvex_ros2 default type casters disabled")
endif()

install(TARGETS axonvex_ros2
        EXPORT axonvexTargets
        LIBRARY DESTINATION axonvex_ros2/libs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_ros2/include)
