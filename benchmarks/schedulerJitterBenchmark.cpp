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
 * p50/p95/p99/max.
 */

#include <algorithm>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/timingController.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
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
        std::vector<double> jitterUs;
        if (timestamps.size() > 1) {
            jitterUs.reserve(timestamps.size() - 1);
        }
        for (size_t i = 1; i < timestamps.size(); ++i) {
            const double intervalUs =
                std::chrono::duration<double, std::micro>(timestamps[i] - timestamps[i - 1])
                    .count();
            jitterUs.push_back(std::fabs(intervalUs - static_cast<double>(periodUs.count())));
        }
        std::sort(jitterUs.begin(), jitterUs.end());
        benchmark::ClobberMemory();

        state.counters["jitter_p50_us"] = percentile(jitterUs, 0.50);
        state.counters["jitter_p95_us"] = percentile(jitterUs, 0.95);
        state.counters["jitter_p99_us"] = percentile(jitterUs, 0.99);
        state.counters["jitter_max_us"] = jitterUs.empty() ? 0.0 : jitterUs.back();
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
