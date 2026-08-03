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
- Regression tests for C12 (a handler may mutate the policy registry during dispatch, call `evaluateAll()` or `triggerEmergencyStop()` re-entrantly; a policy may query the manager from `evaluate()`; the evaluation period may change while the loop runs).

- Sanitizer builds: `-DAXONVEX_SANITIZER=address|thread|undefined` on the root `CMakeLists.txt`, wired to a 3-way `sanitizers` matrix job in CI. Quarantined tests (throughput asserts, two known races) are listed with reasons in the workflow.

### Changed

- `AxonVexSystem::setSafetyManager`/`getSafetyManager` (never functional — the pointer was stored and never read) replaced by `setSafetyHook`/`getSafetyHook` on the new core-owned `core::SafetyHook` interface; `safety::SafetyManager` implements it. Core no longer names any safety-layer type.
- `updateSystemConfiguration` is now rejected once initialization begins (returns `false`) — worker threads read the configuration unlocked, and a live update was a data race that never re-applied to running components anyway (C25).

### Removed

- **Breaking:** `BasePort::setMemoryPoolSize` / `getMemoryPoolSize` and the per-port memory pool behind them. The pool only ever backed the lock-free read path removed in C29; the methods are deleted rather than kept as no-ops so callers fail to compile instead of silently configuring nothing. `MemoryPool` itself is unaffected and still used for system event allocation.
- `examples/memory_pool_ports_simple_example.cpp`, which demonstrated the removed path.

### Fixed

- C2: a `SafetyManager` emergency stop now actually halts the system — `setSafetyHook` registers an emergency callback that performs an emergency shutdown. Teardown is safe from any thread: hook dispatch synchronizes with clearing (so destroying the system with a live hook cannot dangle), `emergencyShutdown`/`stop` serialize thread-handle teardown via a shutdown mutex, and self-join guards let the e-stop fire from a system thread.
- C25: `SystemEvent` string leaks eliminated (LSan-clean) — events are dropped once shutdown begins, the queue is drained back to the pool after the event thread joins, and `initialize()` drains before its component rebuild replaces the event pool (the pool destructor does not destruct live blocks).
- C3: `MemoryPool`'s lock-free free list is no longer ABA-vulnerable — the head is a tagged `{tag:32, index:32}` 64-bit atomic and every pop/push increments the tag, so a stale CAS can never install a stale `next` (the double-handout mechanism).
- C12: `SafetyManager` no longer holds `policiesMutex_` across user code — `evaluateAll()` snapshots the policy set (as `shared_ptr`, so a policy removed mid-cycle stays alive), then runs `evaluate()`, event dispatch, and `triggerEmergencyStop` unlocked; a policy or handler can now call back into the manager without deadlocking, including a re-entrant `evaluateAll()` (which returns the in-progress cycle's worst level instead of blocking or recursing). Handlers moved off the unsynchronized `core::Caller` to a mutex-guarded list with snapshot-then-dispatch, and `evaluationPeriod_` is atomic (the evaluation loop read it unlocked).
- C27: `MemoryPool`, `ThreadSafeQueue` and `RingBuffer` are cache-line aligned but were heap-allocated with plain `new`/`make_unique`, which in C++14 only guarantees 16-byte alignment — every instance was misaligned (undefined behaviour, and the false-sharing padding was not actually separating anything). They now allocate through `AXONVEX_ALIGNED_NEW`. UBSan went from 118 failing tests to zero.
- C28: `InputPort`, `OutputPort` and `AsyncInputPort` leaked the object still held in their memory pool at destruction — the pool frees its blocks without running `~T`, so any non-trivial payload leaked (LSan-confirmed).
- C29: reading a thread-safe port could return a value out of memory another thread had already recycled. The ports' lock-free "pooled" path published a pool block through an atomic pointer and freed the previous block immediately, with no scheme to know whether a reader was still inside it — a use-after-free, ASan-confirmed. The pooled path is removed; thread-safe ports now use only the mutex path that was already present alongside it.
- C4: `MemoryPool::isEmpty()`/`isFull()` bodies were swapped.
- C10: SIGPIPE protection (`MSG_NOSIGNAL`) on TCP/UDP sends — peer teardown no longer kills the process.
- C16: `optional<>` move semantics now match `std::optional` (moved-from source stays engaged).
- C17: UUID v4 version bits applied to the correct 64-bit half — generated strings are RFC-4122-compliant.
- C23: `nextPowerOf2` `value >> 32` undefined behavior on 32-bit `size_t` removed (all 3 copies).
- C1: the scheduler now executes every ready task each cycle (previously one per tick — with tick ≈ period, same-priority tasks starved entirely) and runs user code without holding `tasksMutex_`; `removeTask`/`removeAllTasks` defer pool deallocation while a task is mid-execution; `addTask`'s pool-exhaustion callback moved outside the lock.
- C18: user callbacks are no longer invoked while holding locks — `performHealthCheck` and the event loop snapshot callbacks under `callbacksMutex_` and dispatch outside it; the scheduler's task-error callback and `std::cerr` moved outside `statsMutex_`. Fixed the unsynchronized `lastHealth_` read in the monitoring loop (member deleted; loop keeps a local timer) — this was the root cause of the intermittent `HealthCheckCallbacks` segfault (dangling stack write over a return address). Note: the first periodic health check now fires one `healthCheckInterval` after monitoring starts instead of immediately.
