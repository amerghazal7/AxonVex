# AxonVex Benchmarks

Phase 3 (WS-PERF) performance-measurement track. Per `CLAUDE.md`'s
docs-syncing rule, **this file is the only place performance numbers live**
in this repository — the README links here, it never inlines a number.

## Status

**No numbers are published in this document yet.** The benchmark harness
below was built and reviewed but has not been run to completion on
reference hardware:

- The sandbox this harness was authored in has no Conan installation and no
  `libbenchmark-dev` (Google Benchmark headers/CMake config) — only the
  unrelated runtime package `libbenchmark1` is present. `find_package(benchmark
  REQUIRED)` in this environment would hard-fail, by design (see
  "Why `REQUIRED`" below) — it was never made to pass here, so the
  `AXONVEX_BUILD_BENCHMARKS=ON` configure path, the benchmark build, and the
  benchmark binary itself are **unverified** in this environment.
- Every source file under `benchmarks/` was written directly against the
  real public APIs (`TimingController`, `ProcessingUnit`/ports,
  `SafetyManager`) with signatures and struct layouts checked against the
  headers, but has not been compiled.

Do not treat the absence of numbers here as "benchmarks not written" — the
harness exists (`benchmarks/*.cpp`, gated CMake target). It means "not yet
measured, on purpose, rather than measured wrong." A maintainer with Conan
(or `apt install libbenchmark-dev`, version 1.6+) available must run it once
and paste real output before any number is added to this file.

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
scheduling/port/e-stop cost, not `-O0` overhead. Report CPU model, kernel
version, and whether the machine was otherwise idle alongside the numbers:
none of these benchmarks isolate CPU frequency scaling or run under
`isolcpus`/real-time priority, so cross-machine or cross-run comparisons
without that context are not meaningful.

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
| `benchmarks/schedulerJitterBenchmark.cpp` | `TimingController` real-time scheduling jitter: deviation between a scheduled `ProcessingUnit`'s actual `processSync()` inter-arrival time and its configured period, over 200 measured cycles per repetition (20 discarded warmup cycles first) | p50/p95/p99/max jitter, microseconds |
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

## Adding a benchmark

New benchmark files go in `benchmarks/`, added to
`benchmarks/CMakeLists.txt`'s `axonvex_benchmarks` executable sources (there
is one binary, not one per benchmark — `benchmark::benchmark_main` supplies
`main()`). They are not registered as `ctest` cases: they measure real
wall-clock time and belong to this doc, not to the pass/fail correctness
suite `ctest` runs.
