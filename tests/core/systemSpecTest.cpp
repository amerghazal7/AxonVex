/**
 * @file systemSpecTest.cpp
 * @brief Unit tests for SystemSpec v1 (.axv.json) parse/validate (design spec §2, §4.1-4.2 slice).
 */

#include <axonvex_core/builtinUnits.hpp>
#include <axonvex_core/systemSpec.hpp>
#include <axonvex_core/utils/serialization/jsonSerializer.hpp>
#include <gtest/gtest.h>

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

} // namespace
} // namespace axonvex::core::test
