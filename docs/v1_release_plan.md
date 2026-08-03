# AxonVex v1.0 Release Plan — Deep Audit & Golden-MVP Roadmap

_Date: 2026-07-23 · Basis: full-tree review (core runtime, support subsystems, extension layers, docs/build/tests) + verified clean build with 369/369 tests passing. Complements `implementation_plan.md` and `project_backlog.md` with a release-focused sequencing._

---

## 1. Verdict in one paragraph

AxonVex has a genuinely solid skeleton — clean build (zero warnings), 369 passing tests, coherent library layering, wired CMake export, honest SRS/backlog — wrapped in a README that oversells it and a core with roughly a dozen real correctness bugs. The scheduler at the heart of the "real-time" claim executes **one task per cycle while holding a global mutex**, the safety e-stop is **not connected to anything**, and the "lock-free" memory pool has a **textbook ABA bug**. None of this is fatal: the fixes are well-scoped and the deletable bloat is identified. v1.0 scope is the **full project vision** — core runtime, visualization platform, enterprise security, hardware watchdog, MAVLink, cross-platform, distributed scale — which puts a defensible v1.0 at roughly 7–9 months solo (≈4–5 months with a second developer on the dashboard track), sequenced through alpha/beta/RC milestones below.

## 2. What is genuinely good (keep, build on)

- Build/test discipline: 0-warning build, 369 tests, CTest+GTest integration, 3-OS CI matrix, `.clang-format`.
- `ThreadSafeQueue` — correct Vyukov bounded MPMC queue.
- Trace record/replay (`traceReplay.*`) — small, correct, and strategically valuable (sells determinism).
- `AdapterBase` contract + contract tests; `interfaceUnits.hpp`; `MissionElement` abstraction; `errorHandler`; geometry/UUID types (one bug); `filesystem_compat.hpp`.
- CMake install/export scaffolding (`install(EXPORT axonvexTargets NAMESPACE axonvex::)`, Config + ConfigVersion files) — 80% of packaging exists.
- The SRS and backlog are honest and stratified; the marketing layer is the only dishonest document.

## 3. Critical defect inventory (fix before anything else)

Correctness bugs, each with location, verified by direct code reading:

| # | Severity | Defect | Location |
|---|---|---|---|
| C1 | CRITICAL | Scheduler executes only ONE task per cycle; runs user code while holding `tasksMutex_` (blocks addTask/removeTask/stats for the whole execution; N−1 ready tasks skipped per tick) | `timingController.cpp:277-317` |
| C2 | CRITICAL | SafetyManager never read after `setSafetyManager` — e-stop/policy violations do not halt the system | `system.cpp:497-506`, `system.hpp:715` |
| C3 | CRITICAL | MemoryPool lock-free free-list is ABA-vulnerable (no tag/version on head CAS); underpins thread-safe port writes | `memoryPool.hpp:118-142,181-182` |
| C4 | HIGH | `MemoryPool::isEmpty()`/`isFull()` bodies are swapped | `memoryPool.hpp:210-214` |
| C5 | HIGH | `Configuration::applyUpdates` unlocks mid-merge while holding live refs into `config_data_` → UB with concurrent writers | `configuration.cpp:343-369` |
| C6 | HIGH | Declared-but-unimplemented public API: `saveConfiguration`, `exportDiagnostics`, `collectGarbage`, `registerRecoveryCallback`; `initialize(const Configuration&)` stub; literal "Additional methods would continue here..." | `system.hpp:474,509,528,563`, `system.cpp:266-281,1407` |
| C7 | HIGH | State machine bypass: `emergencyShutdown`/`reset` store `currentState_` directly, skipping validated `transitionState`; `initialize()` calls `initializeComponents()` twice (first TimingController discarded) | `system.cpp:437,516,189+211` |
| C8 | HIGH | `ProcessingUnit` declares `=default` move ops while owning two `std::mutex` (ill-formed if used); ports/system hold raw `BasePort*`/`owner_` that dangle on relocation | `processingUnit.hpp:69-70,561,578` |
| C9 | HIGH | `connectToSystem`/`disconnectFromSystem` lock two `systemPortsMutex_` in argument order → AB/BA deadlock | `system.hpp:811-812,871-872` |
| C10 | HIGH | TCP/UDP `::send` without `MSG_NOSIGNAL` → SIGPIPE process kill on peer teardown | `tcpClient.hpp:74,82`, `udpSocket.hpp:77` |
| C11 | HIGH | TCP newline framing corrupts binary payloads (0x0A); single `::send` never loops on short writes (silent truncation counted as success) | `tcpClient.hpp:81-88,212-222` |
| C12 | HIGH | SafetyManager invokes user callbacks + `triggerEmergencyStop` under `policiesMutex_` (reentrant deadlock); `evaluationPeriod_` read/write race | `safetyManager.hpp:148-177,199-201,224` |
| C27 | HIGH | Over-aligned types (`alignas(64)` in `MemoryPool`, `ThreadSafeQueue`, `RingBuffer`, and the `Block[]`/`Slot[]` arrays) heap-allocated with plain `new`/`make_unique` — C++14's `operator new` only guarantees 16-byte alignment, so every instance was misaligned UB and the false-sharing padding did nothing. UBSan flagged 118 tests. **Fixed**: `AXONVEX_ALIGNED_NEW` + `utils/alignedNew.hpp`. | `memoryPool.hpp`, `threadSafeQueue.hpp`, `ringBuffer.hpp` |
| C28 | MEDIUM | `InputPort`/`OutputPort`/`AsyncInputPort` destructors were `= default`, leaking the object still held in `pooledData_` (the pool frees blocks without running `~T`). `reset`/`setThreadSafe`/`setMemoryPoolSize` already released it; only the destructor did not. **Fixed**: shared `releasePooledData()`. | `ports.hpp:145,220,287` |
| C29 | HIGH | `MemoryPool::allocateObject` data race under concurrent port writes (TSan, `MemoryPoolPortsTest.ConcurrentAccessPatterns` via `InputPort::writeData`) — distinct from C3's ABA fix. Quarantined from the TSan CI gate until fixed. | `memoryPool.hpp:219`, `ports.hpp:505` |
| C26 | MEDIUM | `SafetyManager::evaluationLoop` has no try/catch around `evaluateAll()` — a user policy whose `evaluate()` throws propagates out of the worker thread and calls `std::terminate` (a safety policy kills the process). Same shape as the unguarded user code in `Watchdog::loop`. | `safetyManager.hpp` (`evaluationLoop`), `watchdog.hpp:60-75` |
| C13 | HIGH | Plugin factory declared `extern "C" inline` (not dlsym-able); ROS2 plugin built as INTERFACE lib (no .so ever produced); no ABI/version handshake; dup-name `emplace` leaks instance + dlopen handle; no `RTLD_LOCAL` | `ros2Plugin.hpp:18-24`, `plugins/axonvex_ros2/CMakeLists.txt:11`, `pluginManager.hpp:33-76` |
| C14 | HIGH | Logger: `formatString` returns format unchanged (all `logf`/`LOGF_*` args silently dropped); `LOGF_CRITICAL` macro duplicated/broken; heap allocation per log call despite "<500ns, no allocation" claim | `logger.hpp:632-637,1085-1086,462-495` |
| C15 | MEDIUM | `Path::createConfigPath/createLogPath` size_t underflow → throw/abort on short names; traversal check is substring-based (false positives + bypassable); DCLP on non-atomic `initialized_`; `isInWhitelist` iterates shared state unlocked | `path.cpp:748,764,793-803,76-103,811` |
| C16 | MEDIUM | `optional` move ops destroy the source (non-std semantics) | `optional.hpp:44-96` |
| C17 | MEDIUM | UUID v4 version/variant bits applied to the wrong 64-bit halves → not RFC-4122-compliant strings | `uuid.hpp:27-31,69-70` |
| C18 | MEDIUM | Callbacks invoked under locks (recurring pattern): `executeTask` error callback under `tasksMutex_`+`statsMutex_` + `std::cerr`; `performHealthCheck` under `callbacksMutex_`; `lastHealth_` read race | `timingController.cpp:524-564`, `system.cpp:579-596,1195` |
| C19 | MEDIUM | `PrecisionTimer` on every execution: O(n) `erase(begin())` at 10k samples under mutex; `high_resolution_clock` instead of `steady_clock`; throws from a timing getter | `precisionTimer.cpp:235,90-92`, `precisionTimer.hpp:60` |
| C20 | MEDIUM | ROS2Adapter: `registerDefaultCasters()` empty despite docs; `createServer` ignores the request; `TypeCasterRegistry` keyed only by InternalType (RosMsg collisions → silent nullptr); raw `new` returns | `ros2Adapter.hpp:161-170,114-131`, `typeCasterRegistry.hpp:57,66` |
| C21 | MEDIUM | `RingBuffer` ctor placement-news over value-initialized elements (leak for non-trivial T); advertised generically "lock-free" but is SPSC-only | `ringBuffer.hpp:161` |
| C22 | MEDIUM | `MissionPipeline`: `status_`/`currentElement_` mutated from scheduler thread and user API with no synchronization; raw non-owned `MissionElement*`; no validation of transition graph | `missionPipeline.hpp:85-224,241` |
| C23 | LOW | `nextPowerOf2` `value >> 32` UB on 32-bit `size_t` (3 copies of the function) | `coreUtilities.hpp:36-45`, `memoryPool.hpp:247-257`, `threadSafeQueue.hpp:234` |
| C24 | LOW | TCP/UDP fd close race (`sock_` non-atomic, closed while recvLoop uses it); IPv4-only, `inet_pton`-only (no DNS, "localhost" fails); non-Linux builds silently no-op and return `true` | `tcpClient.hpp:57-60,197` |
| C25 | HIGH | `updateSystemConfiguration` writes `systemConfig_` with no synchronization while monitoring/event/scheduler threads read it (TSan-confirmed during C18 verification); also `publishEvent` after the event thread stops leaks the event's strings (pool objects constructed, never destroyed — LSan-confirmed) | `system.cpp:1582`, `system.cpp` `publishEvent`/`emergencyShutdown` ordering |

**Status (2026-07-23):** C4, C10, C16, C17, C23 **fixed** with regression tests (C4/C16/C17; C10/C23 not observably testable until Phase 4 loopback tests). C18 **fixed** with regression test (`HealthCheckCallbackReentrantRegistration`): root cause was proven from a core dump — the monitoring thread invoked a health callback late (blocked on `callbacksMutex_` behind the test thread, after an unsynchronized epoch read of `lastHealth_.lastCheckTime` triggered a spurious immediate check), and the test's dangling `bool&` capture wrote `0x01` over a return address in the main thread's reused stack. Fix: snapshot-then-dispatch for health/event callbacks, loop-local health-check timer (`lastHealth_` deleted), scheduler error callback + `std::cerr` moved outside `statsMutex_`, test capture by `shared_ptr`. Verified: 374/374 ctest green, 0/100 crashes on the old repro (was 12/30), ASan clean (pre-existing C25 leak aside), TSan clean at C18's sites (remaining races are C25). C18's `executeTask` path still runs callbacks under the *caller's* `tasksMutex_` — that hold is C1's scheduler-loop fix. C1 **fixed** with regression tests (`AllReadyTasksExecutePerCycle` — pre-fix, 2 of 3 same-priority tasks got 0 executions in 600 ms; `RemoveTaskDuringExecutionIsSafe`): the scheduler loop now claims each ready task under `tasksMutex_`, executes without the lock, and releases/deallocates under the lock; `removeTask`/`removeAllTasks` defer deallocation of executing tasks via `SchedulerTask::pendingRemoval`; `executeTask`'s task bookkeeping (constraints/nextExecution/lastExecution) re-acquires `tasksMutex_` so only the user code itself runs unlocked (else it races `updateTaskConstraints`). Verified 376/376, ASan clean, TSan 0 warnings on scheduler tests. C2 **fixed** with regression tests (`SystemSafetyTest`): new core-owned `core::SafetyHook` interface (isEmergencyStopped/setEmergencyCallback) replaces the dead `setSafetyManager` API and removes the `axonvex::safety` forward-decl layering violation from `system.hpp`; `SafetyManager` implements the hook and fires the callback on e-stop (snapshot-then-dispatch); `AxonVexSystem::setSafetyHook` wires it to `emergencyShutdown`. Teardown hardening from review: hook dispatch runs under `emergencyCallbackMutex_` so `~AxonVexSystem`'s automatic `setEmergencyCallback(nullptr)` synchronizes with in-flight dispatch (no dangling `this`); `shutdownMutex_` (try-lock in `emergencyShutdown`, blocking in `stop()`'s join section and the destructor's wait) serializes thread-handle teardown now that e-stop can fire from any thread; self-join guards + destructor second-pass join cover e-stop from a system thread (regression test `EmergencyStopFromSystemThreadIsSafe`). Note: `evaluateAll` still calls `triggerEmergencyStop` under `policiesMutex_` — that is C12's scope. Verified 379/379, ASan clean, TSan clean at C2 sites (only the catalogued C25 warnings remain). C3 **fixed**: tagged free-list head (`{tag:32, index:32}` in one 64-bit atomic, tag incremented on every pop/push — memory-ordering argument documented in `memoryPool.hpp`); stress regression test `ConcurrentAllocateDeallocateNoDoubleHandout` is TSan/ASan-clean (the pre-fix ABA window was not natively reproducible; the test is the standing guard). Known: `MemoryPoolTest.PerformanceTest` asserts >10M ops/s and fails under TSan's slowdown by design — exclude perf tests from sanitizer CI jobs when presets land. C25 **fixed**: `updateSystemConfiguration` rejected once initialization begins (config immutable while worker threads read it — TSan clean, was 2 races); event-string leaks closed via shutdown-time publish guard + `drainEventQueue()` after event-thread join and before `initialize()`'s component rebuild (LSan zero leaks on system+safety tests, was 2072 B / 37 allocs). C12 **fixed** with regression tests (`HandlerMayMutatePolicyRegistryDuringDispatch`, `PolicyMayQueryManagerDuringEvaluate` — both deadlocked pre-fix; `EvaluationPeriodMayChangeWhileRunning`): `evaluateAll()` snapshots `{name, shared_ptr<SafetyPolicy>}` under `policiesMutex_` and then runs `evaluate()`, event dispatch, and `triggerEmergencyStop` with no policy lock held (the `shared_ptr` keeps a policy removed mid-cycle alive until the cycle ends); a new `evaluationMutex_` serializes evaluation cycles so a policy is never re-entered concurrently, and the thread running a cycle is published in `evaluatingThread_` so a re-entrant `evaluateAll()` from a policy/handler returns the cycle's worst-so-far level instead of blocking on a mutex it already holds or recursing until the stack is gone (`ReentrantEvaluateAllFromHandlerReturnsInsteadOfHanging`, `EmergencyStopFromHandlerDoesNotHang` — the first hung before the guard, found by cpp-reviewer); handlers moved off the entirely unsynchronized `core::Caller` to a `handlersMutex_`-guarded vector with snapshot-then-dispatch, so cross-thread and re-entrant (un)registration are safe; `evaluationPeriod_` is now `std::atomic<Duration>` (the evaluation loop read it unlocked while setters wrote it under `policiesMutex_`). Verified 385/385 ctest, TSan and ASan clean across the safety suite. Two load-sensitive tests (`TimingControllerTest.SchedulerPauseAndResume`, `PrecisionTimerTest.PerformanceTest`) fail intermittently when the machine is busy and pass on rerun — both are sleep/throughput-based and belong in the Phase-1 flake cleanup. Note for the sanitizer CI work: GCC 11's libtsan does not intercept `pthread_cond_clockwait`, so `condition_variable::wait_for` in test helpers produces bogus "double lock of a mutex"/race reports — poll an atomic with a deadline instead. Next up: sanitizer CI presets, then C5/C11/C13/C14.

## 4. Honesty audit (docs vs reality)

The README's "Key Features" block claims things that do not exist **yet**. Since all of them are v1.0 scope, the fix is not deletion but truthful status: each claim becomes a tracked commitment ("in development, targeted for v1.0") until its workstream ships it, at which point the claim points at code, tests, and benchmarks. What must still be fixed immediately: fictional community URLs/professional-services copy, the broken `CONTRIBUTING.md` link, the static build badge, and the bridge-app README documenting a nonexistent app.

| Claim | Reality |
|---|---|
| "<1 µs deterministic timing" | Never benchmarked; cv-based wait + mutexed ports + SCHED_OTHER fallback make it implausible as stated. CI validates the *fallback*, never the RT path. |
| "1M+ msgs/sec, sub-ms latency" | No benchmark exists in the tree. |
| "10,000+ processing blocks" | Soak test covers queue volume only, 2 tests, self-described "not micro-benchmarks". |
| "Angular 17+ dashboard, 3D scene, topology" | Zero frontend code. Backlog E07 all "Not Started". |
| "MFA, RBAC, end-to-end encryption" | Zero matches in source. SRS itself defers this (REQ-DEF-003). |
| "Hardware/software watchdogs" | Software-only `std::thread` timer. No /dev/watchdog. |
| "Windows/macOS support" | Sockets are Linux-only with silent no-op fallbacks; unproven claim. Static "Build Passing" badge, not a live one. |
| Community/forum/discord/docs URLs, Professional Services, 24/7 support | Fictional. `CONTRIBUTING.md` linked but absent. |
| Bridge app README | Documents an app (Imu pipeline, `Ros2AdapterNode`, `DataLoggerPU`) that does not exist in code; wrong build paths; unused package.xml deps. |

## 5. Architecture debt

1. **`AxonVexSystem` god class** — ~13 responsibilities (lifecycle FSM, config, logger, event pool/queue/thread, monitoring thread, health, recovery, stats, unit registry, system-port registry + cross-system connect, adapter registry, safety injection, memory accounting); 1,771-line .cpp, 914-line header, 130 graph edges, 0.02 cohesion. Decompose into: `LifecycleController`, `UnitRegistry`, `SystemPortRegistry`, `EventBus`, `HealthMonitor`; System becomes a façade.
2. **Layering inversion** — core's public header forward-declares `safety::SafetyManager` (`system.hpp:18`). Invert: safety subscribes to core, or introduce a core-owned `SafetyHook` interface that the safety lib implements.
3. **No RT-path invariant** — mutexes, heap allocation, string building, `std::cerr`, and user callbacks all occur on scheduler/port hot paths. Establish and enforce: *no locks, no allocation, no I/O, no user callbacks on the RT path*; everything defers to the event thread.
4. **Production socket code in header-only INTERFACE libs** — syscalls recompiled per-TU, never object-tested, OS headers leak into consumers. Move TCP/UDP (and future WS) into a compiled `axonvex_net` library.
5. **Header bloat** — `logger.hpp` 1,086 lines, `ports.hpp` 1,065, `missionPipeline.hpp` fully inline. Split declaration/implementation where templates allow.
6. **Statistics zoo** — 7+ `*Statistics` structs; CRTP `PerformanceStatisticsBase` adopted by only 3 and its CRTP parameter is unused. Pick one model.
7. **~1,500–2,000 LOC of deletable weight** (see §7).

## 6. v1.0 scope — the full vision, mapped to workstreams

v1.0 ships **everything the project vision commits to**. Positioning stays: "the deterministic C++ pipeline runtime with a wired safety envelope, replayable execution — and a live operations dashboard." Every vision pillar maps to a workstream (WS) below; nothing is deferred.

| Vision pillar (README/SRS) | Workstream | What "done" means in v1.0 |
|---|---|---|
| Precision execution, <1 µs class timing | WS-PERF | Reworked scheduler + documented RT deployment profile (SCHED_FIFO, mlockall, isolcpus); published p99 jitter benchmark on reference hardware, CI-gated |
| High performance: 1M+ msgs/sec, sub-ms latency | WS-PERF | Port/queue hot path lock- and alloc-free; throughput + latency benchmarks published and regression-gated |
| Scalable: single-node → distributed, 10,000+ blocks | WS-SCALE | 10k-block soak test in CI (nightly); system-to-system port bridging over hardened TCP/UDP transports for multi-node composition |
| Modular design, plugin system | WS-ECO | Real MODULE `.so` plugins with C ABI version handshake; ROS 2 plugin complete (casters, services); MAVLink adapter functional against SITL |
| Real-time visualization: Angular 17+ dashboard, charts, topology, 3D, logs, mission/safety panels | WS-VIZ | Production WebSocket gateway (Boost.Beast, C++11-compatible) bridging a reworked TelemetryBus (bounded queues + egress thread); JSON + MessagePack; telemetry recording/playback; full Angular workspace under `dashboard/` |
| Mission-critical safety: multi-layer, hardware/software watchdogs | WS-SAFE | SafetyManager wired to halt the scheduler; software watchdog feeding it; Linux `/dev/watchdog` hardware watchdog integration; measured e-stop latency budget |
| Enterprise security: MFA, RBAC, end-to-end encryption | WS-SEC | TLS (OpenSSL via Conan) on TCP + WebSocket; local user store with TOTP-based MFA; role→permission RBAC enforced on gateway/control API; audit log |
| Cross-platform: Linux, Windows 10+, macOS 10.15+ | WS-PLAT | Winsock + `LoadLibrary` plugin loader + Windows thread-priority path; macOS kqueue-free portable sockets; all three OSes green in CI running the full suite |
| Developer-friendly: docs, tooling | WS-DX | Doxygen API reference, `docs/index.md`, tutorials, verified quick start, CONTRIBUTING/CHANGELOG, conan package |

The foundation phases (0–3 below) are unchanged prerequisites: none of the vision workstreams can sit on a core with C1–C24 unfixed.

**Scoping notes (honest constraints inside full scope):**
- "Distributed systems" v1.0 = multi-node composition via system-to-system port bridging over the hardened transports — not a DDS/consensus layer. That is what the current architecture (`connectToSystem` + adapters) can credibly grow into within v1.0.
- "End-to-end encryption" v1.0 = TLS on every network hop plus authenticated gateway sessions — not application-layer crypto between processing units.
- "<1 µs" is achievable as *timer/measurement* precision and as scheduler-wake p99 on tuned hardware (isolcpus + SCHED_FIFO); the benchmark defines the claim, published per reference platform.
- Hardware watchdog is Linux `/dev/watchdog` in v1.0; other platforms get the software layer.

## 7. Deletion list (lean v1.0)

- `Configuration`: snapshots/rollback, templates, file-watching stubs, `getMemoryUsage`, `getPerformanceMetrics`, `expandKeyPattern` (~350 LOC).
- Logger: entire `axonvex::Log` stream layer (`ColoredStreamLogger`/`FileLogger`, ODR-risky template-char-array design), `logf`/`LOGF_*` until real formatting exists.
- `utils/caching/lruCache.hpp` (unused; `capacity==0` UB); `coreUtilities.hpp` ResourceUtils/DebugUtils/AlignmentUtils/MathUtils/ScopedTimer (std reinventions; keep MemoryOrdering + one `nextPowerOf2`).
- `Path`: std-forwarding boilerplate, `getSystemInfo`, validator registry, backup helpers.
- `WebSocketServer` placeholder (hard compile-gate it now; WS-VIZ replaces it with the real Boost.Beast implementation — the placeholder must never silently satisfy `ProtocolInterface` in the meantime).
- Statistics: collapse `MemoryStatistics` vs `MemoryPoolStatistics` duplication; de-CRTP the base or drop it.
- `system.hpp`: every declared-unimplemented method (C6).

## 8. Phased plan (full-vision v1.0)

Foundation first (Phases 0–3), then the vision workstreams in dependency order. Milestones: **v1.0.0-alpha** after Phase 3, **-beta** after Phase 7, **-rc** after Phase 9.

### Phase 0 — Truth alignment (1–2 days)
- README "Key Features" gains an honest status column (shipped / in development for v1.0) mapped to the WS table in §6; delete fictional community URLs and professional-services copy; real CI badge; fix or rewrite the bridge-app README; add `CONTRIBUTING.md`, `CHANGELOG.md`. SRS §2.3 stays — it is now committed v1.0 scope (WS-VIZ).
- Exit: every claim maps to code, a benchmark, or a tracked v1.0 workstream.

### Phase 1 — Correctness (2–3 weeks)
- Fix C1–C24 in priority order; each fix lands with a regression test (TDD where feasible).
- ~~Add ASan/UBSan/TSan CMake presets + CI jobs (TSan is the gate for C3/C5/C12/C18/C22-class bugs).~~ **Done**: `-DAXONVEX_SANITIZER=address|thread|undefined` (root `CMakeLists.txt`) plus a 3-way `sanitizers` matrix job in CI. All three are green at 373/373; the 12 quarantined tests are listed with reasons in `.github/workflows/ci.yml` — throughput asserts that cannot survive sanitizer slowdown, plus two known races (C29, C24). Presets were skipped deliberately: Conan 2 generates `CMakeUserPresets.json`, and a cache option composes with the existing CI flow instead of fighting it.
- Replace sleep-based test assertions with condition-based waits (flake elimination).
- Exit: TSan/ASan/UBSan clean across the full suite; no declared-unimplemented API.

### Phase 2 — Architecture (2–3 weeks)
- Decompose `AxonVexSystem` (LifecycleController / UnitRegistry / SystemPortRegistry / EventBus / HealthMonitor).
- Enforce the RT-path invariant; rework scheduler execution model (ready-set per tick, execution outside locks, optional worker pool).
- Wire SafetyManager → scheduler halt path; fix core↔safety layering via a core-owned hook interface.
- Move sockets to compiled `axonvex_net`; execute the §7 deletion list; make `ProcessingUnit` non-movable; `MissionPipeline` thread-correct.
- Rework `TelemetryBus`: bounded lock-free queue between publishers and a dedicated egress thread; framed serialization contract (JSON now, MessagePack in WS-VIZ). This is the load-bearing joint for the whole visualization platform — it lands here, not later.
- **Composer foundations (see §11)**: define **SystemSpec v1** — a JSON schema describing a full system declaratively (units, port wiring, timing constraints, adapters, safety policies, mission pipelines); add a `UnitFactory` registry (unit types constructible by name) and a unit **metadata/introspection contract** (each unit type describes its ports, data types, and configurable parameters). Implement `AxonVexSystem::loadFromSpec()` so a system can be instantiated from data instead of C++ subclassing — the decomposed registries (UnitRegistry/SystemPortRegistry) are designed around this from day one.
- Exit: `graphify update .` shows the god node dissolved; RT path audited lock/alloc-free; e-stop halts a running system in a test; TelemetryBus decoupled from RT threads; an example system boots from a `.axv.json` spec with zero subclass code.

### Phase 3 — Proof & release engineering (2 weeks) → **v1.0.0-alpha**
- Benchmark harness (Google Benchmark): scheduler jitter, port throughput, e2e latency, e-stop latency; results in `docs/benchmarks.md`; CI perf-regression gate on Linux.
- Real integration tests: TCP/UDP loopback data exchange, real `dlopen` of a built sample plugin `.so`, `RealTimeScheduler` direct tests (privilege-guarded with `GTEST_SKIP`), TelemetryBus tests.
- Packaging: remove `CMAKE_INSTALL_PREFIX` force-override; SOVERSION + symbol visibility; `axonvex/version.hpp`; `conanfile.py` + test_package; CI job that installs and builds a downstream `find_package(axonvex)` consumer.
- Real `.clang-tidy` config; coverage reporting.
- Exit: `conan create` and `find_package` work from a clean machine; benchmarks published.

### Phase 4 — WS-PLAT + transports hardening (3–4 weeks)
- `axonvex_net` production pass: length-prefixed framing, partial-send loops, `MSG_NOSIGNAL`, DNS resolution, IPv6-capable API, fd lifecycle correctness.
- Windows port: Winsock2 sockets, `LoadLibrary`/`GetProcAddress` plugin loader, `SetThreadPriority`/timeBeginPeriod timing path; macOS port verification; remove all silent no-op platform fallbacks (unsupported = compile error or explicit runtime error, never fake success).
- CI: full test suite green on ubuntu/windows/macos, Debug+Release; artifact upload.
- Exit: three-OS CI truthfully green; no simulated-success code paths remain.

### Phase 5 — WS-PERF + WS-SCALE (3–4 weeks)
- Scheduler performance work to the vision numbers: per-tick ready-set execution, optional multi-worker executor, lock-free task stats; documented RT deployment profile (SCHED_FIFO, mlockall, isolcpus, IRQ affinity).
- Port hot path: alloc-free steady state (fixed pools, no string building), benchmark-driven optimization to 1M+ msgs/sec on reference hardware.
- 10,000-block soak test (nightly CI) with memory/jitter regression tracking.
- Multi-node composition: system-to-system port bridging over `axonvex_net` (typed serialization at the boundary), demo app with two processes exchanging pipeline data.
- Exit: published benchmarks meet or honestly restate the README numbers; 10k soak green; two-node demo in CI.

### Phase 6 — WS-SAFE completeness (2 weeks)
- Hardware watchdog: Linux `/dev/watchdog` integration behind the existing `Watchdog` API; petting wired to scheduler liveness; failure → hardware reset path documented.
- Layered safety per the architecture doc: unit-level policies → system SafetyManager → hardware watchdog; latching + hysteresis in the policy model; e-stop latency budget measured end-to-end.
- Exit: pull-the-thread test on real hardware (policy violation → scheduler halt → watchdog pet stops) documented with timings.

### Phase 7 — WS-ECO adapters & plugins (3 weeks) → **v1.0.0-beta**
- ROS 2 plugin complete: default casters for common types implemented, functional `createServer`, registry keyed by (InternalType, RosMsg) pair, `unique_ptr` returns; built as a real MODULE `.so` with ABI handshake; bridge app matches its README.
- MAVLink adapter: `AdapterBase` implementation over UDP using the MAVLink C library, validated against ArduPilot/PX4 SITL in CI (containerized).
- Plugin developer guide + sample plugin template.
- **Composer foundations**: plugins export their unit types into the `UnitFactory` with full introspection metadata (ports, types, parameters), so spec-loaded and future visually-composed systems can use plugin-provided units identically to built-ins.
- Exit: both adapters demonstrably exchange live data in CI; plugin loads via real dlopen/LoadLibrary on both OSes; a plugin-provided unit instantiates from a SystemSpec.

### Phase 8 — WS-SEC (3–4 weeks)
- TLS everywhere: OpenSSL via Conan; `axonvex_net` TLS variants for TCP client/server and the WebSocket gateway; cert config surface in `Configuration`.
- AuthN: local user store (argon2id-hashed credentials) + TOTP MFA for gateway/dashboard sessions; token-based session management.
- AuthZ: role → permission model (viewer / operator / admin) enforced on every gateway control endpoint (mission control, safety override, config mutation).
- Audit log of authenticated control actions through the existing Logger.
- Exit: security test suite (authn/authz/TLS handshake, negative cases); no plaintext control path remains enabled by default.

### Phase 9 — WS-VIZ visualization platform (6–8 weeks) → **v1.0.0-rc**
- Production WebSocket server (Boost.Beast, C++11-compatible with the C++14 tree) replacing the placeholder; TelemetryBus→WS gateway with per-client bounded queues and drop policy; JSON + MessagePack encodings; channel subscribe protocol.
- Telemetry recording (channel snapshots to disk) and playback through the same gateway — reuses the trace/replay design.
- Angular 17+ workspace under `dashboard/` (standalone components, signals): live telemetry charts (ngx-charts/D3), system topology view fed by the unit/port registries, Three.js 3D scene (poses, trajectories), log viewer, mission control panel, safety panel (e-stop, policy states), drag-and-drop layout with session persistence; auth against Phase 8 (login, MFA, role-gated controls).
- **Composer foundations**: the topology view renders from the same **SystemSpec graph schema** the composer will edit (one shared graph model/component, read-only in v1.0); the gateway control API includes `spec/validate`, `spec/get` (export running system as SystemSpec), and RBAC-gated `spec/deploy` (instantiate + start from an uploaded spec) — the composer backend, shipped and tested before its UI exists.
- Dashboard e2e tests (Playwright) against a live example system; dashboard build integrated into CI.
- Exit: `docker compose up` (or equivalent) starts an example system + gateway + dashboard; an operator can watch telemetry, replay a recording, and trigger a role-gated e-stop from the browser.

### Phase 10 — Hardening & release (2 weeks) → **v1.0.0**
- Doxygen API reference + `docs/index.md` + tutorials (first pipeline, first adapter, first plugin, dashboard setup); quick start verified in a clean container.
- Extended soak (24h+), RC bug-fix cycle, release notes, semver/ABI policy documented, tagged release with conan package + dashboard artifacts.

**Totals:** ~30–38 engineering weeks (≈7–9 months solo). The dashboard track (Phase 9 Angular work) is independent of Phases 4–8 once the Phase 2 TelemetryBus contract and Phase 8 auth API are fixed, so a second developer compresses the calendar to ≈4–5 months. Phases 0–1 remain non-negotiable before anything public ships, including an alpha.

## 9. Quality gates for v1.0 (definition of done)

1. TSan/ASan/UBSan-clean full test suite on Linux CI.
2. E-stop test: running pipeline halts within a measured bound after policy violation — including the hardware-watchdog layer on a reference device.
3. Published benchmark numbers backing every README performance claim (<1 µs-class jitter profile, 1M+ msgs/sec, 10k-block soak), regression-gated in CI.
4. Fresh-machine `conan create` + downstream `find_package` consumer build in CI.
5. Real plugin `.so`/DLL loaded via dlopen/LoadLibrary in CI with ABI handshake, on Linux and Windows.
6. ROS 2 and MAVLink adapters exchanging live data in CI (SITL for MAVLink).
7. Full suite green on ubuntu/windows/macos with no simulated-success fallbacks.
8. Dashboard e2e: login with MFA → live charts → replay a recording → role-gated e-stop, automated in CI.
9. No plaintext, unauthenticated control path enabled by default; security test suite green.
10. Zero declared-but-unimplemented public symbols (link test); every README claim traceable to code, test, or benchmark.

## 10. Risks (name them now, not at month six)

- **Solo bandwidth vs. scope**: the vision is a platform, not a library. The milestone gates exist so an alpha/beta is always shippable if the calendar slips; the dashboard track is the natural place to add a second pair of hands.
- **RT numbers are hardware-truths**: <1 µs-class jitter requires tuned Linux (isolcpus, SCHED_FIFO). The benchmark + deployment profile *is* the claim; without dedicated reference hardware the number cannot be honestly published.
- **C++14 ceiling**: Boost.Beast, OpenSSL, and MAVLink C are all C++14-compatible, so no conflict today — but the constraint should be revisited at v1.0 (C++17 unlocks `std::optional`/`std::filesystem` and deletes two compat layers).
- **Security scope creep**: MFA/RBAC is bounded to the gateway/control surface. Anything beyond (LDAP/SSO, per-unit crypto) is explicitly out until v1.x.
- **Windows RT semantics**: Windows cannot match SCHED_FIFO determinism; v1.0 documents per-platform timing classes rather than pretending parity.
- **Composer-foundation drift**: if SystemSpec/UnitFactory/introspection are treated as optional extras during Phases 2–9, the composer (§11) becomes a rewrite instead of a UI. The exit criteria in Phases 2, 7, and 9 exist precisely to prevent this.

## 11. Next release (v1.1) headline — the Visual System Composer

**The product idea:** don't just *observe* an `AxonVexSystem` in the browser — **design and compose one there**. A visual, node-graph web studio where an engineer drags processing units from a palette (built-ins + plugin-provided), wires typed ports on a canvas, attaches timing constraints, safety policies, adapters, and mission pipelines, validates the design, and deploys it to a live runtime — with the v1.0 dashboard then observing what was just composed. Design → deploy → observe → iterate, all in one app. This is the feature that turns AxonVex from a C++ library into a platform.

**Why it works without a rewrite:** v1.0 deliberately ships the entire backend for it —
1. **SystemSpec v1** (Phase 2): the declarative JSON system description *is* the composer's document format.
2. **UnitFactory + introspection metadata** (Phases 2, 7): the palette, property inspectors, and port-type compatibility rules are generated from unit metadata — including plugin units — not hand-maintained.
3. **Gateway spec API** (Phase 9): `spec/validate`, `spec/get`, `spec/deploy` (RBAC-gated) — the composer UI is a client of an API that v1.0 already tests.
4. **Shared graph model** (Phase 9): the dashboard topology view and the composer canvas are one component in two modes (read-only vs. editable).
5. **Security** (Phase 8): composing/deploying is an `admin`-role capability from day one; MFA and audit logging already cover it.

**v1.1 scope (the UI + the deltas):**
- Composer canvas: node-graph editor on the shared graph model (custom Angular canvas or a library such as Rete.js/JointJS — decide by spike); unit palette from `UnitFactory` metadata; typed-port connection validation live on the canvas (type mismatch = can't wire); property inspector per unit/port/connection (parameters, timing constraints, downsampling, memory-pool sizing).
- System-level design surfaces: safety-policy attachment panel, mission-pipeline sub-editor (elements + transition graph), adapter/transport configuration (ROS 2 topics, MAVLink endpoints, TCP/UDP), multi-system composition (bridged systems on one canvas).
- Design lifecycle: save/load SystemSpec documents (git-friendly JSON), spec versioning + diff view, server-side validation with inline errors, dry-run instantiate, deploy-to-runtime, and **round-trip**: import a running system (`spec/get`) back onto the canvas.
- Simulation aid: instantiate the composed system with generator/mock units in a sandbox runtime and watch it in the dashboard before deploying against real hardware.
- Runtime deltas (C++): hot spec re-deploy (stop → swap → start with state disposition rules), richer validation diagnostics (cycle detection, unconnected required ports, timing feasibility check against declared constraints).

**Estimate:** 10–14 weeks after v1.0 (canvas editor ≈ half of it). Frontend-heavy, so it parallelizes cleanly with post-1.0 C++ maintenance.

**Non-negotiable v1.0 exit criteria that guard this feature** (already embedded above): a system boots from `.axv.json` with zero subclass code (Phase 2); plugin units instantiate from spec (Phase 7); `spec/validate|get|deploy` live behind RBAC (Phase 9); topology view renders from the SystemSpec schema (Phase 9).
