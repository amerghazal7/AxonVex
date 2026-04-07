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

## Phase D - Safety Envelope

- Implement baseline `SafetyManager`, e-stop, and policy checks.
- Add fault-injection and safety-event test coverage.

## Phase E - Replay and Scalability Gates

- Implement deterministic trace record/replay harness.
- Add soak/performance regression tests and CI thresholds.
