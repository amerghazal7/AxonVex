#pragma once

// Only ever included when AXONVEX_ROS2_HAS_STD_MSGS is defined
// (plugins/axonvex_ros2/CMakeLists.txt sets it after a real
// find_package(std_msgs QUIET) success — never assumed).
#ifndef AXONVEX_ROS2_HAS_STD_MSGS
#error "defaultCasters.hpp included without AXONVEX_ROS2_HAS_STD_MSGS defined"
#endif

#include <axonvex_ros2/typeCasterRegistry.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>

namespace axonvex::ros2 {

/**
 * @brief Register the four primitive InternalType<->RosMsg casters that
 * std_msgs backs directly: float<->Float32, double<->Float64,
 * bool<->Bool, std::string<->String.
 *
 * Opt-in: apps needing anything beyond these four call
 * ROS2Adapter::registerTypeCaster() themselves. Call once per
 * TypeCasterRegistry before wiring subscribers/publishers that use these
 * exact pairs.
 */
inline void registerDefaultCasters(TypeCasterRegistry& registry) {
    registry.registerCaster<float, std_msgs::msg::Float32>(
        [](const std_msgs::msg::Float32& msg) { return msg.data; },
        [](const float& value) {
            std_msgs::msg::Float32 msg;
            msg.data = value;
            return msg;
        });

    registry.registerCaster<double, std_msgs::msg::Float64>(
        [](const std_msgs::msg::Float64& msg) { return msg.data; },
        [](const double& value) {
            std_msgs::msg::Float64 msg;
            msg.data = value;
            return msg;
        });

    registry.registerCaster<bool, std_msgs::msg::Bool>(
        [](const std_msgs::msg::Bool& msg) { return msg.data; },
        [](const bool& value) {
            std_msgs::msg::Bool msg;
            msg.data = value;
            return msg;
        });

    registry.registerCaster<std::string, std_msgs::msg::String>(
        [](const std_msgs::msg::String& msg) { return msg.data; },
        [](const std::string& value) {
            std_msgs::msg::String msg;
            msg.data = value;
            return msg;
        });
}

} // namespace axonvex::ros2
