/**
 * @file systemPortRegistryTest.cpp
 * @brief Unit tests for SystemPortRegistry (Phase 2 core decomposition, step 1)
 *
 * Pins the behavior extracted verbatim from AxonVexSystem's
 * systemInputPorts_/systemOutputPorts_/systemPortsMutex_/lockSystemPortsWith
 * (see docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.7). AxonVexSystemTest's SystemPort* tests continue to pin the
 * façade-level behavior; these tests pin the registry in isolation.
 */

#include <atomic>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/systemPortRegistry.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

class FixturePU : public ProcessingUnit {
  public:
    explicit FixturePU(const std::string& name) : ProcessingUnit(name) {
        out_ = createOutputPort<double>(1000, "out");
        in_ = createInputPort<double>(1001, "in");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }
    void processSync() override {}
    void processAsync() override {}
    void reset() override {
        setState(ExecutionState::INITIALIZED);
    }
    void finalize() override {}
    std::string getTypeDescription() override {
        return "FixturePU";
    }

    OutputPort<double>* out_;
    InputPort<double>* in_;
};

} // namespace

class SystemPortRegistryTest : public ::testing::Test {
  protected:
    SystemPortRegistry registry_;
    FixturePU unit_{"unit1"};
};

TEST_F(SystemPortRegistryTest, AssignAndLookup) {
    EXPECT_TRUE(registry_.assignInput("sys_in", unit_.in_));
    EXPECT_TRUE(registry_.assignOutput("sys_out", unit_.out_));

    EXPECT_TRUE(registry_.hasInput("sys_in"));
    EXPECT_TRUE(registry_.hasOutput("sys_out"));
    EXPECT_FALSE(registry_.hasInput("nonexistent"));

    EXPECT_EQ(registry_.input("sys_in"), unit_.in_);
    EXPECT_EQ(registry_.output("sys_out"), unit_.out_);
    EXPECT_EQ(registry_.input("nonexistent"), nullptr);
}

TEST_F(SystemPortRegistryTest, RefusesDuplicateName) {
    EXPECT_TRUE(registry_.assignInput("dup", unit_.in_));
    EXPECT_FALSE(registry_.assignInput("dup", unit_.in_));

    EXPECT_TRUE(registry_.assignOutput("dupOut", unit_.out_));
    EXPECT_FALSE(registry_.assignOutput("dupOut", unit_.out_));
}

TEST_F(SystemPortRegistryTest, RemoveInputAndOutput) {
    registry_.assignInput("in1", unit_.in_);
    registry_.assignOutput("out1", unit_.out_);

    EXPECT_TRUE(registry_.removeInput("in1"));
    EXPECT_FALSE(registry_.hasInput("in1"));
    EXPECT_FALSE(registry_.removeInput("in1")); // already gone

    EXPECT_TRUE(registry_.removeOutput("out1"));
    EXPECT_FALSE(registry_.hasOutput("out1"));
    EXPECT_FALSE(registry_.removeOutput("nonexistent"));
}

TEST_F(SystemPortRegistryTest, NamesAndCounts) {
    registry_.assignInput("a", unit_.in_);
    registry_.assignOutput("b", unit_.out_);

    EXPECT_EQ(registry_.inputCount(), 1u);
    EXPECT_EQ(registry_.outputCount(), 1u);

    auto inNames = registry_.inputNames();
    ASSERT_EQ(inNames.size(), 1u);
    EXPECT_EQ(inNames[0], "a");

    auto outNames = registry_.outputNames();
    ASSERT_EQ(outNames.size(), 1u);
    EXPECT_EQ(outNames[0], "b");
}

TEST_F(SystemPortRegistryTest, Clear) {
    registry_.assignInput("a", unit_.in_);
    registry_.assignOutput("b", unit_.out_);
    registry_.clear();
    EXPECT_EQ(registry_.inputCount(), 0u);
    EXPECT_EQ(registry_.outputCount(), 0u);
}

TEST_F(SystemPortRegistryTest, DescribeInputsAndOutputsReportNameTypeAndOwner) {
    registry_.assignInput("sys_in", unit_.in_);
    registry_.assignOutput("sys_out", unit_.out_);

    auto inputs = registry_.describeInputs();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs[0].name, "sys_in");
    EXPECT_EQ(inputs[0].dataTypeName, unit_.in_->getDataTypeName());
    EXPECT_EQ(inputs[0].ownerName, "unit1");

    auto outputs = registry_.describeOutputs();
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0].name, "sys_out");
    EXPECT_EQ(outputs[0].dataTypeName, unit_.out_->getDataTypeName());
    EXPECT_EQ(outputs[0].ownerName, "unit1");
}

TEST_F(SystemPortRegistryTest, RemoveAllForOwnerRemovesOnlyThatOwnersPorts) {
    FixturePU other("unit2");
    registry_.assignInput("unit1_in", unit_.in_);
    registry_.assignOutput("unit1_out", unit_.out_);
    registry_.assignInput("unit2_in", other.in_);
    registry_.assignOutput("unit2_out", other.out_);

    auto removed = registry_.removeAllForOwner(&unit_);

    ASSERT_EQ(removed.inputs.size(), 1u);
    EXPECT_EQ(removed.inputs[0], "unit1_in");
    ASSERT_EQ(removed.outputs.size(), 1u);
    EXPECT_EQ(removed.outputs[0], "unit1_out");

    EXPECT_FALSE(registry_.hasInput("unit1_in"));
    EXPECT_FALSE(registry_.hasOutput("unit1_out"));
    EXPECT_TRUE(registry_.hasInput("unit2_in"));
    EXPECT_TRUE(registry_.hasOutput("unit2_out"));
}

// C9 regression, in isolation: locking two registries in opposing order from
// two threads must not deadlock (std::lock inside lockWith), and self-lock
// (target == this) must lock its own mutex exactly once.
TEST_F(SystemPortRegistryTest, LockWithDoesNotDeadlockOnOpposingOrderOrSelf) {
    SystemPortRegistry other;

    {
        auto locks = registry_.lockWith(registry_); // self case: single lock
        SUCCEED();
    }

    std::atomic<bool> aDone{false};
    std::atomic<bool> bDone{false};
    std::thread t1([&] {
        for (int i = 0; i < 200; ++i) {
            auto locks = registry_.lockWith(other);
        }
        aDone.store(true);
    });
    std::thread t2([&] {
        for (int i = 0; i < 200; ++i) {
            auto locks = other.lockWith(registry_);
        }
        bDone.store(true);
    });

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!(aDone.load() && bDone.load()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!aDone.load() || !bDone.load()) {
        // Wedged: detach rather than join so a real regression fails this
        // test instead of hanging the whole ctest run (see
        // AxonVexSystemTest.OpposingCrossSystemConnectsDoNotDeadlock for the
        // same pattern).
        t1.detach();
        t2.detach();
        FAIL() << "lockWith deadlocked (a->b: " << aDone.load() << ", b->a: " << bDone.load()
               << ")";
        return;
    }
    t1.join();
    t2.join();
}

TEST_F(SystemPortRegistryTest, NonCopyableNonMovable) {
    EXPECT_FALSE(std::is_copy_constructible<SystemPortRegistry>::value);
    EXPECT_FALSE(std::is_move_constructible<SystemPortRegistry>::value);
}
