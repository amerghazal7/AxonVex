/**
 * @file builtinUnitsTest.cpp
 * @brief Correctness + RT-path (no heap allocation) coverage for the demo
 *        ProcessingUnit types in builtinUnits.hpp (composer foundations
 *        review fix — see docs/v1_release_plan.md defect inventory, "C-new:
 *        MovingAverage::processSync() heap allocation").
 */

#include <atomic>
#include <axonvex_core/builtinUnits.hpp>
#include <cstdlib>
#include <gtest/gtest.h>

namespace {

// ---------------------------------------------------------------------------
// Global operator new/delete override, gated by an atomic flag, so a test
// can prove a code region performs zero heap allocations. Delegates to
// malloc/free exactly like the default implementation — the only added
// behavior is an atomic increment while tracking is enabled, so this is
// inert (and zero-cost when tracking is off) for every other test in this
// binary.
std::atomic<bool> g_trackAllocs{false};
std::atomic<long> g_allocCount{0};

} // namespace

void* operator new(std::size_t size) {
    if (g_trackAllocs.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(size);
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

namespace axonvex::core::builtin::test {
namespace {

/// Minimal sink used to capture what a unit under test writes to an output
/// port (OutputPort has no direct read-back; only connected InputPorts do).
class CapturePort : public ProcessingUnit {
  public:
    CapturePort() : ProcessingUnit("capture"), in(createInputPort<double>(0, "in")) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    void initialize() override {}
    std::string getTypeDescription() override {
        return "CapturePort";
    }
    InputPort<double>* in;
};

TEST(MovingAverageTest, ComputesSlidingWindowMean) {
    MovingAverage ma("ma", nlohmann::json{{"window", 3}});
    ma.initialize();
    auto* in = ma.getInputPort<double>(0);
    auto* out = ma.getOutputPort<double>(0);
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    CapturePort cap;
    out->connect(cap.in);

    const double values[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    const double expected[] = {1.0, 1.5, 2.0, 3.0, 4.0};
    for (size_t i = 0; i < 5; ++i) {
        in->writeData(values[i]);
        ma.processSync();
        EXPECT_DOUBLE_EQ(cap.in->read(), expected[i]) << "at tick " << i;
    }
}

TEST(MovingAverageTest, ResetClearsWindowState) {
    MovingAverage ma("ma", nlohmann::json{{"window", 2}});
    ma.initialize();
    auto* in = ma.getInputPort<double>(0);
    auto* out = ma.getOutputPort<double>(0);
    CapturePort cap;
    out->connect(cap.in);

    in->writeData(10.0);
    ma.processSync();
    in->writeData(20.0);
    ma.processSync();
    EXPECT_DOUBLE_EQ(cap.in->read(), 15.0);

    ma.reset();
    in->writeData(4.0);
    ma.processSync();
    EXPECT_DOUBLE_EQ(cap.in->read(), 4.0); // window forgot the pre-reset samples
}

// The blocking review finding: processSync() must not heap-allocate on the
// scheduler hot path (CLAUDE.md rule 2). Warm past construction/initialize()
// (which are allowed to allocate once), then assert zero allocations across
// a batch of steady-state ticks, including window-boundary crossings where a
// deque-backed ring would have to grow/shrink a chunk.
TEST(MovingAverageTest, ProcessSyncPerformsNoHeapAllocation) {
    MovingAverage ma("ma", nlohmann::json{{"window", 8}});
    ma.initialize();
    auto* in = ma.getInputPort<double>(0);

    // Warm-up: past this point processSync() must be in steady state.
    for (int i = 0; i < 64; ++i) {
        in->writeData(static_cast<double>(i));
        ma.processSync();
    }

    g_allocCount.store(0, std::memory_order_relaxed);
    g_trackAllocs.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 100000; ++i) {
        in->writeData(static_cast<double>(i));
        ma.processSync();
    }
    g_trackAllocs.store(false, std::memory_order_relaxed);

    EXPECT_EQ(g_allocCount.load(std::memory_order_relaxed), 0);
}

} // namespace
} // namespace axonvex::core::builtin::test
