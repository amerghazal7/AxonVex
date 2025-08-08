#include <gtest/gtest.h>
#include <axonvex/types/primitives/uuid.hpp>

using axonvex::types::primitives::UUID;

TEST(UUIDTest, GenerateAndStringRoundtrip) {
    UUID id = UUID::generate();
    ASSERT_TRUE(id.isValid());
    auto s = id.toString();
    ASSERT_EQ(s.size(), 36u);
    UUID parsed = UUID::fromString(s);
    EXPECT_EQ(parsed, id);
}

TEST(UUIDTest, ParseInvalid) {
    UUID parsed = UUID::fromString("not-a-uuid");
    EXPECT_FALSE(parsed.isValid());
}
