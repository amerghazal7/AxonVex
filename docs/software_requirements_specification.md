# AxonVex Framework - Software Requirements Specification

**Document ID:** AXVX-SRS-001  
**Version:** 2.0  
**Date:** 2026-04-07  
**Status:** Draft

---

## 1. Purpose and Scope

This SRS defines requirements for AxonVex as a modular real-time C++ framework with an SDK-oriented API surface.

Requirements are grouped into:
- **Current**: implemented or near-implemented behavior.
- **Next-milestone**: mandatory for the planned mission, adapter, and safety track.
- **Deferred**: valuable, but not required for the first milestone after core stabilization.

---

## 2. Requirement Groups

### 2.1 Current Requirements

| ID | Requirement | Priority |
|---|---|---|
| REQ-CUR-001 | Framework SHALL provide `ProcessingUnit` with sync/async processing hooks. | Must |
| REQ-CUR-002 | Framework SHALL provide typed input/output ports and system-level wiring. | Must |
| REQ-CUR-003 | Framework SHALL provide system lifecycle management (`initialize/start/pause/resume/stop`). | Must |
| REQ-CUR-004 | Framework SHALL provide runtime configuration load/save and typed access. | Must |
| REQ-CUR-005 | Framework SHALL provide logging and error-handling primitives usable across modules. | Must |
| REQ-CUR-006 | Framework SHALL provide protocol abstraction through `ProtocolInterface`. | Must |
| REQ-CUR-007 | Framework SHALL provide a unit-test harness runnable with CTest/GTest. | Must |
| REQ-CUR-008 | Framework C++ sources SHALL compile at the **C++14** standard unless the project explicitly raises `CMAKE_CXX_STANDARD`. | Must |

### 2.2 Next-milestone requirements

| ID | Requirement | Priority |
|---|---|---|
| REQ-PAR-001 | Framework SHALL implement mission runtime primitives: `MissionElement`, `MissionPipeline`, execution control. | Must |
| REQ-PAR-002 | Mission pipeline SHALL support sequential and conditional transitions with explicit state reporting. | Must |
| REQ-PAR-003 | Framework SHALL provide adapter contracts for ROS and MAVLink integration. | Must |
| REQ-PAR-004 | Framework SHALL provide adapter contracts for PX4/SITL-compatible flight flows. | Must |
| REQ-PAR-005 | Framework SHALL provide baseline `SafetyManager` with emergency stop and policy checks. | Must |
| REQ-PAR-006 | Framework SHALL provide deterministic replay of recorded traces for regression testing. | Must |
| REQ-PAR-007 | Adapter implementations SHALL be testable in isolation without requiring full mission deployment. | Must |
| REQ-PAR-008 | All next-milestone claims SHALL be backed by automated tests in CI. | Must |

### 2.3 Visualization Requirements

| ID | Requirement | Priority |
|---|---|---|
| REQ-VIS-001 | Framework SHALL expose a WebSocket-based telemetry gateway that streams `TelemetryBus` channels to web clients in real time. | Must |
| REQ-VIS-002 | Framework SHALL provide an Angular 17+ single-page application as the primary visualization dashboard. | Must |
| REQ-VIS-003 | Dashboard SHALL support real-time line/bar/gauge charts for telemetry channels with <100 ms display latency. | Must |
| REQ-VIS-004 | Dashboard SHALL provide a live system topology view showing processing units, ports, and data flow. | Must |
| REQ-VIS-005 | Dashboard SHALL provide a 3D scene view (Three.js / Angular integration) for spatial data (poses, point clouds, trajectories). | Should |
| REQ-VIS-006 | Dashboard SHALL support channel subscription management: subscribe, unsubscribe, filter, and search telemetry channels. | Must |
| REQ-VIS-007 | Dashboard SHALL display system lifecycle state and allow lifecycle commands (start, pause, resume, stop) via authenticated controls. | Should |
| REQ-VIS-008 | Dashboard SHALL display mission pipeline state: current element, transitions, and execution history. | Should |
| REQ-VIS-009 | Dashboard SHALL provide a log viewer with severity filtering, text search, and auto-scroll for framework logs streamed over WebSocket. | Must |
| REQ-VIS-010 | Dashboard SHALL provide configurable layout with resizable/draggable panels that persist across sessions (local storage or server-side). | Should |
| REQ-VIS-011 | Dashboard SHALL support theming (light/dark mode) and responsive design for desktop and tablet viewports. | Should |
| REQ-VIS-012 | Framework SHALL provide a telemetry recording/playback API so the dashboard can replay historical data offline. | Could |
| REQ-VIS-013 | Dashboard SHOULD support overlay of safety envelope status: active policies, violations, and e-stop state. | Should |
| REQ-VIS-014 | WebSocket gateway SHALL support binary (MessagePack/CBOR) and JSON serialization with client-negotiable format. | Should |
| REQ-VIS-015 | Dashboard SHALL include an adapter status panel showing connected adapters, health, and message rates. | Should |
| REQ-VIS-016 | Dashboard SHALL provide a configuration inspector to view and hot-edit runtime configuration values. | Could |
| REQ-VIS-017 | Dashboard SHALL support multi-client connections with independent subscriptions and view state. | Must |
| REQ-VIS-018 | Visualization stack SHALL be buildable and servable independently of the C++ framework build. | Must |

### 2.4 Deferred Requirements

| ID | Requirement | Priority |
|---|---|---|
| REQ-DEF-001 | Framework SHOULD provide VR/AR visualization modes for immersive 3D monitoring. | Could |
| REQ-DEF-002 | Framework SHOULD provide GraphQL and MQTT adapters. | Could |
| REQ-DEF-003 | Framework SHOULD provide enterprise security features (MFA/RBAC) for hosted control surfaces. | Could |
| REQ-DEF-004 | Framework SHOULD provide multi-node orchestration utilities beyond baseline adapters. | Could |

---

## 3. Testability and Evidence Policy

- Every requirement marked complete MUST have linked automated tests.
- Public performance/reliability claims MUST cite reproducible benchmarks.
- Planned features MUST NOT be described as implemented in release-facing docs.
