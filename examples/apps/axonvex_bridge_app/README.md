# axonvex_bridge_app — ROS 2 ↔ AxonVex String Echo Demo

A sample application demonstrating real ROS 2 integration via the
`ROS2Adapter`: it subscribes to `/chatter` (`std_msgs/String`), pipes each
message through an AxonVex processing pipeline, and prints it to the console.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  ROS 2 Network                                              │
│                                                             │
│   /chatter (std_msgs/String)                                │
│       │                                                     │
└───────┼─────────────────────────────────────────────────────┘
        │ rclcpp subscription (created by ROS2Adapter)
        ▼
┌──────────────────────┐
│  ROS2Adapter          │ ──► Owns the rclcpp::Node. A registered
│  (axonvex::ros2)      │     type caster converts std_msgs::String
└──────────┬───────────┘     ◄──► std::string in both directions.
           │ createSubscriber<std::string, std_msgs::msg::String>("/chatter")
           ▼
┌═══════════════════════════════════════════════════════════════┐
║  EchoSystem (AxonVexSystem)                                   ║
║                                                               ║
║  ┌────────────────────┐                                       ║
║  │ Subscriber unit     │ ◄── created by the adapter; writes    ║
║  │ (from ROS2Adapter)  │     each message to its output port   ║
║  └─────────┬──────────┘                                       ║
║            │  OutputPort<std::string>                          ║
║            ▼                                                  ║
║  ┌────────────────────┐                                       ║
║  │ PrinterPU           │ ◄── prints "#N: <message>" and        ║
║  │ port 0: input       │     counts messages received          ║
║  └────────────────────┘                                       ║
╚═══════════════════════════════════════════════════════════════╝
```

### Key patterns

1. **Type casters** — `main()` registers a caster pair on the adapter that
   converts `std_msgs::msg::String` ↔ `std::string`. The pipeline only ever
   sees `std::string`; ROS types stay at the adapter boundary.

2. **Adapter-created subscriber unit** — `EchoSystem::initializeBlocksLayout()`
   asks the adapter for a typed subscriber
   (`createSubscriber<std::string, std_msgs::msg::String>("/chatter")`) and
   registers it as a ProcessingUnit like any other, then wires its output
   port to `PrinterPU`.

3. **PrinterPU** — a pure AxonVex processing unit. It knows nothing about
   ROS — it only reads a typed input port.

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
cd examples/apps/axonvex_bridge_app
colcon build --packages-select axonvex_bridge_app \
    --cmake-args -DCMAKE_PREFIX_PATH=/path/to/AxonVex/install/libs
```

The app's `CMakeLists.txt` does `find_package(axonvex REQUIRED)` and links
`axonvex::core`, `axonvex::adapters`, and `axonvex::ros2`. Transitive
dependencies (Threads, TBB, nlohmann_json) are resolved by the AxonVex
CMake package config.

## Run

```bash
source install/setup.bash
ros2 run axonvex_bridge_app axonvex_bridge_node
```

In another terminal, publish a message:

```bash
ros2 topic pub /chatter std_msgs/msg/String "data: 'hello axonvex'"
```

The app prints each message as `[PrinterPU] #N: hello axonvex` and reports
the total message count on shutdown (Ctrl-C).
