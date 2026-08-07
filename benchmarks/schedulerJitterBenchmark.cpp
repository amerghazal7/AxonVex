/**
 * @file schedulerJitterBenchmark.cpp
 * @brief Google Benchmark: TimingController real-time scheduling jitter
 *        (Phase 3 WS-PERF).
 *
 * Runs a real TimingController against a real ProcessingUnit on the
 * scheduler's own worker thread (started via TimingController::start(),
 * which is non-blocking -- see timingController.cpp's RealTimeScheduler::
 * start(), which spawns schedulerLoop() on a std::thread and returns).
 * Jitter is computed from the probe unit's OWN processSync() call
 * timestamps, not from TimingController::getPerformanceMetrics()
 * .schedulingJitter/.averageSchedulingJitter -- those are a single running
 * value and a running mean (see timingController.hpp's SchedulerStatistics),
 * which is exactly the "mean jitter is meaningless for an RT claim" trap.
 * This file instead keeps every inter-arrival sample and reports
 * p50/p95/p99/max -- of true jitter only. Each inter-arrival interval is
 * split into a systematic `period_offset_us` (median interval vs the
 * requested period -- fixed per-cycle scheduler overhead) and jitter
 * (spread of intervals around their own median). Publishing
 * fabs(interval - period) as a single "jitter" number conflates the two;
 * see computeIntervalStats() below.
 */

#include <algorithm>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/timingController.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Records a timestamp on every processSync() call and signals when a caller-
// armed target count has been reached, so the benchmark can wait for exactly
// N cycles (warmup, then measured) instead of sleeping a guessed duration.
class JitterProbeUnit : public axonvex::core::ProcessingUnit {
  public:
    explicit JitterProbeUnit(const std::string& name) : ProcessingUnit(name) {}

    void processSync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.push_back(Clock::now());
        if (timestamps_.size() >= target_) {
            done_ = true;
            cv_.notify_all();
        }
    }
    void processAsync() override {}
    void reset() override {}
    void initialize() override {}
    std::string getTypeDescription() override {
        return "JitterProbeUnit";
    }

    // Caller must not be racing processSync() when calling arm() -- this
    // benchmark only calls it while the scheduler is stopped or the probe is
    // already known done (checked via waitDone() first).
    void arm(size_t target) {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.clear();
        target_ = target;
        done_ = false;
    }
    void waitDone() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_; });
    }
    std::vector<Clock::time_point> samples() {
        std::lock_guard<std::mutex> lock(mutex_);
        return timestamps_;
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Clock::time_point> timestamps_;
    size_t target_{0};
    bool done_{false};
};

double percentile(const std::vector<double>& sortedAscending, double p) {
    if (sortedAscending.empty()) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(p * static_cast<double>(sortedAscending.size() - 1));
    return sortedAscending[idx];
}

struct IntervalStats {
    double offsetUs = 0.0;
    double jitterP50Us = 0.0;
    double jitterP95Us = 0.0;
    double jitterP99Us = 0.0;
    double jitterMaxUs = 0.0;
};

// Splits each inter-arrival interval into a systematic offset (the median
// interval vs the requested period -- fixed per-cycle scheduler overhead,
// constant regardless of period) and true jitter (spread of intervals
// around their OWN median, not around the nominal period). Reporting
// fabs(interval - period) as "jitter" previously conflated the two: with a
// ~50us constant per-cycle overhead, that quantity was mostly the offset,
// not the ~1-3us of actual spread -- see docs/benchmarks.md and the fix
// commit for the measured numbers this replaces.
IntervalStats computeIntervalStats(std::vector<double> intervalsUs, double periodUs) {
    IntervalStats stats;
    if (intervalsUs.empty()) {
        return stats;
    }
    std::sort(intervalsUs.begin(), intervalsUs.end());
    const double medianIntervalUs = percentile(intervalsUs, 0.50);
    stats.offsetUs = medianIntervalUs - periodUs;

    std::vector<double> jitterUs;
    jitterUs.reserve(intervalsUs.size());
    for (double v : intervalsUs) {
        jitterUs.push_back(std::fabs(v - medianIntervalUs));
    }
    std::sort(jitterUs.begin(), jitterUs.end());
    stats.jitterP50Us = percentile(jitterUs, 0.50);
    stats.jitterP95Us = percentile(jitterUs, 0.95);
    stats.jitterP99Us = percentile(jitterUs, 0.99);
    stats.jitterMaxUs = jitterUs.empty() ? 0.0 : jitterUs.back();
    return stats;
}

// Regression check for the offset/jitter decomposition above, run at
// process start every time this benchmark binary is executed (an assert()
// would vanish under the -DNDEBUG that Release (the documented build type
// for this harness) defines, i.e. disappear exactly when it matters).
// Synthetic intervals: period=1000us, median interval=1050us (systematic
// +50us offset), spread of +/-2us around that median.
struct JitterMathSelfCheck {
    JitterMathSelfCheck() {
        const std::vector<double> synthetic = {1048.0, 1049.0, 1050.0, 1051.0, 1052.0};
        const IntervalStats stats = computeIntervalStats(synthetic, 1000.0);
        const bool ok = std::fabs(stats.offsetUs - 50.0) < 1e-9 &&
                        std::fabs(stats.jitterMaxUs - 2.0) < 1e-9 &&
                        std::fabs(stats.jitterP50Us - 1.0) < 1e-9;
        if (!ok) {
            std::fprintf(stderr,
                         "schedulerJitterBenchmark self-check FAILED: offset=%.6f "
                         "jitterP50=%.6f jitterMax=%.6f (expected 50.0/1.0/2.0)\n",
                         stats.offsetUs, stats.jitterP50Us, stats.jitterMaxUs);
            std::abort();
        }
    }
};
const JitterMathSelfCheck g_jitterMathSelfCheck;

} // namespace

// state.range(0): scheduling period in microseconds.
static void BM_SchedulerJitter(benchmark::State& state) {
    const auto periodUs = std::chrono::microseconds(state.range(0));
    constexpr size_t kWarmupCycles = 20;
    constexpr size_t kMeasuredCycles = 200;

    for (auto _ : state) {
        axonvex::core::TimingController controller;
        JitterProbeUnit unit("jitter_probe");

        axonvex::core::TimingConstraints constraints;
        constraints.period = periodUs;
        constraints.isRealTime = true;
        controller.scheduleProcessingUnit(&unit, constraints);

        // Warmup cycles: scheduler-thread startup and first-touch costs land
        // here, discarded before the measured region below.
        unit.arm(kWarmupCycles);
        controller.start();
        unit.waitDone();

        unit.arm(kMeasuredCycles);
        unit.waitDone();
        controller.stop();

        const auto timestamps = unit.samples();
        std::vector<double> intervalsUs;
        if (timestamps.size() > 1) {
            intervalsUs.reserve(timestamps.size() - 1);
        }
        for (size_t i = 1; i < timestamps.size(); ++i) {
            intervalsUs.push_back(
                std::chrono::duration<double, std::micro>(timestamps[i] - timestamps[i - 1])
                    .count());
        }
        benchmark::ClobberMemory();

        const IntervalStats stats =
            computeIntervalStats(intervalsUs, static_cast<double>(periodUs.count()));
        // Systematic per-cycle scheduler overhead (median interval vs the
        // requested period) -- NOT jitter. Reported separately so it can't
        // be mistaken for the tail below.
        state.counters["period_offset_us"] = stats.offsetUs;
        state.counters["jitter_p50_us"] = stats.jitterP50Us;
        state.counters["jitter_p95_us"] = stats.jitterP95Us;
        state.counters["jitter_p99_us"] = stats.jitterP99Us;
        state.counters["jitter_max_us"] = stats.jitterMaxUs;
    }
}
// One controller lifecycle per repetition (not per Google-Benchmark
// "iteration" inside a single timed loop): each repetition already runs
// warmup+measured cycles internally and reports its own percentiles via
// counters, so there is nothing to gain from -- and real cost to lose from
// (wall time is real here, not mocked) -- letting the timer-driven loop
// repeat the whole lifecycle many times to fill a min-time window.
BENCHMARK(BM_SchedulerJitter)
    ->Arg(1000)
    ->Arg(5000)
    ->Iterations(1)
    ->Repetitions(5)
    ->Unit(benchmark::kMicrosecond);
