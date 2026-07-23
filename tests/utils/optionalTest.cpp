#include <axonvex_core/utils/optional.hpp>
#include <gtest/gtest.h>
#include <string>
#include <utility>

using axonvex::optional;

TEST(OptionalTest, BasicEngageDisengage) {
    optional<int> o;
    EXPECT_FALSE(o.has_value());
    o = 42;
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(*o, 42);
    o = axonvex::nullopt;
    EXPECT_FALSE(o.has_value());
    EXPECT_EQ(o.value_or(7), 7);
    EXPECT_THROW(o.value(), std::logic_error);
}

// Regression test for defect C16: move construction/assignment disengaged the
// source, diverging from std::optional (source stays engaged with a moved-from T).
TEST(OptionalTest, MoveLeavesSourceEngaged) {
    optional<std::string> src(std::string("payload"));
    optional<std::string> dst(std::move(src));
    ASSERT_TRUE(dst.has_value());
    EXPECT_EQ(*dst, "payload");
    EXPECT_TRUE(src.has_value()); // std semantics: engaged, value unspecified

    optional<std::string> src2(std::string("payload2"));
    optional<std::string> dst2;
    dst2 = std::move(src2);
    ASSERT_TRUE(dst2.has_value());
    EXPECT_EQ(*dst2, "payload2");
    EXPECT_TRUE(src2.has_value());

    // Moving from a disengaged optional disengages the destination.
    optional<std::string> empty;
    dst2 = std::move(empty);
    EXPECT_FALSE(dst2.has_value());
}
