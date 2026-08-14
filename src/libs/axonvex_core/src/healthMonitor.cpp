#include <algorithm>
#include <axonvex_core/healthMonitor.hpp>
#include <axonvex_core/logger.hpp>
#include <chrono>
#include <thread>

namespace axonvex::core {

HealthMonitor::HealthMonitor(const SystemConfiguration& cfg, Predicate shutdownRequested,
                             EventBus& events, LoggerAccessor logger, Providers providers,
                             RecoveryHook onCriticalHealth, std::function<void()> onLoopError,
                             std::function<void()> updateStatistics)
    : cfg_(cfg), shutdownRequested_(std::move(shutdownRequested)), events_(events),
      logger_(std::move(logger)), providers_(std::move(providers)),
      onCriticalHealth_(std::move(onCriticalHealth)), onLoopError_(std::move(onLoopError)),
      updateStatistics_(std::move(updateStatistics)) {}

void HealthMonitor::start() {
    // C46-shape: enabled_ is flipped inside WorkerThread::start()'s
    // beforeSpawn hook, strictly after the stale-handle join and strictly
    // before the new thread exists -- see workerThread.hpp for why that
    // ordering is load-bearing.
    monitorThread_.start([this] { monitoringLoop(); }, [this] { enabled_.store(true); });
}

bool HealthMonitor::join() {
    return monitorThread_.join();
}

void HealthMonitor::requestStop() noexcept {
    enabled_.store(false);
}

bool HealthMonitor::stopAndJoin() {
    requestStop();
    return monitorThread_.join();
}

bool HealthMonitor::isOnMonitorThread() const noexcept {
    return monitorThread_.isOnThisThread();
}

uint32_t HealthMonitor::registerHealthCallback(HealthCheckCallback cb) {
    if (!cb) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(callbacksMutex_);
    uint32_t id = nextCallbackId_.fetch_add(1);

    if (healthCallbacks_.size() <= id) {
        healthCallbacks_.resize(id + 1);
    }
    healthCallbacks_[id] = std::move(cb);

    return id;
}

void HealthMonitor::resetCallbacks() noexcept {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    healthCallbacks_.clear();
    nextCallbackId_.store(1);
}

SystemHealth HealthMonitor::check() const {
    SystemHealth health;
    health.lastCheckTime = std::chrono::steady_clock::now();

    SystemState currentState = providers_.getState();
    if (currentState == SystemState::ERROR || currentState == SystemState::FATAL_ERROR) {
        health.overallStatus = SystemHealth::Status::FAILURE;
        health.errors.push_back("System is in error state");
    }

    health.timingControllerHealthy = providers_.timingControllerHealthy();
    health.configurationHealthy = providers_.configurationHealthy();
    health.loggerHealthy = providers_.loggerHealthy();

    size_t memUsage = providers_.getMemoryUsage();
    size_t maxMem = cfg_.maxMemoryPoolSize;
    health.memoryUtilization = static_cast<double>(memUsage) / static_cast<double>(maxMem);

    if (health.memoryUtilization > 0.9) {
        health.overallStatus = std::max(health.overallStatus, SystemHealth::Status::CRITICAL);
        health.errors.push_back("Memory utilization critical: " +
                                std::to_string(static_cast<int>(health.memoryUtilization * 100)) +
                                "%");
    } else if (health.memoryUtilization > 0.75) {
        health.overallStatus = std::max(health.overallStatus, SystemHealth::Status::WARNING);
        health.warnings.push_back("Memory utilization high: " +
                                  std::to_string(static_cast<int>(health.memoryUtilization * 100)) +
                                  "%");
    }

    double successRate = providers_.getSuccessRate();
    if (successRate < 0.9) {
        health.overallStatus = std::max(health.overallStatus, SystemHealth::Status::WARNING);
        health.warnings.push_back(
            "Success rate low: " + std::to_string(static_cast<int>(successRate * 100)) + "%");
    }

    health.memoryHealthy = (health.memoryUtilization < 0.95);

    return health;
}

void HealthMonitor::performHealthCheck() {
    healthCheckTimer_.start();

    SystemHealth health = check();

    // Snapshot under the lock, invoke outside it: user callbacks must never
    // run while callbacksMutex_ is held (C18 -- re-entrant callback API use
    // deadlocks).
    std::vector<HealthCheckCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        callbacks = healthCallbacks_;
    }

    for (const auto& callback : callbacks) {
        if (callback) {
            try {
                SystemHealth userHealth = callback();
                if (userHealth.overallStatus > health.overallStatus) {
                    health.overallStatus = userHealth.overallStatus;
                }
                health.warnings.insert(health.warnings.end(), userHealth.warnings.begin(),
                                       userHealth.warnings.end());
                health.errors.insert(health.errors.end(), userHealth.errors.begin(),
                                     userHealth.errors.end());
            } catch (...) { health.errors.push_back("Health check callback failed"); }
        }
    }

    healthCheckTimer_.stop();

    SystemEvent event;
    event.type = SystemEvent::Type::HEALTH_CHECK;
    event.oldState = providers_.getState();
    event.newState = event.oldState;
    event.description = "Health check completed: " + health.getStatusString();
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["overall_status"] = health.getStatusString();
    event.metadata["warning_count"] = std::to_string(health.warnings.size());
    event.metadata["error_count"] = std::to_string(health.errors.size());
    event.metadata["duration_ms"] = std::to_string(healthCheckTimer_.getElapsedMilliseconds());

    events_.publish(event);

    if (health.overallStatus == SystemHealth::Status::CRITICAL ||
        health.overallStatus == SystemHealth::Status::FAILURE) {

        if (cfg_.enableAutoRecovery) {
            std::string errorDesc = "Health check failed: " + health.getStatusString();
            for (const auto& error : health.errors) {
                errorDesc += "\n- " + error;
            }
            if (onCriticalHealth_) {
                onCriticalHealth_(errorDesc);
            }
        }
    }
}

void HealthMonitor::monitoringLoop() {
    // C41: the thread-id publish (first act) / clear (last, every exit
    // path) happens automatically inside detail::WorkerThread::start() --
    // isOnMonitorThread() reads it via monitorThread_.isOnThisThread().

    auto lastUpdate = std::chrono::steady_clock::now();
    // Loop-local timer: an unlocked read of a shared epoch here would race
    // performHealthCheck() calls arriving from other threads (C18) -- this
    // local never leaves the loop.
    auto lastHealthCheck = lastUpdate;

    // C38: merges the old `monitoringEnabled_.load() && !isShuttingDown_.load()`
    // while-condition and the loop-body's `currentState_.load() >=
    // SystemState::STOPPING` break into one predicate call -- same net
    // effect, restated as a single call per the design spec.
    while (enabled_.load() && !shutdownRequested_()) {
        try {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate);

            if (elapsed >= cfg_.statisticsUpdateInterval) {
                updateStatistics_();
                lastUpdate = now;
            }

            auto healthElapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - lastHealthCheck);

            if (healthElapsed >= cfg_.healthCheckInterval) {
                performHealthCheck();
                lastHealthCheck = std::chrono::steady_clock::now();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (const std::exception& e) {
            if (Logger* logger = logger_()) {
                logger->error("HealthMonitor", "Monitoring loop error: " + std::string(e.what()));
            }
            if (onLoopError_) {
                onLoopError_();
            }
        }
    }
}

} // namespace axonvex::core
