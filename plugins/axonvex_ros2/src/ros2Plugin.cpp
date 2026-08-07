#include <axonvex_plugins/pluginInterface.hpp>
#include <axonvex_ros2/ros2Plugin.hpp>
#include <cstdint>

// Non-inline definitions: this TU is what makes axonvex_ros2 a real,
// dlsym-able shared object (C13). Do not move these back into the header.

extern "C" axonvex::plugins::PluginInterface* axonvex_create_plugin() {
    return new axonvex::ros2::ROS2Plugin();
}

extern "C" void axonvex_destroy_plugin(axonvex::plugins::PluginInterface* p) {
    delete p;
}

extern "C" std::uint32_t axonvex_plugin_abi_version() {
    return axonvex::plugins::AXONVEX_PLUGIN_ABI_VERSION;
}
