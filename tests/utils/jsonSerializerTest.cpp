#include <axonvex_core/utils/utils.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using axonvex::utils::serialization::JSONSerializer;
using nlohmann::json;

TEST(JSONSerializerTest, StringRoundTrip) {
    json j;
    j["name"] = "AxonVex";
    j["version"] = 1;
    auto s = JSONSerializer::toString(j, 0);
    auto j2 = JSONSerializer::fromString(s);
    EXPECT_EQ(j2["name"], "AxonVex");
    EXPECT_EQ(j2["version"], 1);
}
