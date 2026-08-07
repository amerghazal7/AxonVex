# AxonVex Benchmarks

Phase 3 (WS-PERF) performance-measurement track. Per `CLAUDE.md`'s
docs-syncing rule, **this file is the only place performance numbers live**
in this repository — the README links here, it never inlines a number.

## Status

**No numbers are published in this document yet.** The harness now builds,
links, and runs (Conan 2.31.2, default gcc-11/Release profile; a build-type
propagation bug that previously made `AXONVEX_BUILD_BENCHMARKS=ON` fail at
compile time with `benchmark/benchmark.h: No such file or directory` is
fixed — see root `CMakeLists.txt`), but it has not been run on tuned,
isolated reference hardware per "RT deployment profile" below, so no numbers
are published here yet.

Do not treat the absence of numbers here as "benchmarks not written" — the
harness exists (`benchmarks/*.cpp`, gated CMake target) and builds cleanly.
It means "not yet measured under conditions worth recording, on purpose,
rather than measured wrong." A maintainer must run it once under the RT
deployment profile below and paste real output — with CPU model, kernel
version, and governor/isolation settings — before any number is added to
this file.

### Untuned baseline (2026-08-07) — NOT the reference-platform numbers

Recorded because it is the first optimized measurement this project has ever
been able to take: until the C49 fix landed, a plain `set(CMAKE_BUILD_TYPE
Debug)` shadowed the cache variable, so `-DCMAKE_BUILD_TYPE=Release` was
silently ignored and **every build in the project's history was `-O0`**.

This run does **not** satisfy the RT deployment profile below and must not be
quoted as the framework's performance. It is a sanity baseline only.

- Platform: 13th Gen Intel Core i9-13900K, 32 logical CPUs, kernel
  6.8.0-136-generic, gcc 11, `-O3 -DNDEBUG -std=gnu++14`, Conan 2.31.2.
- Tuning: **none.** `powersave` governor, no `isolcpus`, no `SCHED_FIFO`, no
  `mlockall`, no IRQ affinity, shared desktop under load.
- Command: `--benchmark_min_time=0.05s` (short run; the reference-platform
  run must use the full default time and more repeats).

| Benchmark | Result |
|---|---|
| Port write+read, non-thread-safe | 43.5 ns/op — 23.0 M msgs/s |
| Port write+read, thread-safe (locked) | 67.7 ns/op — 14.8 M msgs/s |
| Pipeline latency, 2 stages | p50 0.199 µs, p99 0.201 µs, max 11.2 µs |
| Pipeline latency, 5 stages | p50 0.491 µs, p99 0.495 µs, max 3.77 µs |
| Pipeline latency, 10 stages | p50 0.980 µs, p99 1.24 µs, max 5.09 µs |
| E-stop latency | p50 0.090 µs, p99 0.094 µs, max 2.86 µs |
| Scheduler jitter, 1 ms period | p50 8.1 µs, p99 45.0 µs (period offset 45.1 µs) |
| Scheduler jitter, 5 ms period | p50 3.5 µs, p99 49.8 µs (period offset 49.8 µs) |

What this does and does not support, stated plainly:

- **Throughput and latency claims look sound.** The README's "1M+ msgs/sec"
  is conservative against 23.0 M/s here, and "sub-millisecond latency" holds
  by three orders of magnitude. Thread safety costs ~24 ns/op (~56%), which
  is the expected direction — an earlier revision of this benchmark had its
  argument labels inverted and would have published the opposite.
- **The "<1 µs class" timing claim is NOT demonstrated by this run.** p99
  scheduler jitter is ~45–50 µs on an untuned desktop. That is what an
  untuned box costs, not necessarily a framework limit — but the claim stands
  or falls on the tuned reference-platform run, which has not happened. Do not
  cite it until it does.
- `period_offset` is reported separately from jitter on purpose: it is the
  systematic gap between requested and achieved period, and folding it into
  "jitter" (as an earlier revision did) overstated jitter by roughly 17×.

### RT deployment profile (set this up first)

The harness itself does none of this — it is plain userspace code with no
`mlockall`/`sched_setscheduler`/CPU-pinning calls. Without the steps below,
numbers reflect an untuned, default-governor Linux box, not the RT
deployment this framework targets:

- **CPU frequency scaling**: pin the governor to `performance` on every core
  the benchmark or its scheduler thread may land on —
  `sudo cpupower frequency-set --governor performance`, verify with
  `cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`. Google
  Benchmark's own `***WARNING*** CPU scaling is enabled` at startup means
  this step was skipped.
- **Isolate a core**: reserve a core the rest of the system won't schedule
  onto, either at boot (`isolcpus=<N>` kernel parameter) or per-run with
  `cset shield`, then pin the benchmark process to it:
  `taskset -c <N> ./build/benchmarks/axonvex_benchmarks ...`.
- **IRQ affinity**: move interrupts off the isolated core, e.g.
  `cat /proc/interrupts` to find active IRQs, then write a mask excluding
  `<N>` to each `/proc/irq/<n>/smp_affinity`, or ban the core from
  `irqbalance` (`IRQBALANCE_BANNED_CPUS` in its config).
- **Scheduling priority**: run under `SCHED_FIFO` — `chrt -f 80
  ./build/benchmarks/axonvex_benchmarks ...` (needs `CAP_SYS_NICE` or a
  `/etc/security/limits.d` `rtprio` entry for the invoking user).
- **Memory locking**: neither the harness nor `TimingController`/
  `SafetyManager` calls `mlockall`, so a page fault mid-measurement is still
  possible even with everything above in place. `ulimit -l unlimited`
  before the run removes the `RLIMIT_MEMLOCK` ceiling but does not by
  itself lock anything — actually calling `mlockall` is process code this
  harness does not have. Tracked as a gap, not solved by this doc.

None of this is optional for a number that will be quoted as this
framework's RT characteristic; it is the difference between measuring
`TimingController` and measuring `cpufreq`.

### What a maintainer must run

```bash
# Either via Conan (adds benchmark/1.8.3, see conanfile.txt) ...
conan install . --output-folder=build --build=missing
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release -DAXONVEX_BUILD_BENCHMARKS=ON
# ... or against a system-installed Google Benchmark (apt install
# libbenchmark-dev, or equivalent), skipping the Conan step above:
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAXONVEX_BUILD_BENCHMARKS=ON

cmake --build build -j$(nproc) --target axonvex_benchmarks
./build/benchmarks/axonvex_benchmarks --benchmark_repetitions=5
```

Run in `Release`, not `Debug` — the whole point is to measure real
scheduling/port/e-stop cost, not `-O0` overhead. Apply the RT deployment
profile above first: the harness itself does not isolate CPU frequency
scaling or run under `isolcpus`/real-time priority, so an untuned run's
numbers are not meaningful across machines or even across repeated runs on
the same one. Report CPU model, kernel version, and the governor/isolation
settings actually used alongside the numbers.

## Why `AXONVEX_BUILD_BENCHMARKS` is off by default and `REQUIRED`

Google Benchmark is a new dependency the rest of the tree does not need.
`AXONVEX_BUILD_BENCHMARKS` (root `CMakeLists.txt`) defaults `OFF` so a normal
build never pulls it in. When turned `ON`, `find_package(benchmark
REQUIRED)` is used deliberately instead of a bare/`QUIET` lookup: a
`QUIET` lookup with no `REQUIRED` would silently define zero benchmark
targets when the package is missing, and the surrounding build would still
report success — a "benchmarks pass" that passed because nothing was
compiled. `REQUIRED` makes an unmet dependency a configure-time hard error
instead, the same class of fix this codebase's platform `#ifdef` islands
follow (never silently no-op and return success).

## Harness layout

| File | What it measures | Metric shape |
|---|---|---|
| `benchmarks/schedulerJitterBenchmark.cpp` | `TimingController` real-time scheduling jitter: a scheduled `ProcessingUnit`'s actual `processSync()` inter-arrival intervals, over 200 measured cycles per repetition (20 discarded warmup cycles first), split into a systematic `period_offset_us` (median interval vs. the configured period) and true jitter (spread of intervals around their own median) | `period_offset_us`, plus p50/p95/p99/max jitter, microseconds |
| `benchmarks/portThroughputBenchmark.cpp` | `OutputPort::write()` → connected `InputPort::read()`, single producer/consumer, `threadSafe` on and off | items/sec (throughput, not latency-shaped — a rate claim, not a tail claim) |
| `benchmarks/pipelineLatencyBenchmark.cpp` | End-to-end wall time through a chain of `ProcessingUnit`s connected by sync ports, driven directly (no scheduler thread hop — that's the jitter benchmark's job) | p50/p95/p99/max latency, microseconds, at 2/5/10 chained stages |
| `benchmarks/estopLatencyBenchmark.cpp` | `SafetyManager::triggerEmergencyStop()` call latency — this path dispatches to registered handlers synchronously in the caller's thread (no queue hop), so the call's wall time is the notification latency a real handler would see | p50/p95/p99/max latency, microseconds |

### Methodology notes (apply to every benchmark above)

- **Setup excluded from the timed region.** Unit/controller/port
  construction and connection happens before the `for (auto _ : state)` loop
  (or, for the jitter benchmark, before/after the timed body's warmup phase
  — see below). Only the operation under test is inside the measured region.
- **Explicit warmup, discarded.** Every benchmark runs untimed warmup passes
  (port: 100 write/read pairs; pipeline: 50 passes; e-stop: 50 trigger/reset
  pairs; scheduler: 20 scheduling cycles) before starting the measured
  region, so first-touch page faults and cold caches don't inflate the
  numbers a maintainer eventually pastes here.
- **Latency-shaped results are reported as p50/p95/p99/max, never a mean.**
  The scheduler-jitter, pipeline-latency, and e-stop-latency benchmarks each
  collect every individual sample into a `std::vector<double>` and compute
  percentiles by hand, published via `benchmark::State::counters` — Google
  Benchmark's own default output (mean/median/stddev over repeated timed-loop
  calls) is the wrong shape for an RT tail claim. Port throughput is
  reported as items/sec instead, since it is a rate claim, not a tail claim.
- **`benchmark::DoNotOptimize` / `ClobberMemory`.** Every read result that
  the optimizer could otherwise prove unused (and delete the whole loop
  over) is wrapped in `DoNotOptimize`; `ClobberMemory()` follows the writes
  in the timed region so the compiler can't hoist or eliminate them either.
- **Scheduler jitter runs one controller lifecycle per Google-Benchmark
  "iteration"** (`->Iterations(1)->Repetitions(5)`), not a loop the timer
  runs an unpredictable number of times to fill a minimum time window —
  each lifecycle already does real, non-mocked warmup+measured wall-clock
  scheduling internally, and repeating that lifecycle more than the 5
  explicit repetitions buys nothing.
- **Scheduler jitter separates offset from jitter.** A per-cycle constant
  (scheduler wake-up/dispatch overhead) shows up in every inter-arrival
  interval regardless of period; folding it into "jitter" would overstate
  the true spread by that constant. `period_offset_us` reports the median
  interval's deviation from the requested period; `jitter_p50/p95/p99/max_us`
  report the spread of intervals around their own median, not around the
  nominal period.

## Adding a benchmark

New benchmark files go in `benchmarks/`, added to
`benchmarks/CMakeLists.txt`'s `axonvex_benchmarks` executable sources (there
is one binary, not one per benchmark — `benchmark::benchmark_main` supplies
`main()`). They are not registered as `ctest` cases: they measure real
wall-clock time and belong to this doc, not to the pass/fail correctness
suite `ctest` runs.
