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

- Hostname and IPv6 support for the TCP and UDP transports. Both resolved addresses with `inet_pton(AF_INET, ...)`, which accepts a numeric IPv4 literal and nothing else — `"localhost"` and every other hostname failed, and IPv6 was unreachable. Both now resolve through `getaddrinfo` (new shared `interfaces/detail/addressResolver.hpp`), try the returned candidates in the system's preference order, and — for UDP — pick a destination matching the bound socket's family (C24).

### Changed

- `AxonVexSystem::setSafetyManager`/`getSafetyManager` (never functional — the pointer was stored and never read) replaced by `setSafetyHook`/`getSafetyHook` on the new core-owned `core::SafetyHook` interface; `safety::SafetyManager` implements it. Core no longer names any safety-layer type.
- `updateSystemConfiguration` is now rejected once initialization begins (returns `false`) — worker threads read the configuration unlocked, and a live update was a data race that never re-applied to running components anyway (C25).

### Removed

- **Breaking:** `BasePort::setThreadSafe` and `ProcessingUnit::setPortsThreadSafe`. A port's thread-safety mode selects between a locked and an unlocked access discipline, so changing it while the port was in use let two threads take different disciplines to the same data — an atomic flag makes the read safe, not the switch (C31). Pass the mode when the port is created instead: the port constructors and `createInputPort`/`createOutputPort`/`createAsyncInputPort`/`createAsyncOutputPort` take a trailing `bool threadSafe = false`, and the mode is fixed for the port's lifetime.
- **Breaking:** `BasePort::setMemoryPoolSize` / `getMemoryPoolSize` and the per-port memory pool behind them. The pool only ever backed the lock-free read path removed in C29; the methods are deleted rather than kept as no-ops so callers fail to compile instead of silently configuring nothing. `MemoryPool` itself is unaffected and still used for system event allocation.
- `examples/memory_pool_ports_simple_example.cpp`, which demonstrated the removed path.

### Fixed

- C2: a `SafetyManager` emergency stop now actually halts the system — `setSafetyHook` registers an emergency callback that performs an emergency shutdown. Teardown is safe from any thread: hook dispatch synchronizes with clearing (so destroying the system with a live hook cannot dangle), `emergencyShutdown`/`stop` serialize thread-handle teardown via a shutdown mutex, and self-join guards let the e-stop fire from a system thread.
- C25: `SystemEvent` string leaks eliminated (LSan-clean) — events are dropped once shutdown begins, the queue is drained back to the pool after the event thread joins, and `initialize()` drains before its component rebuild replaces the event pool (the pool destructor does not destruct live blocks).
- C3: `MemoryPool`'s lock-free free list is no longer ABA-vulnerable — the head is a tagged `{tag:32, index:32}` 64-bit atomic and every pop/push increments the tag, so a stale CAS can never install a stale `next` (the double-handout mechanism).
- C12: `SafetyManager` no longer holds `policiesMutex_` across user code — `evaluateAll()` snapshots the policy set (as `shared_ptr`, so a policy removed mid-cycle stays alive), then runs `evaluate()`, event dispatch, and `triggerEmergencyStop` unlocked; a policy or handler can now call back into the manager without deadlocking, including a re-entrant `evaluateAll()` (which returns the in-progress cycle's worst level instead of blocking or recursing). Handlers moved off the unsynchronized `core::Caller` to a mutex-guarded list with snapshot-then-dispatch, and `evaluationPeriod_` is atomic (the evaluation loop read it unlocked).
- C27: `MemoryPool`, `ThreadSafeQueue` and `RingBuffer` are cache-line aligned but were heap-allocated with plain `new`/`make_unique`, which in C++14 only guarantees 16-byte alignment — every instance was misaligned (undefined behaviour, and the false-sharing padding was not actually separating anything). They now allocate through `AXONVEX_ALIGNED_NEW`. UBSan went from 118 failing tests to zero.
- C28: `InputPort`, `OutputPort` and `AsyncInputPort` leaked the object still held in their memory pool at destruction — the pool frees its blocks without running `~T`, so any non-trivial payload leaked (LSan-confirmed).
- C11 (partial): a short `::send` on the TCP transport counted a truncated payload as a fully sent message. Sends now resume from the offset until the whole payload is written, and report how many bytes actually made it. The newline framing still corrupts binary payloads containing 0x0A — that needs a wire-format change and is tracked separately.
- C32: registering a port callback after the port had started carrying traffic raced the dispatch path, which reads those callbacks without a lock; replacing a `std::function` mid-invocation is a use-after-free. Registration is now explicitly setup-only and throws `std::logic_error` if attempted late, instead of failing silently at runtime. Locking the dispatch path was rejected deliberately: it would run user callbacks under a port lock, and copying the callback out to avoid that would allocate on the port hot path.
- C33: two threads calling `stop()` on a transport at the same time could both reach the thread join, which is undefined behaviour. Start and stop are now serialized and idempotent.
- C33 (second shape): calling `stop()` from a message or error callback crashed the process. Those callbacks run on the transport's own receive thread, so the call made that thread join itself, which throws — and the exception escaped the thread, taking the process down with it. Shutdown now recognises the reentrant call and lets the receive loop unwind on its own, deferring the join.
- Destroying a running `TcpClient` or `UdpSocket` crashed the process: neither had a destructor, so the still-running receive thread was destroyed rather than stopped. Both now shut down cleanly on destruction.
- C24 (silent platform failure): on non-Linux builds both transports reported successful startup, spun a thread that did nothing, and counted "sent" bytes that never left the process. They now report the failure and return `false`.
- C34: a message or error handler that called back into the transport's own registration API deadlocked the transport permanently, because callbacks were invoked while the registration lock was held. Handlers now run with no lock held. Unregistering from another thread waits for any in-flight dispatch to finish, so a handler can never be torn down mid-call; unregistering from inside a handler returns immediately, and that handler may still be invoked for the rest of the dispatch already under way. This holds when several threads are dispatching at once, which a transport does routinely — sends report errors on the calling thread while the receive thread delivers messages.
- C35: a callback on a connected input port that touched the sending port's connection API deadlocked, because sends dispatched to connected ports with the connection lock held. Sends now dispatch from an immutable snapshot of the connection list with no lock held, and take no allocation to do it.
- C30: `AsyncOutputPort::write` checked whether the port had any connections without holding the lock that guards the connection list, so a concurrent `connect`/`disconnect` could reallocate the list mid-read. The check now runs under the lock — but in its own critical section, so the output callback still runs with no port lock held.
- C24 (transport shutdown race): `TcpClient::stop()` and `UdpSocket::stop()` closed the socket while the receive thread was still blocked reading from it, and `send()` used the descriptor with no synchronization at all. Because a closed descriptor number is immediately reusable, an in-flight read or send could land on an unrelated file. Shutdown now stops the receive thread first and closes the socket only afterwards, sends hold the socket for the duration of the call, and the per-transport counters are atomic. Receive calls time out every 100 ms so shutdown never has to interrupt a blocked read — that timeout is the upper bound on how long `stop()` takes.
- C29: reading a thread-safe port could return a value out of memory another thread had already recycled. The ports' lock-free "pooled" path published a pool block through an atomic pointer and freed the previous block immediately, with no scheme to know whether a reader was still inside it — a use-after-free, ASan-confirmed. The pooled path is removed; thread-safe ports now use only the mutex path that was already present alongside it.
- C4: `MemoryPool::isEmpty()`/`isFull()` bodies were swapped.
- C10: SIGPIPE protection (`MSG_NOSIGNAL`) on TCP/UDP sends — peer teardown no longer kills the process.
- C16: `optional<>` move semantics now match `std::optional` (moved-from source stays engaged).
- C17: UUID v4 version bits applied to the correct 64-bit half — generated strings are RFC-4122-compliant.
- C23: `nextPowerOf2` `value >> 32` undefined behavior on 32-bit `size_t` removed (all 3 copies).
- C1: the scheduler now executes every ready task each cycle (previously one per tick — with tick ≈ period, same-priority tasks starved entirely) and runs user code without holding `tasksMutex_`; `removeTask`/`removeAllTasks` defer pool deallocation while a task is mid-execution; `addTask`'s pool-exhaustion callback moved outside the lock.
- C18: user callbacks are no longer invoked while holding locks — `performHealthCheck` and the event loop snapshot callbacks under `callbacksMutex_` and dispatch outside it; the scheduler's task-error callback and `std::cerr` moved outside `statsMutex_`. Fixed the unsynchronized `lastHealth_` read in the monitoring loop (member deleted; loop keeps a local timer) — this was the root cause of the intermittent `HealthCheckCallbacks` segfault (dangling stack write over a return address). Note: the first periodic health check now fires one `healthCheckInterval` after monitoring starts instead of immediately.
