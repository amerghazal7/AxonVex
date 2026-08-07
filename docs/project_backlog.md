# AxonVex Framework - Project Backlog (Parity-First)

**Document ID:** AXVX-BL-001  
**Version:** 2.0  
**Date:** 2026-04-07  
**Status:** Draft (Benchmark-Rebased)

---

## Priority Model

- **P0 (Must):** parity-critical replacement capability
- **P1 (Should):** stability/scalability hardening
- **P2 (Could):** deferred expansion

---

## Epics

| Epic | Title | Priority | Status |
|---|---|---|---|
| E01 | Core Runtime Integrity | P0 | Done |
| E02 | Adapter Contract Layer | P0 | Done (Baseline) |
| E03 | Mission Runtime Engine | P0 | Done |
| E04 | Safety Envelope | P0 | Done |
| E05 | Replay and Regression Harness | P0 | Done (trace record/replay + tests) |
| E06 | Scalability and Soak Validation | P1 | Done (soak tests; CI uses existing `ctest`) |
| E07 | Web Visualization Platform (Angular 17+) | P1 | Not Started |
| E08 | Advanced Security and Access Control | P2 | Deferred |

---

## Immediate P0 Items

| ID | Item | Effort | Status |
|---|---|---|---|
| B01.1 | Register all built tests in CTest (including configuration/path) | 0.5d | Done |
| B01.2 | Add interface contract tests for TCP/UDP/WebSocket | 1.5d | Done |
| B01.3 | Align README/API examples with orchestrator API | 0.5d | Done |
| B01.4 | Guard docs build option against missing docs subproject | 0.25d | Done |
| B02.1 | Define adapter contract spec | 1d | Done |
| B02.2 | Implement ROS adapter baseline shell | 2d | Done |
| B02.3 | Implement MAVLink adapter baseline shell | 2d | Done |
| B03.1 | Implement `MissionElement` abstraction | 1d | Done |
| B03.2 | Implement `MissionPipeline` with sequential/conditional transitions | 2d | Done |
| B04.1 | Implement baseline `SafetyManager` + e-stop | 2d | Done |
| B05.1 | Implement deterministic replay harness (`TraceRecorder` / `TraceReplayer`, JSON, tests) | 2d | Done |
| B05.2 | Soak/scalability regression tests (`ThreadSafeQueue` volume + MPMC drain, time-bounded) | 1d | Done |

---

## E07 — Web Visualization Platform (Angular 17+)

### Phase F1: WebSocket Telemetry Gateway (C++ side)

| ID | Item | Effort | Status |
|---|---|---|---|
| B07.1 | Implement WebSocket server on top of existing `ProtocolInterface` (replace placeholder) | 3d | Not Started |
| B07.2 | Bridge `TelemetryBus` channels to WebSocket: auto-fan-out, per-client subscription management | 2d | Not Started |
| B07.3 | Add binary serialization support (MessagePack/CBOR) alongside JSON for WebSocket payloads | 2d | Not Started |
| B07.4 | Expose system lifecycle state and command channel over WebSocket | 1d | Not Started |
| B07.5 | Expose mission pipeline state and log stream over WebSocket | 1d | Not Started |
| B07.6 | Expose adapter health, safety envelope status, and runtime config over WebSocket | 1.5d | Not Started |

### Phase F2: Angular Dashboard — Foundation

| ID | Item | Effort | Status |
|---|---|---|---|
| B07.7 | Scaffold Angular 17+ project (standalone components, signals, SSR-ready) with workspace layout | 1d | Not Started |
| B07.8 | Implement WebSocket service with auto-reconnect, channel subscription API, and binary/JSON negotiation | 2d | Not Started |
| B07.9 | Implement configurable dashboard shell: draggable/resizable panel layout with persistence | 2d | Not Started |
| B07.10 | Implement theming (light/dark mode) and responsive layout for desktop and tablet | 1d | Not Started |

### Phase F3: Angular Dashboard — Core Panels

| ID | Item | Effort | Status |
|---|---|---|---|
| B07.11 | Real-time telemetry chart panel (line, bar, gauge) with channel selector — target <100 ms display latency | 3d | Not Started |
| B07.12 | System topology panel: live graph of processing units, ports, and data flow | 3d | Not Started |
| B07.13 | Log viewer panel: severity filter, text search, auto-scroll, pause/resume | 2d | Not Started |
| B07.14 | Mission pipeline panel: current element, transitions, execution history timeline | 2d | Not Started |
| B07.15 | Lifecycle control panel: system state display with start/pause/resume/stop commands | 1d | Not Started |
| B07.16 | Adapter status panel: connected adapters, health indicators, message rate counters | 1.5d | Not Started |
| B07.17 | Safety overlay panel: active policies, violation alerts, e-stop state | 1.5d | Not Started |
| B07.18 | Configuration inspector panel: browse and hot-edit runtime config values | 2d | Not Started |

### Phase F4: 3D Visualization

| ID | Item | Effort | Status |
|---|---|---|---|
| B07.19 | Integrate Three.js scene renderer within Angular component | 2d | Not Started |
| B07.20 | 3D pose rendering: entity positions, orientations, and trajectories from telemetry channels | 3d | Not Started |
| B07.21 | Point cloud rendering with configurable color mapping and decimation | 2d | Not Started |
| B07.22 | Camera controls: orbit, pan, zoom, follow-entity mode | 1d | Not Started |

### Phase F5: Telemetry Recording and Playback

| ID | Item | Effort | Status |
|---|---|---|---|
| B07.23 | Implement C++ telemetry recording API (channel snapshots to file) | 2d | Not Started |
| B07.24 | Implement dashboard playback controls: load recording, scrub timeline, variable speed | 2d | Not Started |
