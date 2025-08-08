# AxonVex Framework - Architecture Diagram (Phase 3 Modular Design)

## Phase 3 Modular Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            AxonVex Framework                            │
├─────────────────────────────────────────────────────────────────────────┤
│                         Visualization Layer                             │
├───────────────────────┬───────────────────────┬─────────────────────────┤
│ Real-time Dashboard   │ 3D/VR Visualization   │ Analytics Suite         │
├───────────────────────┴───────────────────────┴─────────────────────────┤
│                           Orchestration Layer                           │
├───────────────────────┬───────────────────────┬─────────────────────────┤
│ System Orchestrator   │ Safety Orchestrator   │ Performance Orchestrator│
├─────────────────────────────────────────────────────────────────────────┤
│                               Module Layer                              │
├────────────┬────────────┬─────────┬──────────────┬───────────┬──────────┤
│  core      │  utils     │  types  │     io       │ interfaces│ plugins  │
├────────────┼────────────┼─────────┼──────────────┼───────────┼──────────┤
│ algorithms │ visualization │ safety │ deployment │           │          │
├────────────┴────────────┴─────────┴──────────────┴───────────┴──────────┤
│                         Core Runtime Engine (RT)                         │
├────────────────┬────────────────────┬──────────────────┬────────────────┤
│ Processing     │ Port System (sync/ │ Timing Controller│ Resource &     │
│ Units (Blocks) │ async + validation)│ (µs precision)   │ Config Manager │
├────────────────┴────────────────────┴──────────────────┴────────────────┤
│                      Security Manager & Audit Log                        │
└─────────────────────────────────────────────────────────────────────────┘
```

Notes:
- Module Layer reflects Phase 3 modularization (utils, types, io, interfaces, plugins, algorithms, visualization, safety, deployment).
- Core remains the foundation for timing, ports, scheduling, resources, and security.

## System Port Management (Phase 3 Breakthrough)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        System Port Management                            │
├─────────────────────────────────────────────────────────────────────────┤
│  AxonVexSystem A                             AxonVexSystem B             │
│  ┌──────────────────────┐                   ┌──────────────────────┐     │
│  │  Processing Units    │                   │  Processing Units    │     │
│  │  (blocks & ports)    │                   │  (blocks & ports)    │     │
│  └──────────┬───────────┘                   └──────────┬───────────┘     │
│             │ expose as System I/O                       │ expose as     │
│             ▼                                            ▼ System I/O    │
│     ┌───────────────┐                           ┌───────────────┐        │
│     │ System Inputs │◀──── type-safe connect ───│ System Outputs│        │
│     └───────────────┘                           └───────────────┘        │
│             ▲                                            ▲               │
│             │ auto-cleanup, validation, threading        │               │
├─────────────┴────────────────────────────────────────────┴───────────────┤
│ Features:                                                                │
│ • Cross-system data flow (type-checked)                                  │
│ • Hierarchical composition of subsystems                                 │
│ • Thread-safe dynamic (re)wiring & lifecycle management                  │
│ • System-level introspection & metrics                                    │
└─────────────────────────────────────────────────────────────────────────┘
```

## Core Component Relationships

### Processing Unit Data Flow
```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Input Ports   │───▶│ Processing Unit │───▶│  Output Ports   │
│  (sync/async)   │    │  (processSync/  │    │  (buffer/QoS)   │
│  + validation   │    │   processAsync) │    │  + backpressure │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Module Layer Map (Phase 3)
```
┌─────────────────────────────────────────────────────────────────────────┐
│                                Modules                                   │
├────────────┬────────────┬──────────────┬─────────────┬─────────┬────────┤
│ core       │ utils      │ types        │ io          │ plugins │ algos  │
│ ports, RT, │ math,      │ geometry,    │ files,      │ loader, │ control│
│ timing,    │ containers,│ signals,     │ streams,    │ registry│ est.,  │
│ resources  │ serializ., │ time, units  │ net/dev/px  │ API     │ signal │
├────────────┼────────────┼──────────────┼─────────────┼─────────┼────────┤
│ interfaces │ visualization │ safety     │ deployment  │         │        │
│ ROS, MAV,  │ dashboard, 3D│ watchdogs, │ packaging,  │         │        │
│ WS, REST…  │ charts, AR/VR│ validation  │ ops/metrics │         │        │
└────────────┴────────────┴──────────────┴─────────────┴─────────┴────────┘
```

## Unified Communication Architecture
```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Communication Layer                               │
├─────────────────────────────────────────────────────────────────────────┤
│  Protocol Endpoints                                                      │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐     │
│  │ WebSocket    │ │ HTTP/REST    │ │ GraphQL      │ │ MQTT         │     │
│  └──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘     │
│                    │                        │                            │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐                      │
│  │ ROS Adapter  │ │ MAVLink Adap.│ │ Custom Proto │ …                    │
│  └──────────────┘ └──────────────┘ └──────────────┘                      │
├─────────────────────────────────────────────────────────────────────────┤
│                     Interface Abstraction Layer                          │
├─────────────────────────────────────────────────────────────────────────┤
│  Publisher ◀──▶ Subscriber ◀──▶ Service Client/Server (QoS, Reliability) │
└─────────────────────────────────────────────────────────────────────────┘
```

## Real-time Data Flow with QoS
```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Data Sources                                │
├─────────────────────────────────────────────────────────────────────────┤
│ Sensors │ Controllers │ Estimators │ Actuators │ External Feeds          │
├─────────────────────────────────────────────────────────────────────────┤
│                        Data Stream Manager (RT)                          │
│  • Filter/Transform  • Buffer Mgmt  • Compression  • Zero-copy           │
│  • QoS (Critical/High/Best-effort)  • Backpressure                       │
├───────────────┬─────────────────────────┬───────────────────────────────┤
│ QoS:Critical  │ QoS:High                │ QoS:Best-effort               │
├──────┬────────┼──────────┬──────────────┼──────────┬───────────────────┤
│ WS   │ gRPC   │ WS       │ REST/GraphQL │ WS       │ Storage/Archive   │
├──────┴────────┴──────────┴──────────────┴──────────┴───────────────────┤
│                           Visualization Clients                          │
│               Web UI │ Mobile │ Desktop │ Programmatic API               │
└─────────────────────────────────────────────────────────────────────────┘
```

## Safety and Security Architecture

### Multi-Layer Safety System
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Layer 1: Hardware Safety  | Interlocks | HW Watchdog | E-Stop | Power   │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 2: Software Safety  | SW Watchdog | Param Validator | Timing Mon. │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 3: Application      | Mission Safety | State/Behavior | Perf Mon. │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 4: Human Safety     | Operator UI | Manual Override | Monitoring │
└─────────────────────────────────────────────────────────────────────────┘
```

### Security Framework
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Security Perimeter | Firewall | IDS/IPS | Access Control | Audit Logging │
├─────────────────────────────────────────────────────────────────────────┤
│ Authentication     | MFA | Certificates | Tokens | Sessions              │
├─────────────────────────────────────────────────────────────────────────┤
│ Authorization      | RBAC | Permissions | Operation Validator | ABAC     │
├─────────────────────────────────────────────────────────────────────────┤
│ Encryption         | TLS/SSL | Data-at-Rest | Key Mgmt | Signatures     │
└─────────────────────────────────────────────────────────────────────────┘
```

## Performance Monitoring Architecture
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Data Collection | Timing | Resources | Network | Application              │
├─────────────────────────────────────────────────────────────────────────┤
│ Metrics Processing | Filter | Aggregate | Analyze | Trend/Predict         │
├─────────────────────────────────────────────────────────────────────────┤
│ Storage | Time Series DB | Metrics DB | Alerts DB | Reports               │
├─────────────────────────────────────────────────────────────────────────┤
│ Visualization | Realtime Dash | Historical | Predictive | Alerts          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Deployment Architecture

### Single Node
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Node: AxonVex System + Visualization                                    │
├─────────────────────────────────────────────────────────────────────────┤
│ Modules: core | io | interfaces | algorithms | visualization             │
└─────────────────────────────────────────────────────────────────────────┘
```

### Distributed
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Control Node: Master System | System/Mission/Network Managers           │
└─────────────────────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Network: Ethernet | WiFi | CAN | Serial                                 │
└─────────────────────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Compute Nodes: Sensor | Control | Actuator | Monitoring (Module sets)   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Plugin Architecture (Phase 3)
```
┌─────────────────────────────────────────────────────────────────────────┐
│ Plugin Manager | Discovery | Loader | Registry | Lifecycle | Services    │
├─────────────────────────────────────────────────────────────────────────┤
│ Plugin Types: ProcessingUnit | Interface | Visualization | Mission       │
│            + Ext: Algorithms | Analytics | Custom Tools                 │
└─────────────────────────────────────────────────────────────────────────┘
```

This diagram reflects the Phase 3 modular architecture, highlights the System Port Management capability, clarifies the unified communication framework with protocol adapters and QoS, and aligns safety/security and performance views with the updated design.