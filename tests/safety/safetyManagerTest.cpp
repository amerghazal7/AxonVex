#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_safety/safetyManager.hpp>
#include <axonvex_safety/safetyPolicy.hpp>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
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
    explicit ConfigurablePolicy(const std::string& name) : SafetyPolicy(name) {}
    PolicyResult evaluate() override {
        return {level.load(), description};
    }
    std::atomic<SafetyLevel> level{SafetyLevel::NOMINAL};
    std::string description{"ok"};
};

/// C12 regression: a handler that re-enters the manager's policy API while it
/// is being dispatched, and removes the policy that produced the event.
class ReentrantPolicyHandler : public axonvex::core::Callback<SafetyEvent> {
  public:
    explicit ReentrantPolicyHandler(SafetyManager& mgr) : mgr_(mgr) {}
    void callbackPerform(const SafetyEvent event) override {
        policyCountSeen = mgr_.getPolicyCount();
        periodSeen = mgr_.getEvaluationPeriod();
        removed = mgr_.removePolicy(event.source);
    }
    size_t policyCountSeen{0};
    SafetyManager::Duration periodSeen{0};
    bool removed{false};

  private:
    SafetyManager& mgr_;
};

/// C12 regression: a handler that calls back into evaluateAll() and
/// triggerEmergencyStop() from inside dispatch.
class ReentrantEvaluateHandler : public axonvex::core::Callback<SafetyEvent> {
  public:
    explicit ReentrantEvaluateHandler(SafetyManager& mgr) : mgr_(mgr) {}
    void callbackPerform(const SafetyEvent event) override {
        calls++;
        reentrantLevel = mgr_.evaluateAll();
        if (estopFromHandler) {
            mgr_.triggerEmergencyStop("from handler: " + event.source);
        }
    }
    std::atomic<int> calls{0};
    std::atomic<SafetyLevel> reentrantLevel{SafetyLevel::NOMINAL};
    bool estopFromHandler{false};

  private:
    SafetyManager& mgr_;
};

/// C12 regression: a policy that re-enters the manager's policy API from
/// inside evaluate().
class SelfInspectingPolicy : public SafetyPolicy {
  public:
    explicit SelfInspectingPolicy(SafetyManager& mgr)
        : SafetyPolicy("self_inspecting"), mgr_(mgr) {}
    PolicyResult evaluate() override {
        namesSeen = mgr_.getPolicyNames().size();
        return {SafetyLevel::NOMINAL, "ok"};
    }
    std::atomic<size_t> namesSeen{0};

  private:
    SafetyManager& mgr_;
};

/// Runs @p fn on a worker thread; returns false if it has not finished within
/// @p limit. On timeout the worker is detached — it is deadlocked and joining
/// would hang the suite — so @p fn must only touch state kept alive by a
/// shared_ptr it captures by value.
bool finishesWithin(std::function<void()> fn, std::chrono::milliseconds limit) {
    // Polled rather than condition-variable based: GCC 11's libtsan does not
    // intercept pthread_cond_clockwait, so wait_for() here produces bogus
    // "double lock"/race reports that mask real findings.
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([fn, done]() {
        fn();
        done->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!done->load()) {
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}

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

// =====================================================================
// C12: no locks held across policy evaluation or handler dispatch
// =====================================================================

TEST(SafetyManagerTest, HandlerMayMutatePolicyRegistryDuringDispatch) {
    struct Fixture {
        SafetyManager mgr;
        ReentrantPolicyHandler handler{mgr};
    };
    auto fx = std::make_shared<Fixture>();
    fx->mgr.setEvaluationPeriod(std::chrono::milliseconds(25));
    fx->mgr.registerHandler(&fx->handler);
    fx->mgr.addPolicy(std::unique_ptr<SafetyPolicy>(new CriticalPolicy()));

    ASSERT_TRUE(finishesWithin([fx]() { fx->mgr.evaluateAll(); }, std::chrono::seconds(2)))
        << "evaluateAll() deadlocked: policy registry locked across handler dispatch (C12)";

    EXPECT_EQ(fx->handler.policyCountSeen, 1u);
    EXPECT_EQ(fx->handler.periodSeen, std::chrono::milliseconds(25));
    EXPECT_TRUE(fx->handler.removed);
    EXPECT_EQ(fx->mgr.getPolicyCount(), 0u);
}

TEST(SafetyManagerTest, PolicyMayQueryManagerDuringEvaluate) {
    struct Fixture {
        SafetyManager mgr;
    };
    auto fx = std::make_shared<Fixture>();
    auto policy = std::unique_ptr<SelfInspectingPolicy>(new SelfInspectingPolicy(fx->mgr));
    auto* ptr = policy.get();
    fx->mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(finishesWithin([fx]() { fx->mgr.evaluateAll(); }, std::chrono::seconds(2)))
        << "evaluateAll() deadlocked: policy registry locked across evaluate() (C12)";

    EXPECT_EQ(ptr->namesSeen.load(), 1u);
}

TEST(SafetyManagerTest, ReentrantEvaluateAllFromHandlerReturnsInsteadOfHanging) {
    struct Fixture {
        SafetyManager mgr;
        ReentrantEvaluateHandler handler{mgr};
    };
    auto fx = std::make_shared<Fixture>();
    fx->mgr.registerHandler(&fx->handler);
    auto policy = std::unique_ptr<CriticalPolicy>(new CriticalPolicy());
    fx->mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(finishesWithin([fx]() { fx->mgr.evaluateAll(); }, std::chrono::seconds(2)))
        << "evaluateAll() deadlocked on a re-entrant call from a handler (C12)";

    // Exactly one dispatch: the re-entrant call must not re-run the policies
    // (that would recurse until the stack is exhausted).
    EXPECT_EQ(fx->handler.calls.load(), 1);
    EXPECT_EQ(fx->handler.reentrantLevel.load(), SafetyLevel::CRITICAL);
    EXPECT_EQ(fx->mgr.getStatistics().evaluationCycles, 1u);
}

TEST(SafetyManagerTest, EmergencyStopFromHandlerDoesNotHang) {
    struct Fixture {
        SafetyManager mgr;
        ReentrantEvaluateHandler handler{mgr};
    };
    auto fx = std::make_shared<Fixture>();
    fx->handler.estopFromHandler = true;
    fx->mgr.registerHandler(&fx->handler);
    fx->mgr.addPolicy(std::unique_ptr<SafetyPolicy>(new CriticalPolicy()));

    ASSERT_TRUE(finishesWithin([fx]() { fx->mgr.evaluateAll(); }, std::chrono::seconds(2)))
        << "triggerEmergencyStop() from a handler deadlocked (C12)";

    EXPECT_TRUE(fx->mgr.isEmergencyStopped());
    EXPECT_EQ(fx->mgr.getStatistics().emergencyStopCount, 1u);
}

TEST(SafetyManagerTest, EvaluationPeriodMayChangeWhileRunning) {
    SafetyManager mgr(std::chrono::milliseconds(1));
    auto policy = std::unique_ptr<NominalPolicy>(new NominalPolicy());
    auto* ptr = policy.get();
    mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(mgr.start());
    // Hammer the period while the loop reads it (TSan is the real assertion
    // here) until the loop has actually run a cycle.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (int i = 0; ptr->evaluations.load() == 0 && std::chrono::steady_clock::now() < deadline;
         ++i) {
        mgr.setEvaluationPeriod(std::chrono::milliseconds(1 + (i % 3)));
    }
    mgr.stop();

    EXPECT_GT(ptr->evaluations.load(), 0);
    EXPECT_GT(mgr.getEvaluationPeriod().count(), 0);
}

// C26: a policy whose evaluate() throws used to propagate out of the evaluation
// worker and call std::terminate. It must not — and it must not be silently
// swallowed either: a policy that cannot report cannot vouch for the system, so
// the throw surfaces as a CRITICAL violation through the normal event path.
namespace {
class ThrowingPolicy final : public axonvex::safety::SafetyPolicy {
  public:
    ThrowingPolicy() : SafetyPolicy("throwing") {}
    std::atomic<int> evaluations{0};
    axonvex::safety::PolicyResult evaluate() override {
        ++evaluations;
        throw std::runtime_error("policy blew up");
    }
};

class CountingPolicy final : public axonvex::safety::SafetyPolicy {
  public:
    CountingPolicy() : SafetyPolicy("counting") {}
    std::atomic<int> evaluations{0};
    axonvex::safety::PolicyResult evaluate() override {
        ++evaluations;
        return {};
    }
};

class RecordingHandler final : public axonvex::core::Callback<axonvex::safety::SafetyEvent> {
  public:
    std::atomic<int> count{0};
    std::mutex mtx;
    axonvex::safety::SafetyLevel worst{axonvex::safety::SafetyLevel::NOMINAL};
    void callbackPerform(const axonvex::safety::SafetyEvent event) override {
        std::lock_guard<std::mutex> lock(mtx);
        if (event.level > worst) {
            worst = event.level;
        }
        ++count;
    }
};

// C33's shape, fourth occurrence: policies run ON the evaluation worker, so a
// policy calling stop() used to make that thread join itself.
class StoppingPolicy final : public axonvex::safety::SafetyPolicy {
  public:
    StoppingPolicy() : SafetyPolicy("stopping") {}
    axonvex::safety::SafetyManager* target{nullptr};
    std::atomic<int> evaluations{0};
    axonvex::safety::PolicyResult evaluate() override {
        ++evaluations;
        if (target) {
            target->stop();
        }
        return {};
    }
};
} // namespace

TEST(SafetyManagerTest, ThrowingPolicyIsReportedNotFatal) {
    axonvex::safety::SafetyManager mgr(std::chrono::milliseconds(5));
    auto bad = std::unique_ptr<ThrowingPolicy>(new ThrowingPolicy());
    auto good = std::unique_ptr<CountingPolicy>(new CountingPolicy());
    auto* badPtr = bad.get();
    auto* goodPtr = good.get();
    RecordingHandler handler;

    mgr.registerHandler(&handler);
    mgr.addPolicy(std::move(bad));
    mgr.addPolicy(std::move(good));

    // The throw is reported as a violation, not swallowed and not fatal.
    const auto level = mgr.evaluateAll();
    EXPECT_EQ(level, axonvex::safety::SafetyLevel::CRITICAL);
    EXPECT_GT(handler.count.load(), 0);
    {
        std::lock_guard<std::mutex> lock(handler.mtx);
        EXPECT_EQ(handler.worst, axonvex::safety::SafetyLevel::CRITICAL);
    }
    // A throwing policy must not abort the cycle for the others.
    EXPECT_GT(goodPtr->evaluations.load(), 0);
    EXPECT_GT(badPtr->evaluations.load(), 0);

    // And the periodic loop survives it repeatedly.
    ASSERT_TRUE(mgr.start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const int before = badPtr->evaluations.load();
    while (badPtr->evaluations.load() < before + 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    mgr.stop();
    EXPECT_GE(badPtr->evaluations.load(), before + 3);
}

TEST(SafetyManagerTest, PolicyMayStopTheManager) {
    axonvex::safety::SafetyManager mgr(std::chrono::milliseconds(5));
    auto policy = std::unique_ptr<StoppingPolicy>(new StoppingPolicy());
    auto* ptr = policy.get();
    ptr->target = &mgr;
    mgr.addPolicy(std::move(policy));

    ASSERT_TRUE(mgr.start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (ptr->evaluations.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_GT(ptr->evaluations.load(), 0);

    // The self-stop defers the join; an external stop() must still complete it.
    mgr.stop();
    EXPECT_FALSE(mgr.isRunning());
}

// C36: same shape as WatchdogTest.ConcurrentStopIsSafe — two external stop()
// calls must not both reach join() on the same std::thread.
TEST(SafetyManagerTest, ConcurrentStopIsSafe) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        axonvex::safety::SafetyManager mgr(std::chrono::milliseconds(5));
        auto policy = std::unique_ptr<CountingPolicy>(new CountingPolicy());
        mgr.addPolicy(std::move(policy));
        ASSERT_TRUE(mgr.start());

        std::atomic<int> ready{0};
        std::vector<std::thread> stoppers;
        for (int t = 0; t < 4; ++t) {
            stoppers.emplace_back([&mgr, &ready]() {
                ready.fetch_add(1);
                while (ready.load() < 4) {
                    std::this_thread::yield();
                }
                mgr.stop();
            });
        }
        for (auto& s : stoppers) {
            s.join();
        }
        EXPECT_FALSE(mgr.isRunning());
    }
}
