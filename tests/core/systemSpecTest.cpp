/**
 * @file systemSpecTest.cpp
 * @brief Unit tests for SystemSpec v1 (.axv.json) parse/validate (design spec §2, §4.1-4.2 slice).
 */

#include <axonvex_core/builtinUnits.hpp>
#include <axonvex_core/systemSpec.hpp>
#include <axonvex_core/utils/serialization/jsonSerializer.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace axonvex::core::test {
namespace {

using nlohmann::json;

bool errorsContain(const std::vector<SpecError>& errors, const std::string& code) {
    for (const auto& e : errors) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

UnitTypeDescriptor describeWithPorts(const std::string& typeName, std::vector<PortDescriptor> ports,
                                     std::vector<ParamDescriptor> params = {}) {
    UnitTypeDescriptor d;
    d.typeName = typeName;
    d.ports = std::move(ports);
    d.parameters = std::move(params);
    return d;
}

UnitFactory::CreateFn countingCreate(int* counter) {
    return [counter](const std::string& name, const json&) {
        class Counted : public ProcessingUnit {
          public:
            explicit Counted(const std::string& n) : ProcessingUnit(n) {}
            void processSync() override {}
            void processAsync() override {}
            void reset() override {}
            void initialize() override {
                setState(ExecutionState::INITIALIZED);
            }
            std::string getTypeDescription() override {
                return "test.Counted";
            }
        };
        if (counter) {
            ++(*counter);
        }
        return std::unique_ptr<ProcessingUnit>(new Counted(name));
    };
}

/// A factory pre-loaded with a handful of small reference types covering
/// every direction/kind/dataType combination validate() needs to check.
class SpecTestFactory : public ::testing::Test {
  protected:
    void SetUp() override {
        factory.registerType(
            describeWithPorts("test.DoubleSource", {PortDescriptor::make<double>(
                                                       0, "out", PortDescriptor::Direction::Output,
                                                       PortDescriptor::Kind::Sync)}),
            countingCreate(&createCalls));
        factory.registerType(describeWithPorts("test.DoubleSink",
                                               {PortDescriptor::make<double>(
                                                   0, "in", PortDescriptor::Direction::Input,
                                                   PortDescriptor::Kind::Sync, /*required=*/true)}),
                             countingCreate(&createCalls));
        factory.registerType(
            describeWithPorts("test.BoolSink", {PortDescriptor::make<bool>(
                                                   0, "in", PortDescriptor::Direction::Input,
                                                   PortDescriptor::Kind::Sync, /*required=*/true)}),
            countingCreate(&createCalls));
        factory.registerType(
            describeWithPorts(
                "test.AsyncDoubleSink",
                {PortDescriptor::make<double>(0, "in", PortDescriptor::Direction::Input,
                                              PortDescriptor::Kind::Async, /*required=*/true)}),
            countingCreate(&createCalls));
        factory.registerType(
            describeWithPorts("test.Configurable", {},
                              {ParamDescriptor{"window", ParamDescriptor::Type::Int, json(),
                                               "window", json{{"min", 1}}}}),
            countingCreate(&createCalls));
    }

    UnitFactory factory;
    int createCalls{0};
};

json minimalValidDoc() {
    return json{{"specVersion", "1.0"},
                {"system", {{"name", "t"}}},
                {"units", json::array({{{"name", "u1"}, {"type", "test.DoubleSource"}}})}};
}

// ---------------------------------------------------------------- parse()

TEST(SystemSpecParseTest, MinimalValidDocParses) {
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(minimalValidDoc(), errors);
    EXPECT_TRUE(spec.has_value());
    EXPECT_TRUE(errors.empty());
}

TEST(SystemSpecParseTest, NonObjectRootIsRejected) {
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(json::array(), errors);
    EXPECT_FALSE(spec.has_value());
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].code, SpecErrorCode::INVALID_TYPE);
}

TEST(SystemSpecParseTest, UnknownTopLevelKeyIsRejected) {
    json doc = minimalValidDoc();
    doc["totallyUnknown"] = 1;
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::UNKNOWN_KEY));
}

TEST(SystemSpecParseTest, MissingSpecVersionIsRejected) {
    json doc = minimalValidDoc();
    doc.erase("specVersion");
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::MISSING_FIELD));
}

TEST(SystemSpecParseTest, UnsupportedSpecVersionIsRejected) {
    json doc = minimalValidDoc();
    doc["specVersion"] = "2.0";
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::SPEC_VERSION_UNSUPPORTED));
}

TEST(SystemSpecParseTest, DuplicateUnitNameIsRejected) {
    json doc = minimalValidDoc();
    doc["units"].push_back(json{{"name", "u1"}, {"type", "test.DoubleSource"}});
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::DUP_UNIT_NAME));
}

TEST(SystemSpecParseTest, EmptyUnitsArrayIsRejected) {
    json doc = minimalValidDoc();
    doc["units"] = json::array();
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::PARAM_CONSTRAINT));
}

TEST(SystemSpecParseTest, MalformedPortReferenceIsRejected) {
    json doc = minimalValidDoc();
    doc["connections"] = json::array({{{"from", "no-dot-here"}, {"to", "u1.out"}}});
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::BAD_REFERENCE));
}

TEST(SystemSpecParseTest, InvalidSchedulingPolicyEnumIsRejected) {
    json doc = minimalValidDoc();
    doc["system"]["schedulingPolicy"] = "NOT_A_POLICY";
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::INVALID_ENUM_VALUE));
}

TEST(SystemSpecParseTest, CustomSchedulingPolicyIsRejected) {
    // CUSTOM is a flagged-for-deletion RT-path violation (design spec Q6);
    // the spec grammar never accepted it.
    json doc = minimalValidDoc();
    doc["system"]["schedulingPolicy"] = "CUSTOM";
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::INVALID_ENUM_VALUE));
}

TEST(SystemSpecParseTest, MissionPipelinesShapeIsAcceptedAndParsed) {
    json doc = minimalValidDoc();
    doc["missionPipelines"] = json::array(
        {{{"name", "mp1"},
          {"elements", json::array({{{"name", "e1"}, {"type", "t"}}})},
          {"transitions", json::array({{{"from", "e1"}, {"to", "e1"}, {"on", "success"}}})}}});
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    ASSERT_TRUE(spec.has_value());
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(spec->missionPipelines.size(), 1u);
    EXPECT_EQ(spec->missionPipelines[0].name, "mp1");
    ASSERT_EQ(spec->missionPipelines[0].transitions.size(), 1u);
    EXPECT_EQ(spec->missionPipelines[0].transitions[0].on, "success");
}

TEST(SystemSpecParseTest, MissionPipelineBadTransitionOnValueIsRejected) {
    json doc = minimalValidDoc();
    doc["missionPipelines"] = json::array(
        {{{"name", "mp1"},
          {"transitions", json::array({{{"from", "e1"}, {"to", "e1"}, {"on", "sideways"}}})}}});
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    EXPECT_FALSE(spec.has_value());
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::INVALID_ENUM_VALUE));
}

TEST(SystemSpecParseTest, SystemConfigurationMapsParsedFields) {
    json doc = minimalValidDoc();
    doc["system"]["tickRateUs"] = 500;
    doc["system"]["logLevel"] = "Warning";
    std::vector<SpecError> errors;
    auto spec = SystemSpec::parse(doc, errors);
    ASSERT_TRUE(spec.has_value());
    SystemConfiguration cfg = spec->systemConfiguration();
    EXPECT_EQ(cfg.systemName, "t");
    EXPECT_EQ(cfg.systemTickRate, std::chrono::microseconds(500));
    EXPECT_EQ(cfg.logLevel, LogLevel::Warning);
}

// ---------------------------------------------------------------- validate()

TEST_F(SpecTestFactory, UnknownUnitTypeIsReported) {
    json doc = minimalValidDoc();
    doc["units"][0]["type"] = "test.DoesNotExist";
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::UNKNOWN_UNIT_TYPE));
    EXPECT_EQ(createCalls, 0); // validate() never instantiates.
}

TEST_F(SpecTestFactory, ValidateNeverInstantiatesEvenOnSuccess) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.DoubleSource"}},
                                           {{"name", "dst"}, {"type", "test.DoubleSink"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "dst.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);

    std::vector<SpecError> errors;
    EXPECT_TRUE(spec->validate(factory, errors)) << (errors.empty() ? "" : errors[0].message);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(createCalls, 0);
}

TEST_F(SpecTestFactory, PortTypeMismatchIsReported) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.DoubleSource"}},
                                           {{"name", "dst"}, {"type", "test.BoolSink"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "dst.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::PORT_TYPE_MISMATCH));
}

TEST_F(SpecTestFactory, KindMismatchIsReported) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.DoubleSource"}},
                                           {{"name", "dst"}, {"type", "test.AsyncDoubleSink"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "dst.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::KIND_MISMATCH));
}

TEST_F(SpecTestFactory, RequiredInputUnwiredIsReported) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "dst"}, {"type", "test.DoubleSink"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::REQUIRED_INPUT_UNWIRED));
}

TEST_F(SpecTestFactory, RequiredInputWiredViaSystemPortsSatisfiesTheCheck) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "dst"}, {"type", "test.DoubleSink"}}})},
                    {"systemPorts", {{"inputs", {{"cmd", "dst.in"}}}}}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_TRUE(spec->validate(factory, errors)) << (errors.empty() ? "" : errors[0].message);
}

TEST_F(SpecTestFactory, TwoConnectionsIntoSameSyncInputAreRejected) {
    // Regression: connection state lives only on the OutputPort side
    // (ports.hpp), so nothing at runtime detects a second writer into the
    // same InputPort — the earlier writer's data is silently clobbered every
    // tick. validate() is the only place this can be caught statically.
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src1"}, {"type", "test.DoubleSource"}},
                                           {{"name", "src2"}, {"type", "test.DoubleSource"}},
                                           {{"name", "dst"}, {"type", "test.DoubleSink"}}})},
                    {"connections", json::array({{{"from", "src1.out"}, {"to", "dst.in"}},
                                                 {{"from", "src2.out"}, {"to", "dst.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::DUP_INPUT_WIRE));
}

TEST_F(SpecTestFactory, ConnectionAndSystemPortIntoSameInputAreRejected) {
    // Same defect, reached via the systemPorts.inputs path instead of a
    // second connection entry.
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.DoubleSource"}},
                                           {{"name", "dst"}, {"type", "test.DoubleSink"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "dst.in"}}})},
                    {"systemPorts", {{"inputs", {{"cmd", "dst.in"}}}}}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::DUP_INPUT_WIRE));
}

TEST_F(SpecTestFactory, BadConnectionReferenceToUnknownUnitIsReported) {
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.DoubleSource"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "ghost.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::BAD_REFERENCE));
}

TEST_F(SpecTestFactory, ParamConstraintViolationIsReported) {
    json doc =
        json{{"specVersion", "1.0"},
             {"system", {{"name", "t"}}},
             {"units",
              json::array(
                  {{{"name", "u1"}, {"type", "test.Configurable"}, {"params", {{"window", 0}}}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::PARAM_CONSTRAINT));
}

TEST_F(SpecTestFactory, MalformedAdapterUriIsReported) {
    json doc = minimalValidDoc();
    doc["adapters"] = json::array({{{"uri", "not-a-uri"}, {"type", "ros2"}}});
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value());

    std::vector<SpecError> errors;
    EXPECT_FALSE(spec->validate(factory, errors));
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::BAD_REFERENCE));
}

// ---------------------------------------------------------------- end-to-end example doc

TEST(SystemSpecExampleDocTest, ParsesAndValidatesAgainstBuiltinUnits) {
#ifndef AXONVEX_COMPOSER_EXAMPLE_SPEC_PATH
    GTEST_SKIP() << "AXONVEX_COMPOSER_EXAMPLE_SPEC_PATH not defined by the build";
#else
    json doc =
        axonvex::utils::serialization::JSONSerializer::fromFile(AXONVEX_COMPOSER_EXAMPLE_SPEC_PATH);

    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);
    EXPECT_TRUE(parseErrors.empty());

    UnitFactory factory;
    builtin::registerBuiltinUnitTypes(factory);

    std::vector<SpecError> validateErrors;
    EXPECT_TRUE(spec->validate(factory, validateErrors))
        << (validateErrors.empty() ? "" : validateErrors[0].message);
    EXPECT_TRUE(validateErrors.empty());
#endif
}

// ---------------------------------------------------------------- loadSystemFromSpec() /
// SpecSystem

TEST(SystemSpecLoaderTest, ParseFailureReturnsNullWithoutConstructing) {
    UnitFactory factory;
    std::vector<SpecError> errors;
    auto system = loadSystemFromSpec(json::array(), errors, factory); // non-object root
    EXPECT_EQ(system, nullptr);
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::INVALID_TYPE));
}

TEST_F(SpecTestFactory, ValidateFailureReturnsNullWithoutInstantiating) {
    json doc = minimalValidDoc();
    doc["units"][0]["type"] = "test.DoesNotExist";
    std::vector<SpecError> errors;
    auto system = loadSystemFromSpec(doc, errors, factory);
    EXPECT_EQ(system, nullptr);
    EXPECT_TRUE(errorsContain(errors, SpecErrorCode::UNKNOWN_UNIT_TYPE));
    EXPECT_EQ(createCalls, 0); // loadSystemFromSpec must not instantiate on a validate() failure.
}

// The Phase 2 exit criterion (plan §11 / design spec §4.1): a spec-driven
// system boots, runs, and produces data with ZERO subclass code -- SpecSystem
// is used directly here, never subclassed.
TEST(SystemSpecLoaderTest, BootsRunsAndProducesDataWithZeroSubclassCode) {
    json doc = json{
        {"specVersion", "1.0"},
        {"system", {{"name", "spec-demo"}, {"tickRateUs", 1000}}},
        {"units",
         json::array(
             {{{"name", "gen"},
               {"type", "axonvex.SineGenerator"},
               {"params", {{"frequencyHz", 10.0}, {"amplitude", 1.0}}}},
              {{"name", "filt"}, {"type", "axonvex.MovingAverage"}, {"params", {{"window", 8}}}},
              {{"name", "sink"}, {"type", "axonvex.StatsSink"}, {"params", json::object()}}})},
        {"connections", json::array({{{"from", "gen.out"}, {"to", "filt.in"}},
                                     {{"from", "filt.out"}, {"to", "sink.in"}}})},
        {"systemPorts", {{"outputs", {{"filtered", "filt.out"}}}}}};

    UnitFactory factory;
    builtin::registerBuiltinUnitTypes(factory);

    std::vector<SpecError> errors;
    auto system = loadSystemFromSpec(doc, errors, factory);
    ASSERT_NE(system, nullptr) << (errors.empty() ? "" : errors[0].message);
    EXPECT_TRUE(errors.empty());

    ASSERT_TRUE(system->initialize());
    EXPECT_NE(system->getSystemOutputPort("filtered"), nullptr);

    builtin::StatsSink* sink = nullptr;
    for (ProcessingUnit* u : system->getAllProcessingUnits()) {
        if (u->getName() == "sink") {
            sink = dynamic_cast<builtin::StatsSink*>(u);
        }
    }
    ASSERT_NE(sink, nullptr);

    ASSERT_TRUE(system->start());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (sink->sampleCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GT(sink->sampleCount(), 0u);

    EXPECT_TRUE(system->stop());
}

TEST_F(SpecTestFactory, MissingAdapterFailsInitializeAndRecordsError) {
    // validate() only checks URI well-formedness (SpecErrorCode::BAD_REFERENCE);
    // whether an adapter instance was actually injected via addAdapter() before
    // initialize() is a runtime-only concern (design spec §4.2 step 3), so this
    // is not caught until SpecSystem::initializeBlocksLayout() actually runs.
    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "u1"}, {"type", "test.DoubleSource"}}})},
                    {"adapters", json::array({{{"uri", "ros2://foo"}, {"type", "ros2"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);
    std::vector<SpecError> validateErrors;
    ASSERT_TRUE(spec->validate(factory, validateErrors))
        << (validateErrors.empty() ? "" : validateErrors[0].message);

    SpecSystem system(std::move(*spec), factory);
    EXPECT_FALSE(system.initialize());
    EXPECT_TRUE(errorsContain(system.getLastSpecErrors(), SpecErrorCode::MISSING_ADAPTER));
}

TEST_F(SpecTestFactory, MissionPipelinesSectionIsRejectedAsUnsupportedByTheLoader) {
    // Reserved-but-rejected (design spec §7 Q7): validate() only shape-checks
    // this section (no MissionElement factories exist yet), so it is the
    // loader's job to refuse it explicitly rather than silently ignore it.
    json doc = minimalValidDoc();
    doc["missionPipelines"] = json::array({{{"name", "mp1"}}});
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);
    std::vector<SpecError> validateErrors;
    ASSERT_TRUE(spec->validate(factory, validateErrors))
        << (validateErrors.empty() ? "" : validateErrors[0].message);

    SpecSystem system(std::move(*spec), factory);
    EXPECT_FALSE(system.initialize());
    EXPECT_TRUE(errorsContain(system.getLastSpecErrors(), SpecErrorCode::UNSUPPORTED_SECTION));
}

TEST_F(SpecTestFactory, WiringDefectAfterValidationPassesIsFatal) {
    // Regression for design spec §4.2 step 4: tryConnect() returning false
    // here is a DEFECT (validate() already reported the two ports as
    // compatible "double" Sync ports going by their descriptors) -- e.g. a
    // unit type whose registered UnitTypeDescriptor lies about its real port
    // template type. This must fail loudly (initialize() -> false, a recorded
    // SpecError), never silently skip the connection.
    class LiarSource : public ProcessingUnit {
      public:
        explicit LiarSource(const std::string& name) : ProcessingUnit(name) {
            createOutputPort<int>(0, "out"); // descriptor below lies: claims double.
        }
        void processSync() override {}
        void processAsync() override {}
        void reset() override {}
        void initialize() override {
            setState(ExecutionState::INITIALIZED);
        }
        std::string getTypeDescription() override {
            return "test.LiarSource";
        }
    };
    factory.registerType(
        describeWithPorts("test.LiarSource",
                          {PortDescriptor::make<double>(0, "out", PortDescriptor::Direction::Output,
                                                        PortDescriptor::Kind::Sync)}),
        [](const std::string& name, const json&) {
            return std::unique_ptr<ProcessingUnit>(new LiarSource(name));
        });

    json doc = json{{"specVersion", "1.0"},
                    {"system", {{"name", "t"}}},
                    {"units", json::array({{{"name", "src"}, {"type", "test.LiarSource"}},
                                           {{"name", "dst"}, {"type", "test.DoubleSink"}}})},
                    {"connections", json::array({{{"from", "src.out"}, {"to", "dst.in"}}})}};
    std::vector<SpecError> parseErrors;
    auto spec = SystemSpec::parse(doc, parseErrors);
    ASSERT_TRUE(spec.has_value()) << (parseErrors.empty() ? "" : parseErrors[0].message);
    std::vector<SpecError> validateErrors;
    ASSERT_TRUE(spec->validate(factory, validateErrors))
        << (validateErrors.empty() ? "" : validateErrors[0].message);

    SpecSystem system(std::move(*spec), factory);
    EXPECT_FALSE(system.initialize());
    EXPECT_FALSE(system.getLastSpecErrors().empty());
}

} // namespace
} // namespace axonvex::core::test
