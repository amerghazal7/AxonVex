/**
 * @file unitMetadataTest.cpp
 * @brief Unit tests for the introspection metadata contract (unitMetadata.hpp).
 */

#include <axonvex_core/unitMetadata.hpp>
#include <gtest/gtest.h>

namespace axonvex::core::test {
namespace {

TEST(PortDataTypeNameTest, CoreRegisteredPrimitivesHaveCanonicalNames) {
    EXPECT_STREQ(PortDataTypeName<bool>::value(), "bool");
    EXPECT_STREQ(PortDataTypeName<std::int32_t>::value(), "int32");
    EXPECT_STREQ(PortDataTypeName<std::int64_t>::value(), "int64");
    EXPECT_STREQ(PortDataTypeName<double>::value(), "double");
    EXPECT_STREQ(PortDataTypeName<std::string>::value(), "string");
}

TEST(PortDescriptorTest, MakeDerivesCanonicalDataType) {
    auto d = PortDescriptor::make<double>(3, "out", PortDescriptor::Direction::Output,
                                          PortDescriptor::Kind::Sync, false);
    EXPECT_EQ(d.index, 3);
    EXPECT_EQ(d.name, "out");
    EXPECT_EQ(d.direction, PortDescriptor::Direction::Output);
    EXPECT_EQ(d.kind, PortDescriptor::Kind::Sync);
    EXPECT_EQ(d.dataType, "double");
    EXPECT_FALSE(d.required);
}

TEST(PortDescriptorTest, ToJsonRoundTripsFields) {
    auto d = PortDescriptor::make<std::string>(0, "in", PortDescriptor::Direction::Input,
                                               PortDescriptor::Kind::Async);
    auto j = d.toJson();
    EXPECT_EQ(j.at("index").get<int>(), 0);
    EXPECT_EQ(j.at("name").get<std::string>(), "in");
    EXPECT_EQ(j.at("direction").get<std::string>(), "Input");
    EXPECT_EQ(j.at("kind").get<std::string>(), "Async");
    EXPECT_EQ(j.at("dataType").get<std::string>(), "string");
    EXPECT_TRUE(j.at("required").get<bool>());
}

TEST(ParamDescriptorTest, NullDefaultMeansRequired) {
    ParamDescriptor required{"window", ParamDescriptor::Type::Int, nlohmann::json(), "", {}};
    ParamDescriptor optional{"gain", ParamDescriptor::Type::Double, 1.0, "", {}};
    EXPECT_TRUE(required.isRequired());
    EXPECT_FALSE(optional.isRequired());
}

TEST(UnitTypeDescriptorTest, ToJsonListsPortsAndParameters) {
    UnitTypeDescriptor d;
    d.typeName = "axonvex.Demo";
    d.ports.push_back(PortDescriptor::make<double>(0, "out", PortDescriptor::Direction::Output,
                                                   PortDescriptor::Kind::Sync));
    d.parameters.push_back({"gain", ParamDescriptor::Type::Double, 1.0, "gain", {}});

    auto j = d.toJson();
    EXPECT_EQ(j.at("typeName").get<std::string>(), "axonvex.Demo");
    ASSERT_EQ(j.at("ports").size(), 1u);
    ASSERT_EQ(j.at("parameters").size(), 1u);
    EXPECT_EQ(j.at("parameters")[0].at("name").get<std::string>(), "gain");
}

class ValidateParamsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        meta.typeName = "axonvex.TestUnit";
        meta.parameters.push_back({"window", ParamDescriptor::Type::Int, nlohmann::json(),
                                   "window size", nlohmann::json{{"min", 1}, {"max", 64}}});
        meta.parameters.push_back({"gain", ParamDescriptor::Type::Double, 1.0, "gain", {}});
        meta.parameters.push_back({"mode", ParamDescriptor::Type::Enum, "fast", "mode",
                                   nlohmann::json{{"values", {"fast", "slow"}}}});
    }
    UnitTypeDescriptor meta;
};

TEST_F(ValidateParamsTest, AllValidParamsProduceNoIssues) {
    auto issues =
        validateParams(meta, nlohmann::json{{"window", 8}, {"gain", 2.5}, {"mode", "slow"}});
    EXPECT_TRUE(issues.empty());
}

TEST_F(ValidateParamsTest, DefaultsSatisfyOptionalParams) {
    // "window" is the only required param.
    auto issues = validateParams(meta, nlohmann::json{{"window", 8}});
    EXPECT_TRUE(issues.empty());
}

TEST_F(ValidateParamsTest, MissingRequiredParamIsReported) {
    auto issues = validateParams(meta, nlohmann::json::object());
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].paramName, "window");
    EXPECT_EQ(issues[0].code, "MISSING_REQUIRED");
}

TEST_F(ValidateParamsTest, UnknownKeyIsReported) {
    auto issues = validateParams(meta, nlohmann::json{{"window", 8}, {"typo", 1}});
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].paramName, "typo");
    EXPECT_EQ(issues[0].code, "UNKNOWN_KEY");
}

TEST_F(ValidateParamsTest, TypeMismatchIsReported) {
    auto issues = validateParams(meta, nlohmann::json{{"window", "eight"}});
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].paramName, "window");
    EXPECT_EQ(issues[0].code, "TYPE_MISMATCH");
}

TEST_F(ValidateParamsTest, RangeConstraintViolationIsReported) {
    auto issues = validateParams(meta, nlohmann::json{{"window", 999}});
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].paramName, "window");
    EXPECT_EQ(issues[0].code, "CONSTRAINT_VIOLATION");
}

TEST_F(ValidateParamsTest, EnumConstraintViolationIsReported) {
    auto issues = validateParams(meta, nlohmann::json{{"window", 8}, {"mode", "turbo"}});
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].paramName, "mode");
    EXPECT_EQ(issues[0].code, "CONSTRAINT_VIOLATION");
}

TEST_F(ValidateParamsTest, MultipleIssuesAreAllCollected) {
    auto issues = validateParams(meta, nlohmann::json{{"typo", 1}, {"gain", "not-a-number"}});
    // Missing "window" + unknown "typo" + type-mismatched "gain" == 3.
    EXPECT_EQ(issues.size(), 3u);
}

} // namespace
} // namespace axonvex::core::test
