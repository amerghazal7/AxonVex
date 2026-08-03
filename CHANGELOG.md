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
- Regression tests for C1 (all ready tasks execute each scheduler cycle; removing a task mid-execution is safe).
- Regression tests for C2 (`SafetyManager` e-stop halts the system; hook registration/clearing).
- Concurrent stress regression test for C3 (8-thread allocate/deallocate ownership stamping; TSan-clean).

### Changed

- `AxonVexSystem::setSafetyManager`/`getSafetyManager` (never functional — the pointer was stored and never read) replaced by `setSafetyHook`/`getSafetyHook` on the new core-owned `core::SafetyHook` interface; `safety::SafetyManager` implements it. Core no longer names any safety-layer type.

### Fixed

- C2: a `SafetyManager` emergency stop now actually halts the system — `setSafetyHook` registers an emergency callback that performs an emergency shutdown. Teardown is safe from any thread: hook dispatch synchronizes with clearing (so destroying the system with a live hook cannot dangle), `emergencyShutdown`/`stop` serialize thread-handle teardown via a shutdown mutex, and self-join guards let the e-stop fire from a system thread.
- C3: `MemoryPool`'s lock-free free list is no longer ABA-vulnerable — the head is a tagged `{tag:32, index:32}` 64-bit atomic and every pop/push increments the tag, so a stale CAS can never install a stale `next` (the double-handout mechanism).
- C4: `MemoryPool::isEmpty()`/`isFull()` bodies were swapped.
- C10: SIGPIPE protection (`MSG_NOSIGNAL`) on TCP/UDP sends — peer teardown no longer kills the process.
- C16: `optional<>` move semantics now match `std::optional` (moved-from source stays engaged).
- C17: UUID v4 version bits applied to the correct 64-bit half — generated strings are RFC-4122-compliant.
- C23: `nextPowerOf2` `value >> 32` undefined behavior on 32-bit `size_t` removed (all 3 copies).
- C1: the scheduler now executes every ready task each cycle (previously one per tick — with tick ≈ period, same-priority tasks starved entirely) and runs user code without holding `tasksMutex_`; `removeTask`/`removeAllTasks` defer pool deallocation while a task is mid-execution; `addTask`'s pool-exhaustion callback moved outside the lock.
- C18: user callbacks are no longer invoked while holding locks — `performHealthCheck` and the event loop snapshot callbacks under `callbacksMutex_` and dispatch outside it; the scheduler's task-error callback and `std::cerr` moved outside `statsMutex_`. Fixed the unsynchronized `lastHealth_` read in the monitoring loop (member deleted; loop keeps a local timer) — this was the root cause of the intermittent `HealthCheckCallbacks` segfault (dangling stack write over a return address). Note: the first periodic health check now fires one `healthCheckInterval` after monitoring starts instead of immediately.
