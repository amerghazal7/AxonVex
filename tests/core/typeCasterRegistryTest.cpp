/**
 * @file typeCasterRegistryTest.cpp
 * @brief Regression tests for C20: TypeCasterRegistry pair-keying.
 *
 * The registry used to key on typeid(InternalType) alone: registering
 * <float, FakeMsgA> then <float, FakeMsgB> silently overwrote the first
 * entry, and getCaster<float, FakeMsgA>() after that overwrite
 * dynamic_cast'ed to the wrong concrete TypeCaster type and returned
 * nullptr. Keyed by (InternalType, RosMsg) now: both registrations
 * coexist and lookups are exact-pair.
 */

#include <axonvex_ros2/typeCasterRegistry.hpp>
#include <gtest/gtest.h>

using axonvex::ros2::TypeCasterRegistry;

namespace {

// Two distinct "ROS message" stand-ins so a single InternalType (float) can
// be registered against each without pulling in rclcpp/std_msgs.
struct FakeMsgA {
    float value = 0.0F;
};
struct FakeMsgB {
    float value = 0.0F;
};

} // namespace

TEST(TypeCasterRegistryTest, DistinctRosTypesForOneInternalTypeCoexist) {
    TypeCasterRegistry registry;

    registry.registerCaster<float, FakeMsgA>([](const FakeMsgA& msg) { return msg.value; },
                                             [](const float& v) {
                                                 FakeMsgA msg;
                                                 msg.value = v;
                                                 return msg;
                                             });
    registry.registerCaster<float, FakeMsgB>([](const FakeMsgB& msg) { return msg.value * 2.0F; },
                                             [](const float& v) {
                                                 FakeMsgB msg;
                                                 msg.value = v / 2.0F;
                                                 return msg;
                                             });

    const auto* casterA = registry.getCaster<float, FakeMsgA>();
    const auto* casterB = registry.getCaster<float, FakeMsgB>();

    ASSERT_NE(casterA, nullptr);
    ASSERT_NE(casterB, nullptr);

    FakeMsgA msgA;
    msgA.value = 3.0F;
    EXPECT_FLOAT_EQ(casterA->fromRos(msgA), 3.0F);
    EXPECT_FLOAT_EQ(casterA->toRos(3.0F).value, 3.0F);

    FakeMsgB msgB;
    msgB.value = 3.0F;
    EXPECT_FLOAT_EQ(casterB->fromRos(msgB), 6.0F);
    EXPECT_FLOAT_EQ(casterB->toRos(6.0F).value, 3.0F);
}

TEST(TypeCasterRegistryTest, UnregisteredPairReturnsNull) {
    TypeCasterRegistry registry;

    registry.registerCaster<float, FakeMsgA>([](const FakeMsgA& msg) { return msg.value; },
                                             [](const float& v) {
                                                 FakeMsgA msg;
                                                 msg.value = v;
                                                 return msg;
                                             });

    EXPECT_EQ((registry.getCaster<float, FakeMsgB>()), nullptr);
    EXPECT_EQ((registry.getCaster<int, FakeMsgA>()), nullptr);
}

// Review finding 1: registerCaster overwriting an existing pair would free
// the old TypeCaster while createSubscriber/createPublisher's rclcpp
// callbacks still hold its raw address — a dangling pointer / use-after-free
// once a message arrives. Refused rather than allowed: re-registration
// throws instead of silently invalidating any unit already wired against
// the old caster.
TEST(TypeCasterRegistryTest, ReregisteringAnExistingPairThrows) {
    TypeCasterRegistry registry;

    registry.registerCaster<float, FakeMsgA>([](const FakeMsgA& msg) { return msg.value; },
                                             [](const float& v) {
                                                 FakeMsgA msg;
                                                 msg.value = v;
                                                 return msg;
                                             });

    EXPECT_THROW(
        (registry.registerCaster<float, FakeMsgA>([](const FakeMsgA& msg) { return msg.value; },
                                                  [](const float& v) {
                                                      FakeMsgA msg;
                                                      msg.value = v;
                                                      return msg;
                                                  })),
        std::logic_error);

    // The original caster is untouched by the refused attempt.
    const auto* caster = registry.getCaster<float, FakeMsgA>();
    ASSERT_NE(caster, nullptr);
    FakeMsgA msg;
    msg.value = 5.0F;
    EXPECT_FLOAT_EQ(caster->fromRos(msg), 5.0F);

    // A different pair is unaffected — refusal is per-key, not global.
    EXPECT_NO_THROW(
        (registry.registerCaster<float, FakeMsgB>([](const FakeMsgB& msg) { return msg.value; },
                                                  [](const float& v) {
                                                      FakeMsgB msg;
                                                      msg.value = v;
                                                      return msg;
                                                  })));
}
