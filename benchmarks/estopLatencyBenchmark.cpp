/**
 * @file estopLatencyBenchmark.cpp
 * @brief Google Benchmark: SafetyManager e-stop notification latency
 *        (Phase 3 WS-PERF).
 *
 * SafetyManager::triggerEmergencyStop() dispatches to registered handlers
 * synchronously, in the caller's thread (see safetyManager.hpp's dispatch(),
 * called directly from triggerEmergencyStop() -- no queue hop to a worker
 * thread on this path). So the wall time of the call itself is the
 * notification latency a real handler (e.g. a motor cutoff) would observe.
 * Reported as percentiles, not a mean: an RT safety budget cares about the
 * tail, not the average.
 */

#include <algorithm>
#include <axonvex_core/callback.hpp>
#include <axonvex_safety/safetyManager.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <vector>

namespace {

class NoopHandler : public axonvex::core::Callback<axonvex::safety::SafetyEvent> {
  public:
    void callbackPerform(const axonvex::safety::SafetyEvent event) override {
        // Touch the event so the compiler can't prove the handler is dead
        // code and elide the dispatch this benchmark is measuring. Copy to a
        // non-const local first -- DoNotOptimize(const T&) is deprecated
        // precisely because it can be optimized away, which would defeat the
        // guard this line exists to provide.
        axonvex::safety::SafetyLevel level = event.level;
        benchmark::DoNotOptimize(level);
    }
};

double percentile(const std::vector<double>& sortedAscending, double p) {
    if (sortedAscending.empty()) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(p * static_cast<double>(sortedAscending.size() - 1));
    return sortedAscending[idx];
}

} // namespace

static void BM_EstopLatency(benchmark::State& state) {
    axonvex::safety::SafetyManager manager;
    NoopHandler handler;
    manager.registerHandler(&handler);

    // Warmup: not started as a worker thread (start() isn't called -- the
    // evaluation loop is irrelevant here), just re-arm/trigger to settle any
    // first-call cache effects before the measured region.
    constexpr int kWarmupCalls = 50;
    for (int i = 0; i < kWarmupCalls; ++i) {
        manager.triggerEmergencyStop("warmup");
        manager.resetEmergencyStop();
    }

    std::vector<double> latenciesUs;
    latenciesUs.reserve(static_cast<size_t>(state.max_iterations));
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        manager.triggerEmergencyStop("benchmark");
        benchmark::ClobberMemory();
        auto t1 = std::chrono::steady_clock::now();
        latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        manager.resetEmergencyStop();
    }

    std::vector<double> sorted = latenciesUs;
    std::sort(sorted.begin(), sorted.end());
    state.counters["estop_p50_us"] = percentile(sorted, 0.50);
    state.counters["estop_p95_us"] = percentile(sorted, 0.95);
    state.counters["estop_p99_us"] = percentile(sorted, 0.99);
    state.counters["estop_max_us"] = sorted.empty() ? 0.0 : sorted.back();
}
BENCHMARK(BM_EstopLatency)->Unit(benchmark::kMicrosecond);
