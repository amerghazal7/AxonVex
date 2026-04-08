# AxonVex Framework - Implementation Plan (Parity-First)

**Document ID:** AXVX-IMPL-001  
**Version:** 2.0  
**Date:** 2026-04-07  
**Status:** Draft

---

## Baseline

- Core runtime is currently the strongest implemented area.
- Mission runtime, adapter contracts, and safety envelope are the primary roadmap gaps.

---

## Phase A - Coherence and Test Integrity  ✅ Complete

- Aligned README/API usage with real APIs.
- Registered all tests in CTest (286 passing).
- Added interface contract tests for TCP/UDP/WebSocket.
- Fixed CI workflow paths and documentation references.
- Added `AXONVEX_PLATFORM_LINUX` compile definition.
- Registered orphan examples in CMake.

## Phase B - Adapter Contract Layer  ✅ Complete

- Defined `AdapterInterface` abstract contract with `AdapterBase` common implementation.
- `MockAdapter` for protocol-agnostic contract testing.
- Generic interface units in core: `SubscriberUnit<T>`, `PublisherUnit<T>`, `ServerUnit<T>`, `ClientUnit<T>` with sync+async ports and callback injection.
- System adapter injection: `addAdapter()` / `getAdapter()` on `AxonVexSystem`.
- `axonvex_ros2` plugin lib: `ROS2Adapter` extending `AdapterBase` with typed unit creation and type caster registry.
- Sample app (`axonvex_bridge_app`) demonstrating clean composition.
- All 316 tests passing.

## Phase C - Mission Runtime  ✅ Complete

- Implemented `MissionElement` abstract base with execute/onEnter/onExit/reset lifecycle.
- Implemented `MissionPipeline` as a `ProcessingUnit` with directed-graph execution.
- Sequential and conditional transitions via labeled edges (Default, Option1-3).
- Execution control: startPipeline, abort, restart, pause, resume.
- Control port (AsyncInputPort) and status port (OutputPort) for system integration.
- 15 deterministic tests covering all transition paths, lifecycle hooks, and control commands.
- All 331 tests passing.

## Phase D - Safety Envelope  ✅ Complete

- Implemented `SafetyPolicy` abstract base with `SafetyLevel` (NOMINAL → EMERGENCY) and `PolicyResult`.
- Implemented `SafetyManager` with:
  - Owned policy registry (add/remove/get by name).
  - Periodic evaluation loop on a dedicated thread.
  - Emergency stop trigger/reset with idempotent semantics.
  - Event notification via `Caller<SafetyEvent>` to registered handlers.
  - Automatic e-stop when any policy returns EMERGENCY.
  - Evaluation pauses while e-stopped.
  - Statistics tracking (cycles, violations, e-stop count, worst level seen).
- Integrated `SafetyManager` injection into `AxonVexSystem` (`setSafetyManager` / `getSafetyManager`).
- 33 new tests covering policy lifecycle, on-demand and periodic evaluation, e-stop mechanics, event callbacks, fault injection (dynamic level change), and statistics.
- All 364 tests passing.

## Phase E - Replay and Scalability Gates  ✅ Complete

- Implemented trace record/replay in core: `TraceRecorder`, `TraceReplayer`, JSON save/load, and `sequencesMatch` for regression checks (`axonvex_core/replay/traceReplay.hpp`, `traceReplay.cpp`).
- Tests: `traceReplayTest.cpp` (round-trip file, replay callback order, recorder clear).
- Soak/scalability gates: `soakScalabilityTest.cpp` — high-volume single-threaded enqueue/dequeue and multi-producer/consumer drain with generous wall-clock bounds (CI-friendly; not micro-benchmarks).
- Raised language baseline to **C++14** project-wide (compatibility shims: `axonvex::optional`, experimental filesystem + `stdc++fs` on GNU/non-Apple Clang, ODR-safe `static constexpr` definitions where required).
- Full `test_core` suite green (including parallel CTest); file-based tests use per-process temp paths to avoid races under `ctest -j`.

## Phase F - Web Visualization Platform (Angular 17+)

### F1 — WebSocket Telemetry Gateway (C++)

- Replace placeholder WebSocket implementation with production-ready server.
- Bridge `TelemetryBus` channels to WebSocket with per-client subscription management.
- Add binary serialization (MessagePack/CBOR) alongside JSON, client-negotiable.
- Expose system lifecycle, mission pipeline state, logs, adapter health, safety status, and runtime config over WebSocket.

### F2 — Angular Dashboard Foundation

- Scaffold Angular 17+ project using standalone components, signals, and new control flow.
- Implement WebSocket service with auto-reconnect and binary/JSON negotiation.
- Implement configurable panel layout (drag, resize) with session persistence.
- Add light/dark theming and responsive design (desktop + tablet).

### F3 — Core Dashboard Panels

- Real-time telemetry charts (line, bar, gauge) with channel selector — target <100 ms display latency.
- System topology view: live graph of processing units, ports, and data flow.
- Log viewer with severity filtering, search, and auto-scroll.
- Mission pipeline view: current element, transitions, execution history.
- Lifecycle controls: start, pause, resume, stop.
- Adapter status: connections, health, message rates.
- Safety overlay: policies, violations, e-stop state.
- Configuration inspector: browse and hot-edit runtime config.

### F4 — 3D Visualization

- Integrate Three.js renderer within Angular component.
- Render entity poses, orientations, and trajectories from telemetry channels.
- Point cloud rendering with configurable color maps and decimation.
- Camera controls: orbit, pan, zoom, follow-entity mode.

### F5 — Telemetry Recording and Playback

- Implement C++ telemetry recording API (channel snapshots to file).
- Dashboard playback: load recording, timeline scrubbing, variable-speed replay.
