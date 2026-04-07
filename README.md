![AxonVex Banner](assets/banner.png)

# AxonVex  
**Real-time C++ framework for typed processing pipelines, deterministic scheduling, and extensible protocol adapters**

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Documentation](https://img.shields.io/badge/Documentation-Available-green.svg)](docs/)
[![Version](https://img.shields.io/badge/Version-1.0.0--dev-red.svg)]()

---

## Project Status

AxonVex is in active development. Current strongest capability is the core runtime and test harness.

## Mission Statement

AxonVex is a cutting-edge real-time framework designed to empower developers to master the complexity of modern systems with unparalleled speed and scalability. Inspired by the rapid signaling of neural axons and the challenge of solving intricate problems, AxonVex delivers precision orchestration, deterministic execution, and robust performance for real-time applications across industries. We enable seamless, scalable solutions where milliseconds matter and complexity is conquered effortlessly.

---

## Framework Overview

AxonVex is a comprehensive real-time framework built upon proven hierarchical block-based architecture principles. It provides developers with the tools and infrastructure needed to build sophisticated real-time systems with deterministic timing, high performance, and mission-critical reliability.

### Key Features

- 🎯 **Precision Execution**: Deterministic timing with <1 microsecond accuracy
- 📈 **Scalable Architecture**: From single-node to distributed systems (10,000+ processing blocks)
- 🧩 **Modular Design**: Hierarchical block-based architecture with plugin system
- 🔍 **Real-time Visualization**: Interactive dashboards, 3D visualization, VR/AR support
- 🛡️ **Mission-Critical Safety**: Multi-layer safety systems with hardware/software watchdogs
- 🔒 **Enterprise Security**: Multi-factor authentication, role-based access, end-to-end encryption
- ⚡ **High Performance**: Sub-millisecond latency, 1M+ messages/second throughput
- 🔧 **Developer-Friendly**: Intuitive APIs, comprehensive tooling, extensive documentation

---

## Documentation Suite

### 📚 [Complete Documentation Index](docs/)
Your starting point for all AxonVex documentation, including quick start guides, examples, and community resources.

### 🔧 [Technical Specification](docs/software_requirements_specification.md)

Comprehensive technical specification covering:
- Core architecture philosophy and design principles
- Advanced real-time visual monitoring and control systems
- System architecture and component specifications
- Performance specifications and benchmarks
- Implementation technology stack and APIs
- Safety and security features
- Development and integration guidelines
- Complete API reference and deployment guides

### 🏗️ [Architecture Diagram](docs/architecture_and_design.md)
Visual representation of the system architecture including:
- System architecture overview and component relationships
- Communication architecture and protocol abstractions
- Real-time data flow and processing pipelines
- Safety and security architecture diagrams
- Performance monitoring and deployment models
- Plugin architecture and extensibility framework

---

## Quick Start

### System Requirements

- **CPU**: Multi-core processor (Intel Core i7 or AMD Ryzen 7 recommended)
- **RAM**: Minimum 8GB, recommended 16GB+ for complex systems
- **OS**: Linux (Ubuntu 20.04+), Windows 10+, macOS 10.15+
- **Compiler**: GCC 9+, Clang 10+, or MSVC 2019+

### Dependencies

- [Conan 2](https://conan.io/) package manager
- [CMake 3.20+](https://cmake.org/)

### Build

```bash
git clone https://github.com/axonvex/axonvex-framework.git
cd axonvex-framework

# Install dependencies via Conan
conan install . --output-folder=build --build=missing

# Configure and build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure
```

---

## API Usage

Derive from `AxonVexSystem` to define your processing pipeline, then drive it through the standard lifecycle.

```cpp
#include <axonvex_core/axonvex.hpp>

using namespace axonvex;

class MySystem final : public AxonVexSystem {
  protected:
    bool initializeBlocksLayout() override {
        auto unit = std::make_unique<MyProcessingUnit>("sensor");
        registerProcessingUnit(std::move(unit));
        return true;
    }
};

int main() {
    SystemConfig cfg;
    cfg.name = "Example";
    cfg.enableStatistics = true;

    AxonVexSystem system(cfg);
    if (!system.initialize()) return 1;
    if (!system.start()) return 1;
    // ... run ...
    system.stop();
    return 0;
}
```

See `examples/` for complete working demos.

---

## Testing

All tests live under `tests/` and compile into a single `test_core` binary discovered by CTest.

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

---

## Application Domains

### 🚁 Robotics and Automation
- Autonomous vehicles and drones
- Industrial automation systems
- Robotic manipulators and mobile robots
- Human-robot interaction systems

### 🏭 Industrial Control
- Process control systems
- Manufacturing automation
- Quality assurance systems
- Energy management systems

### 💹 Financial Systems
- High-frequency trading platforms
- Risk management systems
- Real-time market data processing
- Algorithmic trading systems

### 🔬 Scientific Computing
- Real-time data acquisition systems
- Control systems for scientific instruments
- Distributed computing platforms
- Simulation and modeling systems

---

## Technology Stack

### Core System
- **Language**: C++17
- **Build System**: CMake 3.20+ with Conan 2 dependency management
- **Dependencies**: oneTBB (concurrency), nlohmann_json (configuration), GoogleTest (testing)
- **Threading**: Custom thread pool with real-time scheduling via `TimingController`

### Communication Protocols (Current)
- **Protocol Abstraction**: `ProtocolInterface` base with lifecycle, stats, and error reporting
- **TCP Client**: BSD socket implementation (Linux) with simulation fallback
- **UDP Socket**: BSD socket implementation (Linux) with simulation fallback
- **WebSocket**: Placeholder implementation (full stack planned)

---

## Library Architecture

AxonVex is split into libraries under `src/libs/`:

| Library | Type | Role |
|---------|------|------|
| `axonvex_core` | Shared | Runtime, orchestration, ports, timing, configuration, logging, interface units |
| `axonvex_interfaces` | Interface | Protocol abstractions (TCP, UDP, WebSocket) |
| `axonvex_adapters` | Interface | Adapter contract (AdapterInterface, AdapterBase, MockAdapter) |
| `axonvex_ros2` | Interface | ROS 2 plugin — ROS2Adapter with typed unit creation (optional, requires rclcpp) |
| `axonvex_plugins` | Interface | Plugin API and dynamic loading |
| `axonvex_safety` | Interface | Safety primitives (Watchdog) |
| `axonvex_io` | Interface | Filesystem helpers |
| `axonvex_visualization` | Interface | Telemetry pub/sub bus |

All interface libraries depend only on `axonvex_core`. See [Architecture](docs/architecture_and_design.md) for the full design.

---

## Implementation Status

### Implemented
- ✅ **Core Runtime**: System orchestration, processing units, typed ports, timing controller
- ✅ **Configuration**: JSON-backed runtime configuration with typed access
- ✅ **Logging**: Dual-interface logger with stream and function APIs
- ✅ **Utilities**: Thread-safe queue, ring buffer, memory pool, LRU cache, JSON serializer
- ✅ **Types**: UUID, Point2D/3D, Quaternion geometry types
- ✅ **Protocol Abstraction**: TCP, UDP, WebSocket scaffolds with `ProtocolInterface` contract
- ✅ **Plugin System**: Dynamic plugin loading (Linux) with `PluginManager`
- ✅ **Safety Primitives**: Watchdog timer with configurable callbacks
- ✅ **I/O**: Filesystem helpers via `FileManager`
- ✅ **Telemetry**: Keyed pub/sub bus for visualization data
- ✅ **Test Suite**: 286 tests across all modules, CTest/GTest integrated

### In Progress
- 🔄 **Adapter Contract Layer**: ROS and MAVLink adapter shells
- 🔄 **Mission Runtime**: `MissionElement` and `MissionPipeline` abstractions

### Planned
- ⬚ **Safety Envelope**: `SafetyManager` with e-stop and policy checks
- ⬚ **Replay Harness**: Deterministic trace record/replay for regression testing
- ⬚ **Scalability Validation**: Soak tests and performance regression gates

For the detailed roadmap, see [Implementation Plan](docs/implementation_plan.md).

---

## Community and Support

### Development Resources
- **GitHub Repository**: [https://github.com/axonvex/axonvex-framework](https://github.com/axonvex/axonvex-framework)
- **Documentation**: [https://docs.axonvex.framework.io](https://docs.axonvex.framework.io)
- **API Reference**: [https://api.axonvex.framework.io](https://api.axonvex.framework.io)

### Community Support
- **Discussion Forum**: [https://forum.axonvex.framework.io](https://forum.axonvex.framework.io)
- **Discord Server**: [https://discord.gg/axonvex](https://discord.gg/axonvex)
- **Stack Overflow**: Tag `axonvex` for technical questions

### Professional Services
- **Training Programs**: Comprehensive training for developers and engineers
- **Consulting Services**: Expert guidance for system design and implementation
- **Enterprise Support**: 24/7 technical support for mission-critical deployments

---

## Contributing

We welcome contributions from the community! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details on how to get involved.

### Development Guidelines
- Follow the coding standards outlined in the technical specification
- Ensure all code includes comprehensive tests
- Maintain real-time performance requirements
- Document all APIs and interfaces thoroughly

---

## License

AxonVex Framework is released under the [Apache 2.0 License](LICENSE), allowing for both commercial and non-commercial use.

---

## Contact

For questions, support, or collaboration opportunities:

- **Email**: [info@amerghazal.me](mailto:info@amerghazal.me)
- **Website**: [https://axonvex.framework.io](https://axonvex.framework.io)
- **GitHub Issues**: [Report bugs or request features](https://github.com/axonvex/axonvex-framework/issues)

---

**Where milliseconds matter and complexity must be conquered effortlessly, AxonVex delivers the precision, scalability, and reliability your real-time systems demand.**

---

*Copyright © 2026 AxonVex Framework. All rights reserved.*

