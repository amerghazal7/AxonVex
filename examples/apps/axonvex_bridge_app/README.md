# axonvex_bridge_app — ROS 2 ↔ AxonVex Bridge

A sample application demonstrating how the AxonVex adapter layer connects
a ROS 2 network to an AxonVex processing pipeline.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  ROS 2 Network                                              │
│                                                             │
│   /imu/data (sensor_msgs/Imu)                               │
│       │                                                     │
└───────┼─────────────────────────────────────────────────────┘
        │ rclcpp subscription
        ▼
┌──────────────────┐     Adapter Layer (outside the system)
│   Ros2AdapterNode │ ──► Converts sensor_msgs::Imu to bytes,
│   (rclcpp::Node) │     calls adapter.publish("sensor/imu", payload)
└──────────────────┘
        │
        ▼
┌──────────────────┐
│   RosAdapter      │ ──► Manages topic subscriptions and fan-out.
│   (AdapterInterface)    In simulation: loopback. In production:
└──────────────────┘     backed by real rclcpp transport.
        │ adapter.subscribe("sensor/imu", callback)
        ▼
┌═══════════════════════════════════════════════════════════════┐
║  AxonVexSystem ("ImuPipeline")                                ║
║                                                               ║
║  ┌──────────────────┐                                         ║
║  │ AdapterBridgePU   │ ◄── adapter callback writes to port    ║
║  │ port 100: imu_out │                                        ║
║  └────────┬─────────┘                                         ║
║           │  OutputPort<ImuData>                               ║
║           ▼                                                   ║
║  ┌──────────────────┐                                         ║
║  │ ImuProcessorPU    │ ◄── complementary filter                ║
║  │ port 200: imu_in  │                                        ║
║  │ port 201: att_out │                                        ║
║  └────────┬─────────┘                                         ║
║           │  OutputPort<AttitudeEstimate>                      ║
║           ▼                                                   ║
║  ┌──────────────────┐                                         ║
║  │ DataLoggerPU      │ ◄── prints roll/pitch/yaw to console   ║
║  │ port 300: att_in  │                                        ║
║  └──────────────────┘                                         ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
```

### Key patterns

1. **Ros2AdapterNode** — the only file that touches ROS types. It converts
   `sensor_msgs::Imu` into a byte buffer and calls `adapter.publish()`.

2. **AdapterBridgePU** — a ProcessingUnit that subscribes to the adapter
   via callbacks, decodes the bytes, and writes typed data to its output
   port. This is the **bridge** between the callback-based adapter world
   and the port-based processing pipeline.

3. **ImuProcessorPU / DataLoggerPU** — pure AxonVex processing units.
   They know nothing about ROS or adapters — they only see typed ports.

## Prerequisites

- ROS 2 (Humble or later)
- AxonVex libraries built and installed
- Conan dependencies (nlohmann_json, onetbb)

## Build

```bash
# Source ROS 2
source /opt/ros/humble/setup.bash

# Build and install AxonVex libraries
cd /path/to/AxonVex
conan install . --output-folder=build --build=missing
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
cmake --install build   # installs to install/libs by default

# Build this ROS 2 package (point CMAKE_PREFIX_PATH at the AxonVex install)
cd src/apps/axonvex_bridge_app
colcon build --packages-select axonvex_bridge_app \
    --cmake-args -DCMAKE_PREFIX_PATH=/path/to/AxonVex/install/libs
```

The app's `CMakeLists.txt` only does `find_package(axonvex REQUIRED)` and
links `axonvex::core` + `axonvex::adapters`. All transitive dependencies
(Threads, TBB, nlohmann_json) are resolved automatically by the AxonVex
CMake package config.

## Run

```bash
source install/setup.bash
ros2 run axonvex_bridge_app axonvex_bridge_node
```

In another terminal, publish IMU data:

```bash
ros2 topic pub /imu/data sensor_msgs/msg/Imu "{
  linear_acceleration: {x: 0.1, y: 0.0, z: 9.81},
  angular_velocity: {x: 0.01, y: 0.02, z: 0.0}
}" --rate 100
```

You should see attitude estimates printed by the DataLoggerPU.
