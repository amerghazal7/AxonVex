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

### 2.3 Deferred Requirements

| ID | Requirement | Priority |
|---|---|---|
| REQ-DEF-001 | Framework SHOULD provide advanced dashboard UX and 3D visualization. | Could |
| REQ-DEF-002 | Framework SHOULD provide GraphQL and MQTT adapters. | Could |
| REQ-DEF-003 | Framework SHOULD provide enterprise security features (MFA/RBAC) for hosted control surfaces. | Could |
| REQ-DEF-004 | Framework SHOULD provide multi-node orchestration utilities beyond baseline adapters. | Could |

---

## 3. Testability and Evidence Policy

- Every requirement marked complete MUST have linked automated tests.
- Public performance/reliability claims MUST cite reproducible benchmarks.
- Planned features MUST NOT be described as implemented in release-facing docs.
