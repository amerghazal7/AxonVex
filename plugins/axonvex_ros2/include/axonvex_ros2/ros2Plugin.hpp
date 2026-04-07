#pragma once

#include <axonvex_plugins/pluginInterface.hpp>
#include <string>

namespace axonvex::ros2 {

class ROS2Plugin : public axonvex::plugins::PluginInterface {
  public:
    std::string name() const override { return "axonvex_ros2"; }
    std::string version() const override { return "1.0.0"; }
    bool initialize() override { return true; }
    void shutdown() override {}
};

} // namespace axonvex::ros2

extern "C" inline axonvex::plugins::PluginInterface* axonvex_create_plugin() {
    return new axonvex::ros2::ROS2Plugin();
}

extern "C" inline void axonvex_destroy_plugin(axonvex::plugins::PluginInterface* p) {
    delete p;
}
