# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `docs/v1_release_plan.md` — audited v1.0 release plan: defect inventory (C1–C24), workstream table, phased milestones, quality gates.
- Project rules: `CLAUDE.md` and `.cursor/rules` (graphify-first codebase navigation, project conventions).
- Regression tests for C4, C16, and C17.
- Regression test for C18 (re-entrant health-check callback registration must not deadlock).

### Fixed

- C4: `MemoryPool::isEmpty()`/`isFull()` bodies were swapped.
- C10: SIGPIPE protection (`MSG_NOSIGNAL`) on TCP/UDP sends — peer teardown no longer kills the process.
- C16: `optional<>` move semantics now match `std::optional` (moved-from source stays engaged).
- C17: UUID v4 version bits applied to the correct 64-bit half — generated strings are RFC-4122-compliant.
- C23: `nextPowerOf2` `value >> 32` undefined behavior on 32-bit `size_t` removed (all 3 copies).
- C18: user callbacks are no longer invoked while holding locks — `performHealthCheck` and the event loop snapshot callbacks under `callbacksMutex_` and dispatch outside it; the scheduler's task-error callback and `std::cerr` moved outside `statsMutex_`. Fixed the unsynchronized `lastHealth_` read in the monitoring loop (member deleted; loop keeps a local timer) — this was the root cause of the intermittent `HealthCheckCallbacks` segfault (dangling stack write over a return address). Note: the first periodic health check now fires one `healthCheckInterval` after monitoring starts instead of immediately.
