#pragma once

#include <algorithm>
#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_core/safetyHook.hpp>
#include <axonvex_safety/safetyPolicy.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
class SafetyManager : public axonvex::core::SafetyHook {
  public:
    using Duration = std::chrono::milliseconds;
    using Handler = axonvex::core::Callback<SafetyEvent>;

    explicit SafetyManager(Duration evaluationPeriod = Duration(100))
        : evaluationPeriod_(evaluationPeriod) {}

    ~SafetyManager() {
        stop();
    }

    SafetyManager(const SafetyManager&) = delete;
    SafetyManager& operator=(const SafetyManager&) = delete;

    // -----------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------

    bool start() {
        if (running_.exchange(true))
            return true;
        emergencyStopped_.store(false);
        worker_ = std::thread([this]() {
            workerId_.store(std::this_thread::get_id(), std::memory_order_release);
            evaluationLoop();
        });
        return true;
    }

    void stop() {
        // No early return on running_: after a deferred self-stop the flag is
        // already false while the thread still needs reclaiming.
        running_.store(false);

        // Checked before anything else touches worker_. Policies and handlers
        // run ON the evaluation worker, so self-detection must not read the
        // std::thread object — start() move-assigns it and the child has no
        // happens-before edge to that write. The id is published by the child.
        if (workerId_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            // Self-stop from a policy or handler. Joining would be a self-join:
            // system_error unwinding out of the thread entry, i.e.
            // std::terminate (C33's shape). The loop exits after this cycle;
            // the join is left to the destructor or an external stop().
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }
        workerId_.store(std::thread::id(), std::memory_order_release);
    }

    bool isRunning() const {
        return running_.load();
    }

    // -----------------------------------------------------------------
    // E-stop
    // -----------------------------------------------------------------

    void triggerEmergencyStop(const std::string& reason) {
        if (emergencyStopped_.exchange(true))
            return;

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
        dispatch(event);

        // Fire the core SafetyHook emergency callback (C2). Deliberately invoked
        // UNDER emergencyCallbackMutex_ (unlike user handlers): the mutex makes
        // setEmergencyCallback(nullptr) block until an in-flight dispatch
        // finishes, so the system can safely tear down after clearing the hook.
        // No deadlock: the callback never re-enters this manager (a re-entrant
        // triggerEmergencyStop early-returns on emergencyStopped_ above).
        {
            std::lock_guard<std::mutex> lock(emergencyCallbackMutex_);
            if (emergencyCallback_) {
                try {
                    emergencyCallback_(reason);
                } catch (...) {
                    // The e-stop has already been recorded and published; a
                    // throwing hook must not unwind out of whichever thread
                    // tripped it (C26).
                }
            }
        }
    }

    bool resetEmergencyStop() {
        if (!emergencyStopped_.load())
            return false;
        emergencyStopped_.store(false);

        SafetyEvent event;
        event.level = SafetyLevel::NOMINAL;
        event.source = "SafetyManager";
        event.description = "E-STOP reset";
        event.timestamp = std::chrono::steady_clock::now();
        dispatch(event);
        return true;
    }

    bool isEmergencyStopped() const override {
        return emergencyStopped_.load();
    }

    void setEmergencyCallback(core::SafetyHook::EmergencyCallback callback) override {
        std::lock_guard<std::mutex> lock(emergencyCallbackMutex_);
        emergencyCallback_ = std::move(callback);
    }

    // -----------------------------------------------------------------
    // Policy management
    // -----------------------------------------------------------------

    void addPolicy(std::unique_ptr<SafetyPolicy> policy) {
        if (!policy)
            return;
        std::lock_guard<std::mutex> lock(policiesMutex_);
        const std::string& name = policy->getName();
        policies_[name] = std::shared_ptr<SafetyPolicy>(std::move(policy));
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

    /**
     * @brief Evaluate every enabled policy, publishing events for violations.
     *
     * No manager lock is held while user code runs (C12): a policy's
     * evaluate() or an event handler may call addPolicy/removePolicy/
     * triggerEmergencyStop without deadlocking. The snapshot holds a
     * shared_ptr per policy, so a policy removed mid-cycle stays alive until
     * the cycle finishes.
     *
     * A re-entrant call (from a policy or handler on the thread running the
     * cycle) evaluates nothing and returns a partial result: the worst level
     * of the policies evaluated so far, in unordered iteration order. A
     * not-yet-evaluated policy reporting worse is not reflected.
     */
    SafetyLevel evaluateAll() {
        // Re-entry from this thread's own cycle (a policy or handler calling
        // back into evaluateAll) neither recurses — it would re-run every
        // policy and re-dispatch every event until the stack is gone — nor
        // blocks on evaluationMutex_, which this thread already holds. It
        // reports the worst level the in-progress cycle has seen so far.
        if (evaluatingThread_.load() == std::this_thread::get_id()) {
            return cycleWorst_.load();
        }

        // Serializes evaluation cycles (periodic loop vs. on-demand callers) so
        // a policy's evaluate() is never re-entered concurrently. Never taken
        // together with policiesMutex_, and never held by a thread that is not
        // running a cycle.
        std::lock_guard<std::mutex> cycleLock(evaluationMutex_);
        CycleOwner owner(*this);

        std::vector<std::pair<std::string, std::shared_ptr<SafetyPolicy>>> snapshot;
        {
            std::lock_guard<std::mutex> lock(policiesMutex_);
            snapshot.reserve(policies_.size());
            for (const auto& kv : policies_) {
                snapshot.emplace_back(kv.first, kv.second);
            }
        }

        SafetyLevel worst = SafetyLevel::NOMINAL;
        for (const auto& entry : snapshot) {
            SafetyPolicy* policy = entry.second.get();
            if (!policy || !policy->isEnabled())
                continue;

            // A policy that throws must not take the process down (C26), and
            // must not be quietly skipped either: a policy that cannot report
            // is a policy that cannot vouch for the system, so treating the
            // throw as NOMINAL would be a silent failure of the exact kind this
            // subsystem exists to prevent. It is reported as CRITICAL — high
            // enough to surface through the normal event path and to dominate
            // the cycle's worst level, deliberately short of EMERGENCY so a
            // malfunctioning policy cannot trip the e-stop on its own.
            PolicyResult result;
            try {
                result = policy->evaluate();
            } catch (const std::exception& e) {
                result.level = SafetyLevel::CRITICAL;
                result.description = "policy evaluate() threw: " + std::string(e.what());
            } catch (...) {
                result.level = SafetyLevel::CRITICAL;
                result.description = "policy evaluate() threw a non-standard exception";
            }

            if (result.level > worst) {
                worst = result.level;
                cycleWorst_.store(worst);
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
                event.source = entry.first;
                event.description = result.description;
                event.timestamp = std::chrono::steady_clock::now();
                dispatch(event);
            }

            if (result.level == SafetyLevel::EMERGENCY) {
                triggerEmergencyStop(entry.first + ": " + result.description);
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

    void registerHandler(Handler* handler) {
        if (!handler)
            return;
        std::lock_guard<std::mutex> lock(handlersMutex_);
        handlers_.push_back(handler);
    }

    bool unregisterHandler(Handler* handler) {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        auto it = std::find(handlers_.begin(), handlers_.end(), handler);
        if (it == handlers_.end())
            return false;
        handlers_.erase(it);
        return true;
    }

    // -----------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------

    void setEvaluationPeriod(Duration d) {
        evaluationPeriod_.store(d);
    }

    Duration getEvaluationPeriod() const {
        return evaluationPeriod_.load();
    }

    // -----------------------------------------------------------------
    // Statistics
    // -----------------------------------------------------------------

    SafetyManagerStatistics getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }

  private:
    /// Publishes the thread running the current evaluation cycle (and resets
    /// the cycle's worst level) so a re-entrant evaluateAll() can return
    /// instead of deadlocking. Scoped, because a policy's evaluate() may throw.
    class CycleOwner {
      public:
        explicit CycleOwner(SafetyManager& mgr) : mgr_(mgr) {
            mgr_.cycleWorst_.store(SafetyLevel::NOMINAL);
            mgr_.evaluatingThread_.store(std::this_thread::get_id());
        }
        ~CycleOwner() {
            mgr_.evaluatingThread_.store(std::thread::id());
        }
        CycleOwner(const CycleOwner&) = delete;
        CycleOwner& operator=(const CycleOwner&) = delete;

      private:
        SafetyManager& mgr_;
    };

    /// Snapshot-then-dispatch (C12/C18): handlers run with no lock held, so a
    /// handler may register/unregister handlers or touch the policy registry.
    /// A handler unregistered during dispatch still receives the current event.
    void dispatch(const SafetyEvent& event) noexcept {
        std::vector<Handler*> handlers;
        {
            std::lock_guard<std::mutex> lock(handlersMutex_);
            handlers = handlers_;
        }
        for (Handler* handler : handlers) {
            if (!handler)
                continue;
            try {
                handler->callbackPerform(event);
            } catch (...) {
                // A misbehaving handler must not stop the others.
            }
        }
    }

    void evaluationLoop() {
        while (running_.load()) {
            if (!emergencyStopped_.load()) {
                // Backstop (C26). evaluateAll already contains every call into
                // user code and guards each one, so nothing should reach here —
                // but this is a thread entry, and an escaping exception means
                // std::terminate, taking down the process whose safety this
                // subsystem is meant to preserve. The loop keeps running: a
                // failed cycle is not a reason to stop evaluating, and the
                // per-policy handling above has already published the fault.
                try {
                    evaluateAll();
                } catch (...) {}
            }
            std::this_thread::sleep_for(evaluationPeriod_.load());
        }
    }

    std::atomic<Duration> evaluationPeriod_;
    std::atomic<bool> running_{false};
    std::atomic<bool> emergencyStopped_{false};
    std::thread worker_;
    /// Published by the worker itself; see stop().
    std::atomic<std::thread::id> workerId_{std::thread::id()};

    mutable std::mutex evaluationMutex_;
    std::atomic<std::thread::id> evaluatingThread_{std::thread::id()};
    std::atomic<SafetyLevel> cycleWorst_{SafetyLevel::NOMINAL};

    mutable std::mutex policiesMutex_;
    std::unordered_map<std::string, std::shared_ptr<SafetyPolicy>> policies_;

    mutable std::mutex handlersMutex_;
    std::vector<Handler*> handlers_;

    mutable std::mutex statsMutex_;

    // Core SafetyHook emergency callback (C2)
    core::SafetyHook::EmergencyCallback emergencyCallback_;
    mutable std::mutex emergencyCallbackMutex_;
    SafetyManagerStatistics stats_;
};

} // namespace axonvex::safety
