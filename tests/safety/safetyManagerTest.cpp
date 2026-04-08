#include <axonvex_core/callback.hpp>
#include <axonvex_safety/safetyManager.hpp>
#include <axonvex_safety/safetyPolicy.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using axonvex::safety::PolicyResult;
using axonvex::safety::SafetyEvent;
using axonvex::safety::SafetyLevel;
using axonvex::safety::SafetyManager;
using axonvex::safety::SafetyPolicy;

namespace {

class NominalPolicy : public SafetyPolicy {
  public:
    NominalPolicy() : SafetyPolicy("nominal") {}
    PolicyResult evaluate() override {
        evaluations++;
        return {SafetyLevel::NOMINAL, "all clear"};
    }
    std::atomic<int> evaluations{0};
};

class WarningPolicy : public SafetyPolicy {
  public:
    WarningPolicy() : SafetyPolicy("battery_low") {}
    PolicyResult evaluate() override {
        return {SafetyLevel::WARNING, "battery below 20%"};
    }
};

class CriticalPolicy : public SafetyPolicy {
  public:
    CriticalPolicy() : SafetyPolicy("geofence") {}
    PolicyResult evaluate() override {
        return {SafetyLevel::CRITICAL, "outside geofence boundary"};
    }
};

class EmergencyPolicy : public SafetyPolicy {
  public:
    EmergencyPolicy() : SafetyPolicy("comms_lost") {}
    PolicyResult evaluate() override {
        return {SafetyLevel::EMERGENCY, "communication link lost"};
    }
};

class ConfigurablePolicy : public SafetyPolicy {
  public:
    explicit ConfigurablePolicy(const std::string& name)
        : SafetyPolicy(name) {}
    PolicyResult evaluate() override {
        return {level.load(), description};
    }
    std::atomic<SafetyLevel> level{SafetyLevel::NOMINAL};
    std::string description{"ok"};
};

class SafetyHandler : public axonvex::core::Callback<SafetyEvent> {
  public:
    void callbackPerform(const SafetyEvent event) override {
        eventCount++;
        lastLevel = event.level;
        lastSource = event.source;
        lastDescription = event.description;
    }
    std::atomic<int> eventCount{0};
    std::atomic<SafetyLevel> lastLevel{SafetyLevel::NOMINAL};
    std::string lastSource;
    std::string lastDescription;
};

} // namespace

// =====================================================================
// SafetyLevel and to_string
// =====================================================================

TEST(SafetyLevelTest, EnumOrdering) {
    EXPECT_LT(SafetyLevel::NOMINAL, SafetyLevel::ADVISORY);
    EXPECT_LT(SafetyLevel::ADVISORY, SafetyLevel::CAUTION);
    EXPECT_LT(SafetyLevel::CAUTION, SafetyLevel::WARNING);
    EXPECT_LT(SafetyLevel::WARNING, SafetyLevel::CRITICAL);
    EXPECT_LT(SafetyLevel::CRITICAL, SafetyLevel::EMERGENCY);
}

TEST(SafetyLevelTest, ToString) {
    EXPECT_STREQ(axonvex::safety::to_string(SafetyLevel::NOMINAL), "NOMINAL");
    EXPECT_STREQ(axonvex::safety::to_string(SafetyLevel::EMERGENCY), "EMERGENCY");
}

// =====================================================================
// SafetyPolicy basics
// =====================================================================

TEST(SafetyPolicyTest, NameAndEnabledDefaults) {
    NominalPolicy p;
    EXPECT_EQ(p.getName(), "nominal");
    EXPECT_TRUE(p.isEnabled());
}

TEST(SafetyPolicyTest, DisableSkipsEvaluation) {
    NominalPolicy p;
    p.setEnabled(false);
    EXPECT_FALSE(p.isEnabled());
}

// =====================================================================
// SafetyManager — construction and lifecycle
// =====================================================================

TEST(SafetyManagerTest, DefaultConstruction) {
    SafetyManager mgr;
    EXPECT_FALSE(mgr.isRunning());
    EXPECT_FALSE(mgr.isEmergencyStopped());
    EXPECT_EQ(mgr.getPolicyCount(), 0u);
}

TEST(SafetyManagerTest, StartStop) {
    SafetyManager mgr;
    ASSERT_TRUE(mgr.start());
    EXPECT_TRUE(mgr.isRunning());
    mgr.stop();
    EXPECT_FALSE(mgr.isRunning());
}

TEST(SafetyManagerTest, DoubleStartIsIdempotent) {
    SafetyManager mgr;
    ASSERT_TRUE(mgr.start());
    ASSERT_TRUE(mgr.start());
    mgr.stop();
}

TEST(SafetyManagerTest, DestructorStops) {
    auto mgr = std::make_unique<SafetyManager>();
    mgr->start();
    EXPECT_TRUE(mgr->isRunning());
    mgr.reset();
}

// =====================================================================
// Policy management
// =====================================================================

TEST(SafetyManagerTest, AddAndRetrievePolicy) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    EXPECT_EQ(mgr.getPolicyCount(), 1u);
    EXPECT_NE(mgr.getPolicy("nominal"), nullptr);
    EXPECT_EQ(mgr.getPolicy("nonexistent"), nullptr);
}

TEST(SafetyManagerTest, RemovePolicy) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    EXPECT_TRUE(mgr.removePolicy("nominal"));
    EXPECT_EQ(mgr.getPolicyCount(), 0u);
    EXPECT_FALSE(mgr.removePolicy("nominal"));
}

TEST(SafetyManagerTest, PolicyNamesList) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    mgr.addPolicy(std::make_unique<WarningPolicy>());
    auto names = mgr.getPolicyNames();
    EXPECT_EQ(names.size(), 2u);
}

TEST(SafetyManagerTest, AddNullPolicyIgnored) {
    SafetyManager mgr;
    mgr.addPolicy(nullptr);
    EXPECT_EQ(mgr.getPolicyCount(), 0u);
}

// =====================================================================
// On-demand evaluation
// =====================================================================

TEST(SafetyManagerTest, EvaluateAllNominal) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::NOMINAL);
}

TEST(SafetyManagerTest, EvaluateAllReturnsWorstLevel) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    mgr.addPolicy(std::make_unique<WarningPolicy>());
    mgr.addPolicy(std::make_unique<CriticalPolicy>());
    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::CRITICAL);
}

TEST(SafetyManagerTest, DisabledPoliciesAreSkipped) {
    SafetyManager mgr;
    auto critical = std::make_unique<CriticalPolicy>();
    critical->setEnabled(false);
    mgr.addPolicy(std::move(critical));
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::NOMINAL);
}

TEST(SafetyManagerTest, EmptyManagerEvaluatesToNominal) {
    SafetyManager mgr;
    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::NOMINAL);
}

// =====================================================================
// E-stop
// =====================================================================

TEST(SafetyManagerTest, ManualEmergencyStop) {
    SafetyManager mgr;
    EXPECT_FALSE(mgr.isEmergencyStopped());
    mgr.triggerEmergencyStop("test reason");
    EXPECT_TRUE(mgr.isEmergencyStopped());
}

TEST(SafetyManagerTest, EmergencyStopReset) {
    SafetyManager mgr;
    mgr.triggerEmergencyStop("test");
    EXPECT_TRUE(mgr.resetEmergencyStop());
    EXPECT_FALSE(mgr.isEmergencyStopped());
}

TEST(SafetyManagerTest, ResetWithoutStopReturnsFalse) {
    SafetyManager mgr;
    EXPECT_FALSE(mgr.resetEmergencyStop());
}

TEST(SafetyManagerTest, DoubleEmergencyStopIsIdempotent) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);

    mgr.triggerEmergencyStop("first");
    mgr.triggerEmergencyStop("second");
    EXPECT_EQ(handler.eventCount.load(), 1);

    auto stats = mgr.getStatistics();
    EXPECT_EQ(stats.emergencyStopCount, 1u);
}

TEST(SafetyManagerTest, EmergencyPolicyTriggersEStop) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<EmergencyPolicy>());
    mgr.evaluateAll();
    EXPECT_TRUE(mgr.isEmergencyStopped());
}

// =====================================================================
// Event handler callbacks
// =====================================================================

TEST(SafetyManagerTest, HandlerReceivesViolationEvents) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);
    mgr.addPolicy(std::make_unique<WarningPolicy>());

    mgr.evaluateAll();
    EXPECT_GE(handler.eventCount.load(), 1);
    EXPECT_EQ(handler.lastLevel.load(), SafetyLevel::WARNING);
    EXPECT_EQ(handler.lastSource, "battery_low");
}

TEST(SafetyManagerTest, HandlerReceivesEStopEvent) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);

    mgr.triggerEmergencyStop("manual test");
    EXPECT_EQ(handler.lastLevel.load(), SafetyLevel::EMERGENCY);
    EXPECT_NE(handler.lastDescription.find("E-STOP"), std::string::npos);
}

TEST(SafetyManagerTest, HandlerReceivesResetEvent) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);

    mgr.triggerEmergencyStop("test");
    mgr.resetEmergencyStop();
    EXPECT_EQ(handler.lastLevel.load(), SafetyLevel::NOMINAL);
    EXPECT_NE(handler.lastDescription.find("reset"), std::string::npos);
}

TEST(SafetyManagerTest, UnregisterHandler) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);
    EXPECT_TRUE(mgr.unregisterHandler(&handler));

    mgr.triggerEmergencyStop("test");
    EXPECT_EQ(handler.eventCount.load(), 0);
}

TEST(SafetyManagerTest, NominalPolicyDoesNotFireEvent) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);
    mgr.addPolicy(std::make_unique<NominalPolicy>());

    mgr.evaluateAll();
    EXPECT_EQ(handler.eventCount.load(), 0);
}

// =====================================================================
// Periodic evaluation loop
// =====================================================================

TEST(SafetyManagerTest, PeriodicEvaluationRunsPolicies) {
    SafetyManager mgr(std::chrono::milliseconds(30));
    auto policy = std::make_unique<NominalPolicy>();
    auto* ptr = policy.get();
    mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(mgr.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    mgr.stop();

    EXPECT_GE(ptr->evaluations.load(), 2);
}

TEST(SafetyManagerTest, EStopPausesPeriodicEvaluation) {
    SafetyManager mgr(std::chrono::milliseconds(20));
    auto policy = std::make_unique<NominalPolicy>();
    auto* ptr = policy.get();
    mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(mgr.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    int countBefore = ptr->evaluations.load();

    mgr.triggerEmergencyStop("test");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    int countAfter = ptr->evaluations.load();

    mgr.stop();
    EXPECT_LE(countAfter - countBefore, 1);
}

// =====================================================================
// Statistics
// =====================================================================

TEST(SafetyManagerTest, StatisticsTrackEvaluations) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<NominalPolicy>());
    mgr.evaluateAll();
    mgr.evaluateAll();

    auto stats = mgr.getStatistics();
    EXPECT_EQ(stats.evaluationCycles, 2u);
}

TEST(SafetyManagerTest, StatisticsTrackViolations) {
    SafetyManager mgr;
    mgr.addPolicy(std::make_unique<WarningPolicy>());
    mgr.addPolicy(std::make_unique<CriticalPolicy>());
    mgr.evaluateAll();

    auto stats = mgr.getStatistics();
    EXPECT_EQ(stats.policyViolations, 2u);
    EXPECT_EQ(stats.worstLevelSeen, SafetyLevel::CRITICAL);
}

TEST(SafetyManagerTest, StatisticsTrackEStopCount) {
    SafetyManager mgr;
    mgr.triggerEmergencyStop("first");
    mgr.resetEmergencyStop();
    mgr.triggerEmergencyStop("second");

    auto stats = mgr.getStatistics();
    EXPECT_EQ(stats.emergencyStopCount, 2u);
}

// =====================================================================
// Fault injection: dynamic policy level change
// =====================================================================

TEST(SafetyManagerTest, DynamicPolicyLevelChange) {
    SafetyManager mgr;
    SafetyHandler handler;
    mgr.registerHandler(&handler);

    auto policy = std::make_unique<ConfigurablePolicy>("sensor_check");
    auto* ptr = policy.get();
    mgr.addPolicy(std::move(policy));

    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::NOMINAL);
    EXPECT_EQ(handler.eventCount.load(), 0);

    ptr->level.store(SafetyLevel::CAUTION);
    ptr->description = "sensor drift detected";
    EXPECT_EQ(mgr.evaluateAll(), SafetyLevel::CAUTION);
    EXPECT_EQ(handler.eventCount.load(), 1);
    EXPECT_EQ(handler.lastSource, "sensor_check");

    ptr->level.store(SafetyLevel::EMERGENCY);
    ptr->description = "sensor failure";
    mgr.evaluateAll();
    EXPECT_TRUE(mgr.isEmergencyStopped());
}

// =====================================================================
// Configuration
// =====================================================================

TEST(SafetyManagerTest, ConfigureEvaluationPeriod) {
    SafetyManager mgr(std::chrono::milliseconds(100));
    EXPECT_EQ(mgr.getEvaluationPeriod(), std::chrono::milliseconds(100));
    mgr.setEvaluationPeriod(std::chrono::milliseconds(50));
    EXPECT_EQ(mgr.getEvaluationPeriod(), std::chrono::milliseconds(50));
}
