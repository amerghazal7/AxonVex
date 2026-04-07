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
| E04 | Safety Envelope | P0 | Not Started |
| E05 | Replay and Regression Harness | P0 | Not Started |
| E06 | Scalability and Soak Validation | P1 | Not Started |
| E07 | Visualization Expansion | P2 | Deferred |
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
| B04.1 | Implement baseline `SafetyManager` + e-stop | 2d | Planned |
| B05.1 | Implement deterministic replay harness | 2d | Planned |
