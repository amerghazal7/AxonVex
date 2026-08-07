/**
 * @file unitFactoryTest.cpp
 * @brief Unit tests for UnitFactory (composer foundations, design spec §3.4).
 */

#include "unitDescriptorHonestyCheck.hpp"

#include <axonvex_core/builtinUnits.hpp>
#include <axonvex_core/unitFactory.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>

namespace axonvex::core::test {
namespace {

UnitTypeDescriptor makeTrivialDescriptor(const std::string& typeName) {
    UnitTypeDescriptor d;
    d.typeName = typeName;
    return d;
}

UnitFactory::CreateFn trivialCreate() {
    return [](const std::string& name, const nlohmann::json&) {
        class Trivial : public ProcessingUnit {
          public:
            explicit Trivial(const std::string& n) : ProcessingUnit(n) {}
            void processSync() override {}
            void processAsync() override {}
            void reset() override {}
            void initialize() override {
                setState(ExecutionState::INITIALIZED);
            }
            std::string getTypeDescription() override {
                return "test.Trivial";
            }
        };
        return std::unique_ptr<ProcessingUnit>(new Trivial(name));
    };
}

TEST(UnitFactoryTest, RegisterAndDescribeRoundTrips) {
    UnitFactory factory;
    factory.registerType(makeTrivialDescriptor("test.A"), trivialCreate());
    EXPECT_TRUE(factory.hasType("test.A"));
    EXPECT_FALSE(factory.hasType("test.B"));
    const UnitTypeDescriptor* d = factory.describe("test.A");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->typeName, "test.A");
    EXPECT_EQ(factory.describe("test.B"), nullptr);
}

TEST(UnitFactoryTest, TypeNamesListsEverythingRegistered) {
    UnitFactory factory;
    factory.registerType(makeTrivialDescriptor("test.A"), trivialCreate());
    factory.registerType(makeTrivialDescriptor("test.B"), trivialCreate());
    auto names = factory.typeNames();
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{"test.A", "test.B"}));
}

TEST(UnitFactoryTest, DuplicateRegistrationIsRefusedLoudly) {
    UnitFactory factory;
    factory.registerType(makeTrivialDescriptor("test.A"), trivialCreate());
    EXPECT_THROW(factory.registerType(makeTrivialDescriptor("test.A"), trivialCreate()),
                 std::logic_error);
    // The original registration must survive the refused attempt.
    EXPECT_TRUE(factory.hasType("test.A"));
}

TEST(UnitFactoryTest, RegisterRejectsNullCreateFn) {
    UnitFactory factory;
    EXPECT_THROW(factory.registerType(makeTrivialDescriptor("test.A"), UnitFactory::CreateFn()),
                 std::invalid_argument);
}

TEST(UnitFactoryTest, CreateUnknownTypeThrows) {
    UnitFactory factory;
    EXPECT_THROW(factory.create("nope", "inst", nlohmann::json::object()), std::invalid_argument);
}

TEST(UnitFactoryTest, CreateBuildsAnInstance) {
    UnitFactory factory;
    factory.registerType(makeTrivialDescriptor("test.A"), trivialCreate());
    auto unit = factory.create("test.A", "myInstance", nlohmann::json::object());
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(unit->getName(), "myInstance");
}

TEST(UnitFactoryTest, CreateValidatesParamsBeforeInvokingCreateFn) {
    UnitFactory factory;
    UnitTypeDescriptor d = makeTrivialDescriptor("test.A");
    d.parameters.push_back({"window", ParamDescriptor::Type::Int, nlohmann::json(), "", {}});

    int createCalls = 0;
    factory.registerType(d, [&](const std::string& name, const nlohmann::json& params) {
        ++createCalls;
        return trivialCreate()(name, params);
    });

    // Missing required "window" -> create() must throw and never call the CreateFn.
    EXPECT_THROW(factory.create("test.A", "inst", nlohmann::json::object()), std::invalid_argument);
    EXPECT_EQ(createCalls, 0);

    auto unit = factory.create("test.A", "inst", nlohmann::json{{"window", 4}});
    ASSERT_NE(unit, nullptr);
    EXPECT_EQ(createCalls, 1);
}

// The CreateFn must run OUTSIDE the factory's internal lock (design spec
// §3.4, the C12/C18 rule applied to new code). Proof: a CreateFn that
// re-enters the SAME factory (factory.hasType(), which locks the same
// mutex_) must not deadlock. A non-recursive std::mutex held across the
// call would hang here forever; bound the wait so a regression fails the
// test instead of wedging the whole ctest run.
TEST(UnitFactoryTest, CreateFnRunsOutsideTheFactoryLock) {
    UnitFactory factory;
    UnitTypeDescriptor d = makeTrivialDescriptor("test.Reentrant");
    factory.registerType(d, [&factory](const std::string& name, const nlohmann::json& params) {
        // Re-enters the factory while "inside" create(). Would deadlock if
        // create() held mutex_ across the CreateFn invocation.
        EXPECT_TRUE(factory.hasType("test.Reentrant"));
        return trivialCreate()(name, params);
    });

    auto future = std::async(std::launch::async, [&factory]() {
        return factory.create("test.Reentrant", "inst", nlohmann::json::object());
    });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "UnitFactory::create() deadlocked — CreateFn is running under the factory lock";
    EXPECT_NE(future.get(), nullptr);
}

// ---------------------------------------------------------------- builtin units

class BuiltinUnitsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        builtin::registerBuiltinUnitTypes(factory);
    }
    UnitFactory factory;
};

TEST_F(BuiltinUnitsTest, AllThreeDemoTypesAreRegisteredWithFullMetadata) {
    for (const std::string& type :
         {"axonvex.SineGenerator", "axonvex.MovingAverage", "axonvex.StatsSink"}) {
        ASSERT_TRUE(factory.hasType(type)) << type;
        const UnitTypeDescriptor* d = factory.describe(type);
        ASSERT_NE(d, nullptr);
        EXPECT_FALSE(d->description.empty()) << type;
    }
}

TEST_F(BuiltinUnitsTest, SineGeneratorDescriptorMatchesInstance) {
    auto unit = factory.create("axonvex.SineGenerator", "gen", nlohmann::json::object());
    auto issues =
        testing::verifyDescriptorMatchesInstance(*factory.describe("axonvex.SineGenerator"), *unit);
    EXPECT_TRUE(issues.empty()) << (issues.empty() ? "" : issues[0]);
}

TEST_F(BuiltinUnitsTest, MovingAverageDescriptorMatchesInstance) {
    auto unit = factory.create("axonvex.MovingAverage", "filt", nlohmann::json{{"window", 4}});
    auto issues =
        testing::verifyDescriptorMatchesInstance(*factory.describe("axonvex.MovingAverage"), *unit);
    EXPECT_TRUE(issues.empty()) << (issues.empty() ? "" : issues[0]);
}

TEST_F(BuiltinUnitsTest, StatsSinkDescriptorMatchesInstance) {
    auto unit = factory.create("axonvex.StatsSink", "sink", nlohmann::json::object());
    auto issues =
        testing::verifyDescriptorMatchesInstance(*factory.describe("axonvex.StatsSink"), *unit);
    EXPECT_TRUE(issues.empty()) << (issues.empty() ? "" : issues[0]);
}

TEST_F(BuiltinUnitsTest, MovingAverageComputesSlidingWindowMean) {
    auto unit = factory.create("axonvex.MovingAverage", "filt", nlohmann::json{{"window", 2}});
    auto* in = unit->getInputPort<double>(0);
    auto* out = unit->getOutputPort<double>(0);
    double lastOut = 0.0;
    out->setOutputCallback([&](const double& v) { lastOut = v; });

    in->writeData(2.0);
    unit->processSync();
    EXPECT_DOUBLE_EQ(lastOut, 2.0);

    in->writeData(4.0);
    unit->processSync();
    EXPECT_DOUBLE_EQ(lastOut, 3.0); // mean(2,4)
}

TEST_F(BuiltinUnitsTest, MovingAverageMissingRequiredWindowThrows) {
    EXPECT_THROW(factory.create("axonvex.MovingAverage", "filt", nlohmann::json::object()),
                 std::invalid_argument);
}

} // namespace
} // namespace axonvex::core::test
