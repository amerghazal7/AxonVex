/**
 * @file builtinUnitsTest.cpp
 * @brief Correctness + RT-path (no heap allocation) coverage for the demo
 *        ProcessingUnit types in builtinUnits.hpp (composer foundations
 *        review fix — see docs/v1_release_plan.md defect inventory, "C-new:
 *        MovingAverage::processSync() heap allocation").
 */

#include <axonvex_core/builtinUnits.hpp>
#include <gtest/gtest.h>

namespace axonvex::core::builtin {

// C50: this used to be a process-wide `operator new`/`operator delete`
// override (gated by an atomic flag) shared with timingControllerTest.cpp.
// That gave one test file's allocation probe the entire test_core process
// as blast radius -- a single missing overload (the nothrow form) took down
// ~all 606 tests under ASan with Conan-provided GTest, invisible with
// system GTest (see docs/v1_release_plan.md C50 for the full incident).
// Replacement: samples_ is a std::vector sized once at construction and
// never grown/shrunk by processSync() (plain index writes only -- see its
// definition and the member's declaration comment), so its capacity() is
// stable *if and only if* no allocation happened (capacity is monotonic --
// see the accessor's declaration comment in builtinUnits.hpp for why this
// beats comparing buffer pointers). Scoped to the one MovingAverage
// instance the test constructs; it cannot observe or affect any other test
// in the binary.
class MovingAverageTestAccessor {
  public:
    static size_t samplesCapacity(const MovingAverage& ma) {
        return ma.samples_.capacity();
    }
};

namespace test {
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
// (which are allowed to allocate once), then assert samples_'s capacity()
// is unchanged across a batch of steady-state ticks, including
// window-boundary crossings where a deque-backed ring would have had to
// grow/shrink a chunk. capacity() only increases when a (re)allocation
// grows the vector and never decreases on its own, so "unchanged" here is
// exactly "processSync() performed zero heap operations against samples_"
// -- see MovingAverageTestAccessor's comment for why this replaced a
// process-wide operator-new override (C50).
TEST(MovingAverageTest, ProcessSyncPerformsNoHeapAllocation) {
    MovingAverage ma("ma", nlohmann::json{{"window", 8}});
    ma.initialize();
    auto* in = ma.getInputPort<double>(0);

    // Warm-up: past this point processSync() must be in steady state.
    for (int i = 0; i < 64; ++i) {
        in->writeData(static_cast<double>(i));
        ma.processSync();
    }

    size_t capacityAfterWarmup = MovingAverageTestAccessor::samplesCapacity(ma);
    ASSERT_GT(capacityAfterWarmup, 0u);

    for (int i = 0; i < 100000; ++i) {
        in->writeData(static_cast<double>(i));
        ma.processSync();
    }

    EXPECT_EQ(MovingAverageTestAccessor::samplesCapacity(ma), capacityAfterWarmup)
        << "MovingAverage::processSync() grew its sample buffer's capacity -- it heap-allocated "
           "on the scheduler hot path";
}

} // namespace
} // namespace test
} // namespace axonvex::core::builtin
