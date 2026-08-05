#pragma once

#include <axonvex_plugins/pluginInterface.hpp>
#include <string>

namespace axonvex::ros2 {

class ROS2Plugin : public axonvex::plugins::PluginInterface {
  public:
    std::string name() const override {
        return "axonvex_ros2";
    }
    std::string version() const override {
        return "1.0.0";
    }
    bool initialize() override {
        return true;
    }
    void shutdown() override {}
};

} // namespace axonvex::ros2

// The extern "C" plugin entry points (axonvex_create_plugin,
// axonvex_destroy_plugin, axonvex_plugin_abi_version) are defined once, as
// ordinary (non-inline) functions, in src/ros2Plugin.cpp — that TU is what
// makes this a real, dlsym-able .so. See pluginInterface.hpp for the ABI
// handshake contract.
