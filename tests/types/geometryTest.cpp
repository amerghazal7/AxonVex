#include <gtest/gtest.h>
#include <axonvex_core/types/types.hpp>

using axonvex::types::geometry::Point2D;
using axonvex::types::geometry::Point3D;
using axonvex::types::geometry::Quaternion;

TEST(GeometryTest, Point2DBasicOps) {
    Point2D a(1.0, 2.0), b(3.0, 5.0);
    auto c = a + b;
    EXPECT_DOUBLE_EQ(c.x(), 4.0);
    EXPECT_DOUBLE_EQ(c.y(), 7.0);
    EXPECT_NEAR(a.distance(b), std::sqrt(13.0), 1e-12);
}

TEST(GeometryTest, Point3DBasicOps) {
    Point3D a(1.0, 2.0, 3.0), b(4.0, 6.0, 8.0);
    auto c = b - a;
    EXPECT_DOUBLE_EQ(c.x(), 3.0);
    EXPECT_DOUBLE_EQ(c.y(), 4.0);
    EXPECT_DOUBLE_EQ(c.z(), 5.0);
}

TEST(GeometryTest, QuaternionOps) {
    Quaternion q1; // identity
    Quaternion q2(0.0, 1.0, 0.0, 0.0);
    auto q3 = q1 * q2;
    EXPECT_DOUBLE_EQ(q3.w, 0.0);
    EXPECT_DOUBLE_EQ(q3.x, 1.0);
    EXPECT_DOUBLE_EQ(q3.y, 0.0);
    EXPECT_DOUBLE_EQ(q3.z, 0.0);
    auto qn = q3.normalized();
    EXPECT_NEAR(qn.norm(), 1.0, 1e-12);
}
