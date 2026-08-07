/**
 * @file portThroughputBenchmark.cpp
 * @brief Google Benchmark: OutputPort::write() -> connected InputPort::read()
 *        throughput (Phase 3 WS-PERF).
 *
 * Not a latency claim (see pipelineLatencyBenchmark.cpp / estopLatencyBenchmark.cpp
 * for those) -- this measures steady-state items/sec through a single
 * producer/consumer port pair, with and without the port's internal mutex
 * (threadSafe=true/false), single-threaded. A cross-thread producer/consumer
 * variant is deliberately out of scope here: that would be measuring
 * ThreadSafeQueue-style handoff, not the port write/read machinery itself.
 */

#include <axonvex_core/processingUnit.hpp>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <string>

namespace {

// Minimal concrete ProcessingUnit: only exists to own ports (createInputPort/
// createOutputPort are ProcessingUnit members). processSync/processAsync are
// never invoked by this benchmark -- write()/read() are called directly.
class BenchUnit : public axonvex::core::ProcessingUnit {
  public:
    explicit BenchUnit(const std::string& name) : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    void initialize() override {}
    std::string getTypeDescription() override {
        return "BenchUnit";
    }
};

} // namespace

// state.range(0): 0 = threadSafe port (locked), 1 = non-threadSafe (unlocked).
static void BM_PortWriteRead(benchmark::State& state) {
    const bool threadSafe = state.range(0) != 0;
    BenchUnit producer("producer");
    BenchUnit consumer("consumer");
    auto* out = producer.createOutputPort<int64_t>(0, "out", threadSafe);
    auto* in = consumer.createInputPort<int64_t>(0, "in", threadSafe);
    out->connect(in);

    // Warmup: first-touch page faults / cache fill for the port's internal
    // buffers happen here, outside the measured region.
    for (int i = 0; i < 100; ++i) {
        out->write(static_cast<int64_t>(i));
        benchmark::DoNotOptimize(in->read());
    }

    int64_t counter = 0;
    for (auto _ : state) {
        out->write(counter);
        benchmark::ClobberMemory();
        int64_t got = in->read();
        benchmark::DoNotOptimize(got);
        ++counter;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_PortWriteRead)->Arg(0)->Arg(1)->Unit(benchmark::kNanosecond);
