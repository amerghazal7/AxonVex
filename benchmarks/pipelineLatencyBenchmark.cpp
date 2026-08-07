/**
 * @file pipelineLatencyBenchmark.cpp
 * @brief Google Benchmark: end-to-end latency through a chain of
 *        ProcessingUnits connected by sync ports (Phase 3 WS-PERF).
 *
 * Deliberately does NOT go through TimingController: that thread hop and its
 * own jitter is schedulerJitterBenchmark.cpp's territory. This isolates the
 * pipeline's own cost -- port read/write plus processSyncBase() virtual
 * dispatch per stage -- for a single-threaded, directly-driven pass.
 *
 * Latency is a distribution, not a mean (a mean here would hide a p99 tail
 * that matters to a real-time budget), so this reports p50/p95/p99/max via
 * manual percentile computation rather than Google Benchmark's default
 * mean/median/stddev.
 */

#include <algorithm>
#include <axonvex_core/processingUnit.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

// One pipeline stage: forwards whatever int64_t is on its input port to its
// output port, unchanged. No async ports are created, so processAsync() is
// never called by anything in this file.
class StageUnit : public axonvex::core::ProcessingUnit {
  public:
    explicit StageUnit(const std::string& name) : ProcessingUnit(name) {
        in_ = createInputPort<int64_t>(0, "in");
        out_ = createOutputPort<int64_t>(0, "out");
    }
    void processSync() override {
        out_->write(in_->read());
    }
    void processAsync() override {}
    void reset() override {}
    void initialize() override {}
    std::string getTypeDescription() override {
        return "StageUnit";
    }

    axonvex::core::InputPort<int64_t>* in_{nullptr};
    axonvex::core::OutputPort<int64_t>* out_{nullptr};
};

double percentile(const std::vector<double>& sortedAscending, double p) {
    if (sortedAscending.empty()) {
        return 0.0;
    }
    const size_t idx = static_cast<size_t>(p * static_cast<double>(sortedAscending.size() - 1));
    return sortedAscending[idx];
}

void runOnePass(std::vector<std::unique_ptr<StageUnit>>& stages, int64_t value) {
    stages.front()->in_->writeData(value);
    for (auto& stage : stages) {
        stage->processSyncBase();
    }
}

} // namespace

// state.range(0): number of chained stages.
static void BM_PipelineLatency(benchmark::State& state) {
    const int stageCount = static_cast<int>(state.range(0));

    std::vector<std::unique_ptr<StageUnit>> stages;
    stages.reserve(static_cast<size_t>(stageCount));
    for (int i = 0; i < stageCount; ++i) {
        stages.push_back(std::make_unique<StageUnit>("stage" + std::to_string(i)));
    }
    for (int i = 0; i + 1 < stageCount; ++i) {
        stages[static_cast<size_t>(i)]->out_->connect(stages[static_cast<size_t>(i + 1)]->in_);
    }

    constexpr int kWarmupPasses = 50;
    for (int i = 0; i < kWarmupPasses; ++i) {
        runOnePass(stages, i);
    }

    std::vector<double> latenciesUs;
    latenciesUs.reserve(static_cast<size_t>(state.max_iterations));
    int64_t value = 0;
    for (auto _ : state) {
        auto t0 = std::chrono::steady_clock::now();
        runOnePass(stages, value);
        benchmark::ClobberMemory();
        auto t1 = std::chrono::steady_clock::now();
        latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        ++value;
    }

    std::vector<double> sorted = latenciesUs;
    std::sort(sorted.begin(), sorted.end());
    state.counters["latency_p50_us"] = percentile(sorted, 0.50);
    state.counters["latency_p95_us"] = percentile(sorted, 0.95);
    state.counters["latency_p99_us"] = percentile(sorted, 0.99);
    state.counters["latency_max_us"] = sorted.empty() ? 0.0 : sorted.back();
}
BENCHMARK(BM_PipelineLatency)->Arg(2)->Arg(5)->Arg(10)->Unit(benchmark::kMicrosecond);
