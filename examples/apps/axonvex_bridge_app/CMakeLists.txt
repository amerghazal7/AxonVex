cmake_minimum_required(VERSION 3.20)
project(axonvex_bridge_app)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

find_package(axonvex REQUIRED)

add_executable(axonvex_bridge_node src/main.cpp)

target_link_libraries(axonvex_bridge_node
    axonvex::core
    axonvex::adapters
    axonvex::ros2
)

ament_target_dependencies(axonvex_bridge_node
    rclcpp
    std_msgs
)

install(TARGETS axonvex_bridge_node
    DESTINATION lib/${PROJECT_NAME}
)

ament_package()
