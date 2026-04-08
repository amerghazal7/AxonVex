#pragma once

#include <axonvex_core/caller.hpp>
#include <axonvex_core/callback.hpp>
#include <axonvex_safety/safetyPolicy.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace axonvex::safety {

struct SafetyManagerStatistics {
    uint64_t evaluationCycles{0};
    uint64_t emergencyStopCount{0};
    uint64_t policyViolations{0};
    SafetyLevel worstLevelSeen{SafetyLevel::NOMINAL};
};

/**
 * @brief Central safety manager with e-stop, policy evaluation, and event
 *        notification.
 *
 * Owns a set of SafetyPolicy instances and periodically evaluates them.
 * When any policy reports a level above a configurable action threshold the
 * manager emits a SafetyEvent to all registered handlers. An emergency stop
 * can be triggered programmatically or by a policy returning EMERGENCY.
 *
 * The evaluation loop runs on a dedicated thread (like Watchdog). Call
 * start() after adding policies and stop() before destruction.
 */
class SafetyManager {
  public:
    using Duration = std::chrono::milliseconds;
    using Handler = axonvex::core::Callback<SafetyEvent>;

    explicit SafetyManager(Duration evaluationPeriod = Duration(100))
        : evaluationPeriod_(evaluationPeriod) {}

    ~SafetyManager() { stop(); }

    SafetyManager(const SafetyManager&) = delete;
    SafetyManager& operator=(const SafetyManager&) = delete;

    // -----------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------

    bool start() {
        if (running_.exchange(true)) return true;
        emergencyStopped_.store(false);
        worker_ = std::thread([this]() { evaluationLoop(); });
        return true;
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    bool isRunning() const { return running_.load(); }

    // -----------------------------------------------------------------
    // E-stop
    // -----------------------------------------------------------------

    void triggerEmergencyStop(const std::string& reason) {
        if (emergencyStopped_.exchange(true)) return;

        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.emergencyStopCount++;
            stats_.worstLevelSeen = SafetyLevel::EMERGENCY;
        }

        SafetyEvent event;
        event.level = SafetyLevel::EMERGENCY;
        event.source = "SafetyManager";
        event.description = "E-STOP: " + reason;
        event.timestamp = std::chrono::steady_clock::now();
        caller_.callCallbacksSafe(event);
    }

    bool resetEmergencyStop() {
        if (!emergencyStopped_.load()) return false;
        emergencyStopped_.store(false);

        SafetyEvent event;
        event.level = SafetyLevel::NOMINAL;
        event.source = "SafetyManager";
        event.description = "E-STOP reset";
        event.timestamp = std::chrono::steady_clock::now();
        caller_.callCallbacksSafe(event);
        return true;
    }

    bool isEmergencyStopped() const { return emergencyStopped_.load(); }

    // -----------------------------------------------------------------
    // Policy management
    // -----------------------------------------------------------------

    void addPolicy(std::unique_ptr<SafetyPolicy> policy) {
        if (!policy) return;
        std::lock_guard<std::mutex> lock(policiesMutex_);
        const std::string& name = policy->getName();
        policies_[name] = std::move(policy);
    }

    bool removePolicy(const std::string& name) {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        return policies_.erase(name) > 0;
    }

    SafetyPolicy* getPolicy(const std::string& name) const {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        auto it = policies_.find(name);
        return (it != policies_.end()) ? it->second.get() : nullptr;
    }

    size_t getPolicyCount() const {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        return policies_.size();
    }

    std::vector<std::string> getPolicyNames() const {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        std::vector<std::string> names;
        names.reserve(policies_.size());
        for (const auto& kv : policies_) {
            names.push_back(kv.first);
        }
        return names;
    }

    // -----------------------------------------------------------------
    // On-demand evaluation (also called by the periodic loop)
    // -----------------------------------------------------------------

    SafetyLevel evaluateAll() {
        SafetyLevel worst = SafetyLevel::NOMINAL;

        std::lock_guard<std::mutex> lock(policiesMutex_);
        for (auto& kv : policies_) {
            SafetyPolicy* policy = kv.second.get();
            if (!policy || !policy->isEnabled()) continue;

            PolicyResult result = policy->evaluate();
            if (result.level > worst) {
                worst = result.level;
            }

            if (result.level > SafetyLevel::NOMINAL) {
                {
                    std::lock_guard<std::mutex> slock(statsMutex_);
                    stats_.policyViolations++;
                    if (result.level > stats_.worstLevelSeen) {
                        stats_.worstLevelSeen = result.level;
                    }
                }

                SafetyEvent event;
                event.level = result.level;
                event.source = kv.first;
                event.description = result.description;
                event.timestamp = std::chrono::steady_clock::now();
                caller_.callCallbacksSafe(event);
            }

            if (result.level == SafetyLevel::EMERGENCY) {
                triggerEmergencyStop(kv.first + ": " + result.description);
            }
        }

        {
            std::lock_guard<std::mutex> slock(statsMutex_);
            stats_.evaluationCycles++;
        }

        return worst;
    }

    // -----------------------------------------------------------------
    // Event handlers
    // -----------------------------------------------------------------

    void registerHandler(Handler* handler) { caller_.registerCallback(handler); }
    bool unregisterHandler(Handler* handler) { return caller_.unregisterCallback(handler); }

    // -----------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------

    void setEvaluationPeriod(Duration d) {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        evaluationPeriod_ = d;
    }

    Duration getEvaluationPeriod() const {
        std::lock_guard<std::mutex> lock(policiesMutex_);
        return evaluationPeriod_;
    }

    // -----------------------------------------------------------------
    // Statistics
    // -----------------------------------------------------------------

    SafetyManagerStatistics getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }

  private:
    void evaluationLoop() {
        while (running_.load()) {
            if (!emergencyStopped_.load()) {
                evaluateAll();
            }
            std::this_thread::sleep_for(evaluationPeriod_);
        }
    }

    Duration evaluationPeriod_;
    std::atomic<bool> running_{false};
    std::atomic<bool> emergencyStopped_{false};
    std::thread worker_;

    mutable std::mutex policiesMutex_;
    std::unordered_map<std::string, std::unique_ptr<SafetyPolicy>> policies_;

    axonvex::core::Caller<SafetyEvent> caller_;

    mutable std::mutex statsMutex_;
    SafetyManagerStatistics stats_;
};

} // namespace axonvex::safety
