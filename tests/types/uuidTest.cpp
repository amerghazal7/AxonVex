#include <axonvex_core/types/primitives/uuid.hpp>
#include <gtest/gtest.h>

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

// Regression test for defect C17: version bits were applied to low_ while
// toString() reads the version nibble from high_, producing non-RFC-4122 strings.
TEST(UUIDTest, GenerateSetsRfc4122VersionAndVariant) {
    for (int i = 0; i < 16; ++i) {
        auto s = UUID::generate().toString();
        ASSERT_EQ(s.size(), 36u);
        EXPECT_EQ(s[14], '4') << s;                                         // version nibble
        EXPECT_NE(std::string("89ab").find(s[19]), std::string::npos) << s; // variant
    }
}
