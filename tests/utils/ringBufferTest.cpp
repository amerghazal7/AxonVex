#include <gtest/gtest.h>
#include <axonvex/utils/utils.hpp>

using axonvex::utils::containers::RingBuffer;

TEST(RingBufferTest, PushPopBasic) {
    RingBuffer<int> rb(16);
    EXPECT_TRUE(rb.write(1));
    EXPECT_TRUE(rb.write(2));
    EXPECT_TRUE(rb.write(3));
    EXPECT_TRUE(rb.write(4));

    auto a = rb.read();
    auto b = rb.read();
    auto c = rb.read();
    auto d = rb.read();
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(b.has_value());
    EXPECT_TRUE(c.has_value());
    EXPECT_TRUE(d.has_value());
    EXPECT_EQ(a.value(), 1);
    EXPECT_EQ(b.value(), 2);
    EXPECT_EQ(c.value(), 3);
    EXPECT_EQ(d.value(), 4);
    EXPECT_TRUE(rb.isEmpty());
}
