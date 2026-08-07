/**
 * @file system.cpp
 * @brief AxonVex System Management and Lifecycle Control Implementation
 *
 * Usage documentation for core utilities:
 * - Use systemTimer_ for timing system-level operations (init, shutdown, health checks).
 * - Use ThreadSafeQueue (e.g., eventQueue_) for event/message passing or deferred actions.
 * - Use MemoryPool (e.g., eventPool_) for real-time safe allocation of system event objects.
 * These members are available in the AxonVexSystem base class for use in implementation and
 * extensions.
 */

#include <algorithm>
#include <axonvex_core/safetyHook.hpp>
#include <axonvex_core/system.hpp>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#elif _WIN32
#include <psapi.h>
#include <windows.h>
#endif

namespace axonvex::core {

// =================================================================
// SYSTEM CONFIGURATION IMPLEMENTATION
// =================================================================

void SystemConfiguration::validate() const {
    if (systemName.empty()) {
        throw std::invalid_argument("System name cannot be empty");
    }

    if (version.empty()) {
        throw std::invalid_argument("Version cannot be empty");
    }

    if (maxProcessingUnits == 0) {
        throw std::invalid_argument("Max processing units must be greater than 0");
    }

    if (maxMemoryPoolSize < 1024 * 1024) { // Minimum 1MB
        throw std::invalid_argument("Max memory pool size must be at least 1MB");
    }

    if (loggerQueueSize < 1024) {
        throw std::invalid_argument("Logger queue size must be at least 1024");
    }

    if (statisticsUpdateInterval.count() <= 0) {
        throw std::invalid_argument("Statistics update interval must be positive");
    }

    if (healthCheckInterval.count() <= 0) {
        throw std::invalid_argument("Health check interval must be positive");
    }
}

std::string SystemConfiguration::toString() const {
    std::ostringstream oss;
    oss << "SystemConfiguration:\n";
    oss << "  Name: " << systemName << "\n";
    oss << "  Version: " << version << "\n";
    oss << "  Log Level: " << static_cast<int>(logLevel) << "\n";
    oss << "  Scheduling Policy: " << static_cast<int>(defaultSchedulingPolicy) << "\n";
    oss << "  Tick Rate: " << systemTickRate.count() << " μs\n";
    oss << "  Real-time Scheduling: " << (enableRealTimeScheduling ? "Yes" : "No") << "\n";
    oss << "  Max Processing Units: " << maxProcessingUnits << "\n";
    oss << "  Max Memory Pool: " << (maxMemoryPoolSize / 1024 / 1024) << " MB\n";
    oss << "  Performance Monitoring: " << (enablePerformanceMonitoring ? "Yes" : "No") << "\n";
    oss << "  Auto Recovery: " << (enableAutoRecovery ? "Yes" : "No") << "\n";
    return oss.str();
}

// =================================================================
// SYSTEM STATISTICS IMPLEMENTATION
// =================================================================

void SystemStatistics::reset() {
    totalProcessingUnits.store(0);
    activeProcessingUnits.store(0);
    totalExecutions.store(0);
    successfulExecutions.store(0);
    failedExecutions.store(0);
    totalMissedDeadlines.store(0);
    memoryUsageBytes.store(0);
    peakMemoryUsageBytes.store(0);
    totalStateTransitions.store(0);
    errorCount.store(0);
    recoveryAttempts.store(0);
    successfulRecoveries.store(0);

    auto now = std::chrono::steady_clock::now();
    startTime = now;
    lastUpdateTime = now;
}

std::string SystemStatistics::getReport() const {
    std::ostringstream oss;
    oss << "System Statistics:\n";
    oss << "  Processing Units: " << totalProcessingUnits.load() << " total, "
        << activeProcessingUnits.load() << " active\n";
    oss << "  Executions: " << totalExecutions.load() << " total, " << successfulExecutions.load()
        << " successful, " << failedExecutions.load() << " failed\n";
    oss << "  Success Rate: " << std::fixed << std::setprecision(2) << (getSuccessRate() * 100.0)
        << "%\n";
    oss << "  Missed Deadlines: " << totalMissedDeadlines.load() << "\n";
    oss << "  Memory Usage: " << (memoryUsageBytes.load() / 1024 / 1024)
        << " MB (Peak: " << (peakMemoryUsageBytes.load() / 1024 / 1024) << " MB)\n";
    oss << "  State Transitions: " << totalStateTransitions.load() << "\n";
    oss << "  Errors: " << errorCount.load() << "\n";
    oss << "  Recovery: " << successfulRecoveries.load() << "/" << recoveryAttempts.load()
        << " attempts\n";
    oss << "  Uptime: " << std::fixed << std::setprecision(1) << getUptimeSeconds() << " seconds\n";
    return oss.str();
}

double SystemStatistics::getUptimeSeconds() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime);
    return duration.count() / 1000000.0;
}

double SystemStatistics::getSuccessRate() const {
    auto total = totalExecutions.load();
    if (total == 0)
        return 1.0;
    return static_cast<double>(successfulExecutions.load()) / static_cast<double>(total);
}

// =================================================================
// SYSTEM HEALTH IMPLEMENTATION
// =================================================================

std::string SystemHealth::getStatusString() const {
    switch (overallStatus) {
        case Status::HEALTHY:
            return "HEALTHY";
        case Status::WARNING:
            return "WARNING";
        case Status::CRITICAL:
            return "CRITICAL";
        case Status::FAILURE:
            return "FAILURE";
        default:
            return "UNKNOWN";
    }
}

// =================================================================
// AXONVEX SYSTEM IMPLEMENTATION
// =================================================================

AxonVexSystem::AxonVexSystem(const SystemConfiguration& config)
    : systemConfig_(config), unitRegistry_(systemConfig_.maxProcessingUnits) {

    // Validate configuration
    try {
        systemConfig_.validate();
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid system configuration: " + std::string(e.what()));
    }

    // Initialize statistics
    statistics_.reset();
}

AxonVexSystem::~AxonVexSystem() {
    // Clear the hook registration first: it holds a callback capturing `this`.
    // SafetyManager dispatches under its callback mutex, so this blocks until
    // any in-flight emergency dispatch has finished (C2).
    if (safetyHook_) {
        safetyHook_->setEmergencyCallback(nullptr);
        safetyHook_ = nullptr;
    }
    if (currentState_.load() != SystemState::UNINITIALIZED &&
        currentState_.load() != SystemState::STOPPED) {
        emergencyShutdown();
    }
    // If a concurrent emergencyShutdown owned the teardown (our call above
    // skipped via try_lock), wait for it to finish before members are
    // destroyed. This only synchronizes with mutex-owning teardowns: a
    // deferred self-join emergencyShutdown (running ON eventProcessingThread_/
    // monitoringThread_) releases the mutex via try_lock's early return while
    // still unwinding user-code frames on that thread — the joins below cover
    // that window. Legal here (no self-join risk): a worker thread cannot
    // reach the destructor without going through initialize()/stop()/reset(),
    // all of which refuse on isOnWorkerThread().
    { std::lock_guard<std::mutex> wait(shutdownMutex_); }
    joinAndClearThreadHandle(eventProcessingThread_);
    joinAndClearThreadHandle(monitoringThread_);
}

// =================================================================
// SYSTEM LIFECYCLE MANAGEMENT
// =================================================================

bool AxonVexSystem::initialize(const std::string& configPath) {
    // C41: lifecycle teardown destroys components this worker's own stack is
    // using; refusal is the only honest behavior. emergencyShutdown() remains
    // the sanctioned from-any-thread path (it stops but never destroys).
    // Must be the first statement: everything below (including the C39
    // joins just after this) assumes it is not running on eventThreadId_/
    // monitoringThreadId_.
    if (isOnWorkerThread()) {
        if (logger_) {
            logger_->error("System", "initialize() called from a system worker thread -- refused. "
                                     "Use emergencyShutdown() from callbacks.");
        }
        return false;
    }

    // C39: join and clear any stale thread handles BEFORE
    // initializeComponents() below replaces logger_/eventPool_/eventQueue_/
    // timingController_. A stale thread can still be running here — from
    // emergencyShutdown's deferred self-join (it runs on the very thread it
    // would otherwise join, so it skips the join and leaves the handle set,
    // C33/C36) or from a throw between a previous initialize()'s thread
    // start and its INITIALIZED transition (the catch path's
    // cleanupComponents() never touches thread handles) — and that stale
    // thread dereferences logger_/timingController_ inside
    // emergencyShutdown()/monitoringLoop()/eventProcessingLoop(). Freeing
    // those objects out from under it (by calling initializeComponents()
    // first) is a use-after-free window that reset()'s sleep_for(100ms)
    // only ever masked. This join must stay above initializeComponents().
    //
    // Ordering vs the C38 flag-clear further down is load-bearing, and the
    // argument holds even more cleanly here at the top: no flags have been
    // touched yet and currentState_ is still whatever the last
    // stop()/emergencyShutdown() left it (FATAL_ERROR/UNINITIALIZED), so any
    // stale thread's loop condition is still guaranteed to be exiting (or
    // already exited). Clearing isShuttingDown_ or setting the per-thread
    // run flags true BEFORE this join would let a stale loop observe
    // "revived" flags and keep running instead of exiting, turning the join
    // below into a potential indefinite block. Join both handles first,
    // THEN initialize components, THEN clear/set flags, THEN start new
    // threads.
    joinAndClearThreadHandle(monitoringThread_);
    joinAndClearThreadHandle(eventProcessingThread_);

    // Initialize core components first to ensure they're ready for use
    if (!initializeComponents()) {
        logStateTransition(SystemState::UNINITIALIZED, SystemState::ERROR);
        return false;
    }

    if (!transitionState(SystemState::INITIALIZING)) {
        return false;
    }

    systemTimer_.start(); // Start timing initialization

    try {
        // Load configuration if path provided
        if (!configPath.empty()) {
            Path configFilePath(configPath);
            if (!loadConfiguration(configFilePath)) {
                logStateTransition(SystemState::INITIALIZING, SystemState::ERROR);
                return false;
            }
        }

        // Initialize the processing blocks layout (implemented by derived classes)
        if (!initializeBlocksLayout()) {
            if (logger_) {
                logger_->error("System", "Block layout initialization failed");
            }
            logStateTransition(SystemState::INITIALIZING, SystemState::ERROR);
            cleanupComponents();
            return false;
        }

        // C38: initialize() is the single owner of clearing the shutdown
        // latch. stop()/emergencyShutdown() set it; nothing else may clear
        // it. This does NOT decide a race against a concurrent e-stop:
        // initialize() overwrites isShuttingDown_, monitoringEnabled_ and
        // eventProcessingRunning_ unconditionally regardless of who wins.
        // What actually guarantees the e-stop wins is transitionState():
        // emergencyShutdown() sets currentState_ to FATAL_ERROR (legal from
        // any state), which trips the `currentState_.load() >=
        // SystemState::STOPPING` guards in monitoringLoop()/
        // eventProcessingLoop() even for threads this call is about to
        // start, while this call's own transitionState(INITIALIZED) is
        // refused (FATAL_ERROR only accepts a transition to UNINITIALIZED),
        // so initialize() itself returns false. Those >= STOPPING guards
        // are load-bearing for this invariant — do not remove or weaken
        // them.
        isShuttingDown_.store(false);

        // Start monitoring if enabled
        if (systemConfig_.enablePerformanceMonitoring) {
            monitoringEnabled_.store(true);
            monitoringThread_ = std::make_unique<std::thread>(&AxonVexSystem::monitoringLoop, this);
        }

        // Start event processing thread
        eventProcessingRunning_.store(true);
        eventProcessingThread_ =
            std::make_unique<std::thread>(&AxonVexSystem::eventProcessingLoop, this);

        // Transition to initialized state
        if (!transitionState(SystemState::INITIALIZED)) {
            cleanupComponents();
            return false;
        }

        systemTimer_.stop(); // Stop timing initialization

        // Log successful initialization
        if (logger_) {
            logger_->info("System", "AxonVex System initialized successfully in " +
                                        std::to_string(systemTimer_.getElapsedMilliseconds()) +
                                        " ms");
            logger_->info("System", systemConfig_.toString());
        }

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Initialization failed: " + std::string(e.what()));
        }

        cleanupComponents();
        transitionState(SystemState::ERROR);
        return false;
    }
}

bool AxonVexSystem::start() {
    if (!transitionState(SystemState::STARTING)) {
        return false;
    }

    try {
        // Start all components
        if (!startComponents()) {
            transitionState(SystemState::ERROR);
            return false;
        }

        // Mark start time for statistics
        statistics_.startTime = std::chrono::steady_clock::now();
        statistics_.lastUpdateTime = statistics_.startTime;

        // Transition to running state
        if (!transitionState(SystemState::RUNNING)) {
            stopComponents(std::chrono::milliseconds(1000));
            return false;
        }

        if (logger_) {
            logger_->info("System", "AxonVex System started and running");
        }

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Start failed: " + std::string(e.what()));
        }

        transitionState(SystemState::ERROR);
        return false;
    }
}

bool AxonVexSystem::pause() {
    if (!transitionState(SystemState::PAUSING)) {
        return false;
    }

    try {
        // Pause all components
        if (!pauseComponents()) {
            transitionState(SystemState::ERROR);
            return false;
        }

        // Transition to paused state
        if (!transitionState(SystemState::PAUSED)) {
            return false;
        }

        if (logger_) {
            logger_->info("System", "AxonVex System paused");
        }

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Pause failed: " + std::string(e.what()));
        }

        transitionState(SystemState::ERROR);
        return false;
    }
}

bool AxonVexSystem::resume() {
    if (!transitionState(SystemState::RESUMING)) {
        return false;
    }

    try {
        // Resume all components
        if (!resumeComponents()) {
            transitionState(SystemState::ERROR);
            return false;
        }

        // Transition back to running state
        if (!transitionState(SystemState::RUNNING)) {
            return false;
        }

        if (logger_) {
            logger_->info("System", "AxonVex System resumed");
        }

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Resume failed: " + std::string(e.what()));
        }

        transitionState(SystemState::ERROR);
        return false;
    }
}

bool AxonVexSystem::stop(std::chrono::milliseconds timeoutMs) {
    // C40: stop() joins the worker threads and destroys components — it
    // cannot run on a worker thread (the old path self-joined, threw, and
    // silently escalated to emergencyShutdown/FATAL_ERROR). Refuse before
    // the STOPPING transition so a refused stop leaves the state untouched.
    if (isOnWorkerThread()) {
        if (logger_) {
            logger_->error("System", "stop() called from a system worker thread -- refused. "
                                     "Use emergencyShutdown() from callbacks.");
        }
        return false;
    }

    if (!transitionState(SystemState::STOPPING)) {
        return false;
    }

    isShuttingDown_.store(true); // Signal shutdown to all threads
    systemTimer_.start();        // Start timing shutdown

    try {
        // Thread-handle teardown serialized against emergencyShutdown (C2),
        // which may fire concurrently from any thread via the SafetyHook.
        // Flags are set before locking so a system thread that enters
        // emergencyShutdown (try_lock fails) still exits its loop promptly.
        eventProcessingRunning_.store(false);
        monitoringEnabled_.store(false);
        // Held through component stop/cleanup as well: a hook-triggered
        // emergencyShutdown must not touch timingController_ while
        // cleanupComponents() resets it (it try_locks and skips instead)
        std::unique_lock<std::mutex> teardownLock(shutdownMutex_);
        // Stop event processing thread first
        if (eventProcessingThread_ && eventProcessingThread_->joinable()) {
            eventProcessingThread_->join();
            eventProcessingThread_.reset();
            drainEventQueue();
        }
        // Stop monitoring thread next
        if (monitoringThread_ && monitoringThread_->joinable()) {
            monitoringThread_->join();
            monitoringThread_.reset();
        }
        // Now stop all components
        if (!stopComponents(timeoutMs)) {
            // Release first: emergencyShutdown try_locks this mutex and must
            // own the teardown here, not skip it
            teardownLock.unlock();
            emergencyShutdown();
            return false;
        }
        // Finally, clean up resources
        cleanupComponents();
        teardownLock.unlock();
        // Transition to stopped state
        if (!transitionState(SystemState::STOPPED)) {
            return false;
        }
        systemTimer_.stop(); // Stop timing shutdown
        if (logger_) {
            logger_->info("System", "AxonVex System stopped gracefully in " +
                                        std::to_string(systemTimer_.getElapsedMilliseconds()) +
                                        " ms");
        }
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Stop failed: " + std::string(e.what()));
        }
        emergencyShutdown();
        return false;
    }
}

void AxonVexSystem::emergencyShutdown() {
    // C7: validated transition (legal from any state). Runs before
    // isShuttingDown_ is set so the STATE_CHANGE event isn't dropped
    // by publishEvent's shutdown guard.
    transitionState(SystemState::FATAL_ERROR);
    isShuttingDown_.store(true);
    eventProcessingRunning_.store(false);
    monitoringEnabled_.store(false);

    // Single-owner teardown: e-stop can fire from any thread via the SafetyHook
    // (C2), racing stop() or another e-stop on the thread handles. try_lock, not
    // lock: a system thread entering here while stop() joins it under the mutex
    // must return (flags above end its loop) or the join would deadlock. The
    // teardown owner finishes the joins; the destructor waits on this mutex.
    std::unique_lock<std::mutex> teardownLock(shutdownMutex_, std::try_to_lock);
    if (!teardownLock.owns_lock()) {
        return;
    }

    // Stop event processing thread first. Self-join guard: the emergency
    // callback may run on a system thread; skip the join and reset there —
    // the destructor's second pass joins from the owner thread.
    if (eventProcessingThread_ && eventProcessingThread_->joinable() &&
        std::this_thread::get_id() != eventThreadId_.load()) {
        eventProcessingThread_->join();
        eventProcessingThread_.reset();
    }
    // Unconditional: events can be queued before the event thread ever starts
    // (e.g. registerProcessingUnit during a failed initializeBlocksLayout) and
    // would otherwise leak when the pool is torn down (C25)
    drainEventQueue();
    // Stop monitoring thread next
    if (monitoringThread_ && monitoringThread_->joinable() &&
        std::this_thread::get_id() != monitoringThreadId_.load()) {
        monitoringThread_->join();
        monitoringThread_.reset();
    }
    // Force stop all components immediately
    try {
        if (timingController_) {
            timingController_->stop();
        }
        // Clear all processing units. clear() releases unitRegistry_'s
        // internal lock before returning; the discarded unique_ptrs are
        // destroyed here, already outside that lock (C18-shape fix carried
        // from the UnitRegistry extraction).
        unitRegistry_.clear();
        if (logger_) {
            logger_->critical("System", "Emergency shutdown completed");
            logger_->flush();
        }
    } catch (...) {
        // Ignore all exceptions during emergency shutdown
    }
}

// =================================================================
// ADAPTER INJECTION IMPLEMENTATION
// =================================================================

void AxonVexSystem::addAdapter(axonvex::adapters::AdapterInterface* adapter,
                               const std::string& uri) {
    adapters_[uri] = adapter;
    if (logger_) {
        logger_->info("System", "Registered adapter: " + uri);
    }
}

axonvex::adapters::AdapterInterface* AxonVexSystem::getAdapter(const std::string& uri) const {
    auto it = adapters_.find(uri);
    if (it != adapters_.end()) {
        return it->second;
    }
    return nullptr;
}

// =================================================================
// SAFETY HOOK INJECTION IMPLEMENTATION
// =================================================================

void AxonVexSystem::setSafetyHook(SafetyHook* hook) {
    if (safetyHook_ && safetyHook_ != hook) {
        safetyHook_->setEmergencyCallback(nullptr);
    }
    safetyHook_ = hook;
    if (hook) {
        // C2: an engaged e-stop must actually halt the system
        hook->setEmergencyCallback([this](const std::string& reason) {
            if (logger_) {
                logger_->critical("System", "Safety e-stop: " + reason);
            }
            emergencyShutdown();
        });
    }
    if (logger_) {
        logger_->info("System", hook ? "SafetyHook registered" : "SafetyHook cleared");
    }
}

SafetyHook* AxonVexSystem::getSafetyHook() const {
    return safetyHook_;
}

void AxonVexSystem::reset() {
    // C40: reset() destroys timingController_/configuration_/logger_ that a
    // worker thread's own call stack may be using; a worker calling this
    // would tear down its own components mid-callback. Refuse — same
    // contract as initialize()/stop(). Must be the first statement: nothing
    // below is safe to run on eventThreadId_/monitoringThreadId_.
    if (isOnWorkerThread()) {
        if (logger_) {
            logger_->error("System", "reset() called from a system worker thread -- refused. "
                                     "Use emergencyShutdown() from callbacks.");
        }
        return;
    }

    emergencyShutdown();

    // C40: wait for an in-flight teardown (another thread may own
    // shutdownMutex_ — our emergencyShutdown() try_locks and skips in that
    // case) instead of guessing with a sleep. Safe from deadlock: worker
    // threads are refused above, so nobody joining US can hold this mutex.
    // This only synchronizes with mutex-owning teardowns, not a deferred
    // self-join emergencyShutdown: that variant runs ON eventProcessingThread_/
    // monitoringThread_ and releases the mutex via try_lock's early return
    // while still unwinding user-code frames on that thread. The joins below
    // close that window — legal here because worker threads can't reach this
    // point (refused above), so no self-join is possible.
    { std::lock_guard<std::mutex> wait(shutdownMutex_); }
    joinAndClearThreadHandle(eventProcessingThread_);
    joinAndClearThreadHandle(monitoringThread_);

    // Reset to uninitialized state
    // C7: FATAL_ERROR → UNINITIALIZED is already in the transition table.
    transitionState(SystemState::UNINITIALIZED);
    statistics_.reset();
    currentRecoveryAttempts_.store(0);

    // Clear all containers. Unlike emergencyShutdown()'s clear, reset() also
    // restarts the id counter at 1 (today's exact behavior, carried via the
    // two separate registry calls).
    unitRegistry_.clear();
    unitRegistry_.resetIds();

    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        eventCallbacks_.clear();
        healthCheckCallbacks_.clear();
        nextCallbackId_.store(1);
    }

    // Reset components
    timingController_.reset();
    configuration_.reset();
    logger_.reset();
}

// =================================================================
// STATE AND MONITORING IMPLEMENTATION
// =================================================================

SystemState AxonVexSystem::getState() const noexcept {
    return currentState_.load();
}

bool AxonVexSystem::isRunning() const noexcept {
    SystemState state = currentState_.load();
    return state == SystemState::RUNNING;
}

bool AxonVexSystem::isHealthy() const noexcept {
    SystemState state = currentState_.load();
    return state != SystemState::ERROR && state != SystemState::FATAL_ERROR;
}

double AxonVexSystem::getUptimeSeconds() const noexcept {
    return statistics_.getUptimeSeconds();
}

const SystemStatistics& AxonVexSystem::getStatistics() const noexcept {
    return statistics_;
}

SystemHealth AxonVexSystem::getHealth() const {
    return performInternalHealthCheck();
}

void AxonVexSystem::performHealthCheck() {
    systemTimer_.start(); // Start timing health check

    SystemHealth health = performInternalHealthCheck();

    // Snapshot under the lock, invoke outside it: user callbacks must never run
    // while callbacksMutex_ is held (C18 — re-entrant callback API use deadlocks).
    std::vector<HealthCheckCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        callbacks = healthCheckCallbacks_;
    }

    for (const auto& callback : callbacks) {
        if (callback) {
            try {
                SystemHealth userHealth = callback();
                // Merge user health with system health
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

    systemTimer_.stop(); // Stop timing health check

    // Publish health check event
    SystemEvent event;
    event.type = SystemEvent::Type::HEALTH_CHECK;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "Health check completed: " + health.getStatusString();
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["overall_status"] = health.getStatusString();
    event.metadata["warning_count"] = std::to_string(health.warnings.size());
    event.metadata["error_count"] = std::to_string(health.errors.size());
    event.metadata["duration_ms"] =
        std::to_string(systemTimer_.getElapsedMilliseconds()); // Add duration to metadata

    publishEvent(event);

    // Take action based on health status
    if (health.overallStatus == SystemHealth::Status::CRITICAL ||
        health.overallStatus == SystemHealth::Status::FAILURE) {

        if (systemConfig_.enableAutoRecovery) {
            std::string errorDesc = "Health check failed: " + health.getStatusString();
            for (const auto& error : health.errors) {
                errorDesc += "\n- " + error;
            }
            attemptRecovery(errorDesc);
        }
    }
}

// =================================================================
// PROCESSING UNIT MANAGEMENT
// =================================================================

uint32_t AxonVexSystem::registerProcessingUnit(std::unique_ptr<ProcessingUnit> unit,
                                               const TimingConstraints& constraints) {
    if (!unit) {
        throw std::invalid_argument("Processing unit cannot be null");
    }

    // unitRegistry_.add() throws std::runtime_error on capacity, matching the
    // pre-extraction check-then-insert ordering exactly (same exception type
    // and message).
    ProcessingUnit* unitPtr = unit.get();
    uint32_t unitId = unitRegistry_.add(std::move(unit));

    // Register with timing controller if running. NOTE (accepted narrow
    // window, ponytail: known ceiling): between unitRegistry_.add() above and
    // this call, unitId is externally reachable (getProcessingUnit()/
    // unregisterProcessingUnit()); the pre-extraction code closed this window
    // by holding unitsMutex_ across both steps, which UnitRegistry's
    // per-call locking cannot replicate without exposing its mutex. A
    // concurrent unregisterProcessingUnit(unitId) landing in this exact
    // window would hand scheduleProcessingUnit a freed unitPtr. No in-tree
    // caller unregisters a unit it hasn't already observed via
    // getAllProcessingUnits()/getProcessingUnit(), which cannot return
    // unitId until this call returns, so the window has no reachable
    // trigger today. Upgrade path if that changes: give UnitRegistry a
    // combined "add-and-run-under-lock" primitive.
    if (timingController_ && isRunning()) {
        try {
            TimingConstraints finalConstraints = constraints;
            if (finalConstraints.period.count() == 0) {
                // Use default constraints based on system tick rate
                finalConstraints.period = systemConfig_.systemTickRate;
                finalConstraints.deadline = finalConstraints.period;
                finalConstraints.wcet = finalConstraints.period / 10;
            }

            timingController_->scheduleProcessingUnit(unitPtr, finalConstraints);
        } catch (const std::exception& e) {
            // Remove the unit if scheduling failed. remove() returns
            // ownership; the temporary is destroyed here, after
            // unitRegistry_'s internal lock has already been released (C18-
            // shape fix carried from the UnitRegistry extraction).
            unitRegistry_.remove(unitId);
            throw;
        }
    }

    // Update statistics
    statistics_.totalProcessingUnits.fetch_add(1);
    statistics_.activeProcessingUnits.fetch_add(1);

    // Publish event
    SystemEvent event;
    event.type = SystemEvent::Type::PROCESSING_UNIT_ADDED;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "Processing unit registered: " + unitPtr->getName();
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_id"] = std::to_string(unitId);
    event.metadata["unit_name"] = unitPtr->getName();

    publishEvent(event);

    if (logger_) {
        logger_->info("System", "Registered processing unit: " + unitPtr->getName() +
                                    " (ID: " + std::to_string(unitId) + ")");
    }

    return unitId;
}

ProcessingUnit* AxonVexSystem::getProcessingUnit(uint32_t unitId) const {
    return unitRegistry_.find(unitId);
}

std::vector<ProcessingUnit*> AxonVexSystem::getAllProcessingUnits() const {
    return unitRegistry_.all();
}

size_t AxonVexSystem::getProcessingUnitCount() const noexcept {
    return statistics_.activeProcessingUnits.load();
}

// =================================================================
// SYSTEM PORT MANAGEMENT IMPLEMENTATION
// =================================================================

bool AxonVexSystem::assignSystemInputPort(const std::string& systemPortName, ProcessingUnit* unit,
                                          int unitPortId) {
    if (!unit || systemPortName.empty()) {
        if (logger_) {
            logger_->warning("System", "Invalid parameters for system input port assignment");
        }
        return false;
    }

    // Verify the unit is registered in this system
    if (!unitRegistry_.idOf(unit).has_value()) {
        if (logger_) {
            logger_->warning("System", "ProcessingUnit not registered in this system");
        }
        return false;
    }

    // Get the port from the ProcessingUnit (check input ports)
    BasePort* port = nullptr;

    // Check input ports first (most likely for system input assignment)
    auto& inputPorts = unit->getInputPorts();
    auto it = inputPorts.find(unitPortId);
    if (it != inputPorts.end()) {
        port = it->second;
    } else {
        // Also check async input ports
        auto& asyncInputPorts = unit->getAsyncInputPorts();
        auto asyncIt = asyncInputPorts.find(unitPortId);
        if (asyncIt != asyncInputPorts.end()) {
            port = asyncIt->second;
        }
    }

    if (!port) {
        if (logger_) {
            logger_->warning("System", "Input port " + std::to_string(unitPortId) +
                                           " not found in ProcessingUnit " + unit->getName());
        }
        return false;
    }

    // Check if system port name already exists / assign the port
    if (!systemPorts_.assignInput(systemPortName, port)) {
        if (logger_) {
            logger_->warning("System", "System input port '" + systemPortName + "' already exists");
        }
        return false;
    }

    if (logger_) {
        logger_->info("System", "Assigned system input port '" + systemPortName + "' from " +
                                    unit->getName() + ":" + std::to_string(unitPortId));
    }

    // Publish event
    SystemEvent event;
    event.type = SystemEvent::Type::CONFIGURATION_CHANGED;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "System input port assigned: " + systemPortName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["port_name"] = systemPortName;
    event.metadata["unit_name"] = unit->getName();
    event.metadata["unit_port_id"] = std::to_string(unitPortId);

    publishEvent(event);

    return true;
}

bool AxonVexSystem::assignSystemOutputPort(const std::string& systemPortName, ProcessingUnit* unit,
                                           int unitPortId) {
    if (!unit || systemPortName.empty()) {
        if (logger_) {
            logger_->warning("System", "Invalid parameters for system output port assignment");
        }
        return false;
    }

    // Verify the unit is registered in this system
    if (!unitRegistry_.idOf(unit).has_value()) {
        if (logger_) {
            logger_->warning("System", "ProcessingUnit not registered in this system");
        }
        return false;
    }

    // Get the port from the ProcessingUnit (check output ports)
    BasePort* port = nullptr;

    // Check output ports first (most likely for system output assignment)
    auto& outputPorts = unit->getOutputPorts();
    auto it = outputPorts.find(unitPortId);
    if (it != outputPorts.end()) {
        port = it->second;
    } else {
        // Also check async output ports
        auto& asyncOutputPorts = unit->getAsyncOutputPorts();
        auto asyncIt = asyncOutputPorts.find(unitPortId);
        if (asyncIt != asyncOutputPorts.end()) {
            port = asyncIt->second;
        }
    }

    if (!port) {
        if (logger_) {
            logger_->warning("System", "Output port " + std::to_string(unitPortId) +
                                           " not found in ProcessingUnit " + unit->getName());
        }
        return false;
    }

    // Check if system port name already exists / assign the port
    if (!systemPorts_.assignOutput(systemPortName, port)) {
        if (logger_) {
            logger_->warning("System",
                             "System output port '" + systemPortName + "' already exists");
        }
        return false;
    }

    if (logger_) {
        logger_->info("System", "Assigned system output port '" + systemPortName + "' from " +
                                    unit->getName() + ":" + std::to_string(unitPortId));
    }

    // Publish event
    SystemEvent event;
    event.type = SystemEvent::Type::CONFIGURATION_CHANGED;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "System output port assigned: " + systemPortName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["port_name"] = systemPortName;
    event.metadata["unit_name"] = unit->getName();
    event.metadata["unit_port_id"] = std::to_string(unitPortId);

    publishEvent(event);

    return true;
}

bool AxonVexSystem::removeSystemInputPort(const std::string& systemPortName) {
    if (!systemPorts_.removeInput(systemPortName)) {
        return false;
    }

    if (logger_) {
        logger_->info("System", "Removed system input port: " + systemPortName);
    }

    return true;
}

bool AxonVexSystem::removeSystemOutputPort(const std::string& systemPortName) {
    if (!systemPorts_.removeOutput(systemPortName)) {
        return false;
    }

    if (logger_) {
        logger_->info("System", "Removed system output port: " + systemPortName);
    }

    return true;
}

BasePort* AxonVexSystem::getSystemInputPort(const std::string& portName) const {
    return systemPorts_.input(portName);
}

BasePort* AxonVexSystem::getSystemOutputPort(const std::string& portName) const {
    return systemPorts_.output(portName);
}

std::vector<std::string> AxonVexSystem::getSystemInputPortNames() const {
    return systemPorts_.inputNames();
}

std::vector<std::string> AxonVexSystem::getSystemOutputPortNames() const {
    return systemPorts_.outputNames();
}

bool AxonVexSystem::hasSystemInputPort(const std::string& portName) const {
    return systemPorts_.hasInput(portName);
}

bool AxonVexSystem::hasSystemOutputPort(const std::string& portName) const {
    return systemPorts_.hasOutput(portName);
}

std::string AxonVexSystem::getSystemPortInfo() const {
    std::ostringstream oss;
    oss << "System Port Information for '" << systemConfig_.systemName << "':\n";

    // describeInputs()/describeOutputs() read name + type + owner under one
    // SystemPortRegistry::mutex_ hold, so there is no window for a
    // concurrent unregisterProcessingUnit() to free the port between lookup
    // and dereference. The old inputNames()+input()+deref two-step (each
    // step its own lock/unlock) had exactly that window — a
    // heap-use-after-free; regression test:
    // AxonVexSystemTest.GetSystemPortInfoDoesNotRaceUnregisterProcessingUnit.
    auto inputDescriptions = systemPorts_.describeInputs();
    oss << "Input Ports (" << inputDescriptions.size() << "):\n";
    for (const auto& desc : inputDescriptions) {
        oss << "  - " << desc.name;
        if (!desc.dataTypeName.empty() || !desc.ownerName.empty()) {
            oss << " (Type: " << desc.dataTypeName << ", Owner: " << desc.ownerName << ")";
        }
        oss << "\n";
    }

    auto outputDescriptions = systemPorts_.describeOutputs();
    oss << "Output Ports (" << outputDescriptions.size() << "):\n";
    for (const auto& desc : outputDescriptions) {
        oss << "  - " << desc.name;
        if (!desc.dataTypeName.empty() || !desc.ownerName.empty()) {
            oss << " (Type: " << desc.dataTypeName << ", Owner: " << desc.ownerName << ")";
        }
        oss << "\n";
    }

    return oss.str();
}

// =================================================================
// INTERNAL METHODS IMPLEMENTATION (Partial - Key Methods)
// =================================================================

bool AxonVexSystem::transitionState(SystemState newState) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    SystemState currentState = currentState_.load();

    // C7: an emergency shutdown is legal from every state — FATAL_ERROR is the
    // one transition that must never be refused.
    bool validTransition = false;
    if (newState == SystemState::FATAL_ERROR) {
        validTransition = true;
    } else {
        switch (currentState) {
            case SystemState::UNINITIALIZED:
                validTransition = (newState == SystemState::INITIALIZING);
                break;
            case SystemState::INITIALIZING:
                validTransition =
                    (newState == SystemState::INITIALIZED || newState == SystemState::ERROR);
                break;
            case SystemState::INITIALIZED:
                validTransition = (newState == SystemState::STARTING);
                break;
            case SystemState::STARTING:
                validTransition =
                    (newState == SystemState::RUNNING || newState == SystemState::ERROR);
                break;
            case SystemState::RUNNING:
                validTransition =
                    (newState == SystemState::PAUSING || newState == SystemState::STOPPING ||
                     newState == SystemState::ERROR);
                break;
            case SystemState::PAUSING:
                validTransition =
                    (newState == SystemState::PAUSED || newState == SystemState::ERROR);
                break;
            case SystemState::PAUSED:
                validTransition =
                    (newState == SystemState::RESUMING || newState == SystemState::STOPPING ||
                     newState == SystemState::ERROR);
                break;
            case SystemState::RESUMING:
                validTransition =
                    (newState == SystemState::RUNNING || newState == SystemState::ERROR);
                break;
            case SystemState::STOPPING:
                validTransition = (newState == SystemState::STOPPED);
                break;
            case SystemState::STOPPED:
                validTransition =
                    (newState == SystemState::STARTING || newState == SystemState::UNINITIALIZED);
                break;
            case SystemState::ERROR:
                validTransition = true; // Can transition to any state from error
                break;
            case SystemState::FATAL_ERROR:
                validTransition = (newState == SystemState::UNINITIALIZED);
                break;
        }
    }

    if (!validTransition) {
        if (logger_) {
            logger_->warning("System", "Invalid state transition from " +
                                           stateToString(currentState) + " to " +
                                           stateToString(newState));
        }
        return false;
    }

    // Perform the transition
    currentState_.store(newState);
    statistics_.totalStateTransitions.fetch_add(1);

    // Log and notify
    logStateTransition(currentState, newState);
    notifyStateChange(currentState, newState);

    return true;
}

bool AxonVexSystem::initializeComponents() {
    try {
        // Initialize logger first
        logger_ = std::make_unique<Logger>(systemConfig_.loggerQueueSize);
        logger_->setLevel(systemConfig_.logLevel);

        // Add console output
        auto consoleOutput = std::make_shared<ConsoleOutput>();
        logger_->addOutput(consoleOutput);

        // Add file output if enabled
        if (systemConfig_.enableFileLogging && !systemConfig_.logFilePath.empty()) {
            Path logPath(systemConfig_.logFilePath);
            logPath.parent().createDirectories();

            auto fileOutput = std::make_shared<FileOutput>(systemConfig_.logFilePath, true);
            logger_->addOutput(fileOutput);
        }

        logger_->start();

        // Initialize event queue and pool
        eventPool_ = std::make_unique<MemoryPool<SystemEvent>>(systemConfig_.eventPoolSize);
        eventQueue_ = std::make_unique<ThreadSafeQueue<SystemEvent*>>(systemConfig_.eventQueueSize);

        // Initialize configuration if not already set
        if (!configuration_) {
            configuration_ = std::make_unique<Configuration>();
        }

        // Initialize timing controller
        timingController_ =
            std::make_unique<TimingController>(systemConfig_.defaultSchedulingPolicy);

        // Set up simple error callback to update system error statistics
        timingController_->setErrorCallback([this](ProcessingUnit*, const std::string&) {
            this->statistics_.errorCount.fetch_add(1);
        });

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Component initialization failed: " + std::string(e.what()));
        }
        return false;
    }
}

bool AxonVexSystem::startComponents() {
    try {
        // Start timing controller first
        if (timingController_) {
            timingController_->start();
        }

        // Schedule all registered processing units. Snapshot outside any
        // registry lock (all() takes and releases its own internal lock),
        // then call into the timing controller with no registry lock held —
        // strictly narrower than the old unitsMutex_-held span, never wider.
        for (ProcessingUnit* unit : unitRegistry_.all()) {
            // Create default constraints
            TimingConstraints constraints;
            constraints.period = systemConfig_.systemTickRate;
            constraints.deadline = constraints.period;
            constraints.wcet = constraints.period / 10;

            try {
                timingController_->scheduleProcessingUnit(unit, constraints);
            } catch (const std::exception& e) {
                if (logger_) {
                    logger_->warning("System", "Failed to schedule processing unit " +
                                                   unit->getName() + ": " + e.what());
                }
            }
        }

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Component start failed: " + std::string(e.what()));
        }
        return false;
    }
}

namespace {
/**
 * C41: publishes the calling thread's id into `slot` on construction and
 * clears it (back to std::thread::id{}, the "no worker" sentinel
 * isOnWorkerThread() checks against) on destruction — every exit path of the
 * loop that owns the guard, including an exception escaping the loop body,
 * runs the clear. Ids are reusable once a thread exits, so a stale id left
 * behind after a loop ends could later alias an unrelated thread; the clear
 * is not optional cleanup, it is the correctness condition.
 */
class WorkerThreadIdGuard {
  public:
    explicit WorkerThreadIdGuard(std::atomic<std::thread::id>& slot) : slot_(slot) {
        slot_.store(std::this_thread::get_id());
    }
    ~WorkerThreadIdGuard() {
        slot_.store(std::thread::id{});
    }
    WorkerThreadIdGuard(const WorkerThreadIdGuard&) = delete;
    WorkerThreadIdGuard& operator=(const WorkerThreadIdGuard&) = delete;

  private:
    std::atomic<std::thread::id>& slot_;
};
} // namespace

void AxonVexSystem::monitoringLoop() {
    // C41: published first, cleared last (by the guard's destructor, on
    // every exit path) so isOnWorkerThread() can identify this thread.
    WorkerThreadIdGuard idGuard(monitoringThreadId_);

    auto lastUpdate = std::chrono::steady_clock::now();
    // Loop-local timer: the old unlocked read of lastHealth_.lastCheckTime raced
    // performHealthCheck() on other threads (C18).
    auto lastHealthCheck = lastUpdate;

    while (monitoringEnabled_.load() && !isShuttingDown_.load()) {
        try {
            // Guard against accessing resources during shutdown
            if (currentState_.load() >= SystemState::STOPPING) {
                break;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate);

            // Update statistics periodically
            if (elapsed >= systemConfig_.statisticsUpdateInterval) {
                updateStatistics();
                lastUpdate = now;
            }

            // Perform health check periodically
            auto healthElapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - lastHealthCheck);

            if (healthElapsed >= systemConfig_.healthCheckInterval) {
                performHealthCheck();
                lastHealthCheck = std::chrono::steady_clock::now();
            }

            // Sleep for a short interval
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (const std::exception& e) {
            if (logger_) {
                logger_->error("System", "Monitoring loop error: " + std::string(e.what()));
            }
            statistics_.errorCount.fetch_add(1);
        }
    }
}

SystemHealth AxonVexSystem::performInternalHealthCheck() const {
    SystemHealth health;
    health.lastCheckTime = std::chrono::steady_clock::now();

    // Check system state
    SystemState currentState = currentState_.load();
    if (currentState == SystemState::ERROR || currentState == SystemState::FATAL_ERROR) {
        health.overallStatus = SystemHealth::Status::FAILURE;
        health.errors.push_back("System is in error state");
    }

    // Check component health
    health.timingControllerHealthy = (timingController_ != nullptr);
    health.configurationHealthy = (configuration_ != nullptr);
    health.loggerHealthy = (logger_ != nullptr && logger_->isRunning());

    // Check resource usage
    size_t memUsage = getMemoryUsage();
    size_t maxMem = systemConfig_.maxMemoryPoolSize;
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

    // Check performance metrics
    auto successRate = statistics_.getSuccessRate();
    if (successRate < 0.9) {
        health.overallStatus = std::max(health.overallStatus, SystemHealth::Status::WARNING);
        health.warnings.push_back(
            "Success rate low: " + std::to_string(static_cast<int>(successRate * 100)) + "%");
    }

    health.memoryHealthy = (health.memoryUtilization < 0.95);

    return health;
}

std::string AxonVexSystem::stateToString(SystemState state) const {
    return to_string(state);
}

void AxonVexSystem::logStateTransition(SystemState from, SystemState to) {
    if (logger_) {
        logger_->info("System",
                      "State transition: " + stateToString(from) + " → " + stateToString(to));
    }
}

void AxonVexSystem::notifyStateChange(SystemState oldState, SystemState newState) {
    SystemEvent event;
    event.type = SystemEvent::Type::STATE_CHANGE;
    event.oldState = oldState;
    event.newState = newState;
    event.description =
        "System state changed from " + stateToString(oldState) + " to " + stateToString(newState);
    event.timestamp = std::chrono::steady_clock::now();

    publishEvent(event);
}

void AxonVexSystem::handleProcessingUnitError(ProcessingUnit* unit, const std::string& error) {
    if (!unit)
        return;

    // Set error state in the processing unit
    unit->setError(error);

    // Create and publish error event
    SystemEvent event;
    event.type = SystemEvent::Type::PROCESSING_UNIT_ERROR;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "ProcessingUnit '" + unit->getName() + "' error: " + error;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_name"] = unit->getName();
    event.metadata["error_message"] = error;

    publishEvent(event);

    // Attempt recovery if enabled
    if (systemConfig_.enableAutoRecovery) {
        attemptRecovery("ProcessingUnit error: " + error);
    }
}

void AxonVexSystem::drainEventQueue() noexcept {
    // C25: events still queued when the event thread exits would leak their
    // strings; return them to the pool without dispatching.
    if (!eventQueue_ || !eventPool_) {
        return;
    }
    while (true) {
        auto eventOpt = eventQueue_->tryDequeue(std::chrono::milliseconds(0));
        if (!eventOpt.has_value()) {
            break;
        }
        eventPool_->deallocateObject(eventOpt.value());
    }
}

void AxonVexSystem::joinAndClearThreadHandle(std::unique_ptr<std::thread>& handle) {
    // C41: callers must not run on the thread the handle names; initialize()
    // guarantees this via isOnWorkerThread() refusal. This helper used to
    // carry a self-join guard (detach instead of join) for exactly the
    // reinit-from-callback path initialize()'s refusal now closes before
    // ever reaching here — nothing else relied on it, so it is gone rather
    // than kept as unreachable defense-in-depth.
    if (handle && handle->joinable()) {
        handle->join();
    }
    handle.reset();
}

bool AxonVexSystem::isOnWorkerThread() const noexcept {
    const std::thread::id self = std::this_thread::get_id();
    // C42: extends coverage to the scheduler thread. Reading timingController_
    // (a unique_ptr the lifecycle methods replace/reset) without a lock is
    // safe here: every caller of isOnWorkerThread() is either a lifecycle
    // method checking this on entry, before it has mutated any component
    // (so timingController_ is exactly whatever the last successful
    // initialize()/reset() left it — there is no concurrent writer to race
    // on THIS thread's own read), or a call arriving from inside a
    // scheduler-thread task/error-callback, where timingController_ being
    // alive is implied by the call itself: the task is running inside
    // executeTask(), which is running inside the very TimingController this
    // thread belongs to, so it cannot have been reset out from under its own
    // live call stack.
    return self == eventThreadId_.load() || self == monitoringThreadId_.load() ||
           (timingController_ && timingController_->isOnSchedulerThread());
}

void AxonVexSystem::publishEvent(const SystemEvent& event) {
    // Components may not exist yet (e-stop on a never-initialized system).
    if (!eventPool_ || !eventQueue_) {
        return;
    }
    // C25: once shutdown begins the event thread stops draining the queue —
    // allocating here would leak the event's strings. Drop shutdown-time events.
    if (isShuttingDown_.load()) {
        return;
    }
    // Allocate an event from the pool
    SystemEvent* eventPtr = eventPool_->allocateObject(event);

    if (eventPtr) {
        // Enqueue the event for asynchronous processing
        if (!eventQueue_->enqueue(eventPtr)) {
            // Handle queue full case
            eventPool_->deallocateObject(eventPtr); // Deallocate back to pool
            if (logger_) {
                logger_->warning("System", "Event queue overflow, event dropped.");
            }
            statistics_.errorCount.fetch_add(1);
        }
    } else {
        // Handle pool exhaustion
        if (logger_) {
            logger_->warning("System", "Event pool exhausted, event dropped.");
        }
        statistics_.errorCount.fetch_add(1);
    }
}

void AxonVexSystem::eventProcessingLoop() {
    // C41: published first, cleared last (by the guard's destructor, on
    // every exit path) so isOnWorkerThread() can identify this thread.
    WorkerThreadIdGuard idGuard(eventThreadId_);

    while (eventProcessingRunning_.load() && !isShuttingDown_.load()) {
        // Guard against accessing resources during shutdown
        if (currentState_.load() >= SystemState::STOPPING) {
            break;
        }

        // Dequeue an event with a timeout to allow for graceful shutdown
        auto eventOpt = eventQueue_->tryDequeue(std::chrono::milliseconds(100));

        if (eventOpt.has_value()) {
            SystemEvent* eventPtr = eventOpt.value();

            // Process the event. Snapshot under the lock, invoke outside it —
            // same C18 rule as performHealthCheck.
            std::vector<EventCallback> callbacks;
            {
                std::lock_guard<std::mutex> lock(callbacksMutex_);
                callbacks = eventCallbacks_;
            }
            for (const auto& callback : callbacks) {
                if (callback) {
                    try {
                        callback(*eventPtr);
                    } catch (const std::exception& e) {
                        if (logger_) {
                            logger_->warning("System",
                                             "Event callback failed: " + std::string(e.what()));
                        }
                    }
                }
            }

            // Deallocate the event back to the pool
            eventPool_->deallocateObject(eventPtr);
        }
    }
}

// =================================================================
// UTILITY FUNCTIONS IMPLEMENTATION
// =================================================================

std::string to_string(SystemState state) {
    switch (state) {
        case SystemState::UNINITIALIZED:
            return "UNINITIALIZED";
        case SystemState::INITIALIZING:
            return "INITIALIZING";
        case SystemState::INITIALIZED:
            return "INITIALIZED";
        case SystemState::STARTING:
            return "STARTING";
        case SystemState::RUNNING:
            return "RUNNING";
        case SystemState::PAUSING:
            return "PAUSING";
        case SystemState::PAUSED:
            return "PAUSED";
        case SystemState::RESUMING:
            return "RESUMING";
        case SystemState::STOPPING:
            return "STOPPING";
        case SystemState::STOPPED:
            return "STOPPED";
        case SystemState::ERROR:
            return "ERROR";
        case SystemState::FATAL_ERROR:
            return "FATAL_ERROR";
        default:
            return "UNKNOWN";
    }
}

SystemConfiguration createDefaultSystemConfiguration() {
    return SystemConfiguration{};
}

// (The implementation is quite extensive - this shows the core structure and key methods)

// =================================================================
// MISSING METHOD IMPLEMENTATIONS
// =================================================================

bool AxonVexSystem::pauseComponents() {
    try {
        if (timingController_) {
            timingController_->pause();
        }
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to pause components: " + std::string(e.what()));
        }
        return false;
    }
}

bool AxonVexSystem::resumeComponents() {
    try {
        if (timingController_) {
            timingController_->resume();
        }
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to resume components: " + std::string(e.what()));
        }
        return false;
    }
}

bool AxonVexSystem::stopComponents(std::chrono::milliseconds timeout) {
    try {
        if (timingController_) {
            timingController_->stop();
        }
        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to stop components: " + std::string(e.what()));
        }
        return false;
    }
}

void AxonVexSystem::updateStatistics() {
    if (!timingController_)
        return;

    try {
        auto timingStats = timingController_->getPerformanceMetrics();

        std::lock_guard<std::mutex> lock(statisticsMutex_);
        statistics_.totalExecutions.store(timingStats.totalExecutions);
        statistics_.successfulExecutions.store(timingStats.successfulExecutions);
        statistics_.failedExecutions.store(timingStats.failedExecutions);
        statistics_.lastUpdateTime = std::chrono::steady_clock::now();
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to update statistics: " + std::string(e.what()));
        }
    }
}

bool AxonVexSystem::loadConfiguration(const Path& filePath) {
    try {
        if (configuration_) {
            return configuration_->loadFromFile(filePath);
        }
        return false;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to load configuration: " + std::string(e.what()));
        }
        return false;
    }
}

bool AxonVexSystem::attemptRecovery(const std::string& errorDescription) {
    if (!systemConfig_.enableAutoRecovery) {
        return false;
    }

    uint32_t currentAttempts = currentRecoveryAttempts_.load();
    if (currentAttempts >= systemConfig_.maxRecoveryAttempts) {
        if (logger_) {
            logger_->error("System",
                           "Max recovery attempts reached: " + std::to_string(currentAttempts));
        }
        return false;
    }

    currentRecoveryAttempts_.fetch_add(1);
    statistics_.recoveryAttempts.fetch_add(1);

    try {
        // Attempt basic recovery - restart timing controller
        if (timingController_) {
            timingController_->stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            timingController_->start();
        }

        statistics_.successfulRecoveries.fetch_add(1);

        if (logger_) {
            logger_->info("System", "Recovery attempt successful");
        }

        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Recovery attempt failed: " + std::string(e.what()));
        }
        return false;
    }
}

size_t AxonVexSystem::getMemoryUsage() const noexcept {
    // Simple memory usage estimation
    size_t usage = sizeof(*this);

    usage += unitRegistry_.count() * 1024; // Rough estimate per ProcessingUnit

    usage += (systemPorts_.inputCount() + systemPorts_.outputCount()) * 64;

    statistics_.memoryUsageBytes.store(usage);
    statistics_.peakMemoryUsageBytes.store(
        std::max(usage, statistics_.peakMemoryUsageBytes.load()));

    return usage;
}

size_t AxonVexSystem::getPeakMemoryUsage() const noexcept {
    return statistics_.peakMemoryUsageBytes.load();
}

std::string AxonVexSystem::getResourceReport() const {
    std::ostringstream oss;
    oss << "Resource Usage Report for '" << systemConfig_.systemName << "':\n";
    oss << "  Current Memory: " << (getMemoryUsage() / 1024) << " KB\n";
    oss << "  Peak Memory: " << (getPeakMemoryUsage() / 1024) << " KB\n";
    oss << "  Processing Units: " << getProcessingUnitCount() << "\n";
    oss << "  System Ports: "
        << (getSystemInputPortNames().size() + getSystemOutputPortNames().size()) << "\n";
    oss << "  Uptime: " << std::fixed << std::setprecision(1) << statistics_.getUptimeSeconds()
        << " seconds\n";
    return oss.str();
}

const SystemConfiguration& AxonVexSystem::getSystemConfiguration() const noexcept {
    return systemConfig_;
}

bool AxonVexSystem::updateSystemConfiguration(const SystemConfiguration& config) {
    // C25: systemConfig_ is read by the monitoring/event/scheduler threads with
    // no lock, and most fields are consumed during initialize() anyway — a live
    // "update" was a data race that never re-applied to running components.
    // The config is immutable once initialization begins.
    if (currentState_.load() != SystemState::UNINITIALIZED) {
        if (logger_) {
            logger_->error("System",
                           "updateSystemConfiguration rejected: only allowed before initialize()");
        }
        return false;
    }
    try {
        config.validate();
        systemConfig_ = config;

        if (logger_) {
            logger_->info("System", "System configuration updated");
        }

        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System",
                           "Failed to update system configuration: " + std::string(e.what()));
        }
        return false;
    }
}

Configuration& AxonVexSystem::getConfiguration() noexcept {
    return *configuration_;
}

const Configuration& AxonVexSystem::getConfiguration() const noexcept {
    return *configuration_;
}

uint32_t AxonVexSystem::registerEventCallback(EventCallback callback) {
    if (!callback)
        return 0;

    std::lock_guard<std::mutex> lock(callbacksMutex_);
    uint32_t callbackId = nextCallbackId_.fetch_add(1);

    if (eventCallbacks_.size() <= callbackId) {
        eventCallbacks_.resize(callbackId + 1);
    }
    eventCallbacks_[callbackId] = callback;

    return callbackId;
}

void AxonVexSystem::unregisterEventCallback(uint32_t callbackId) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);

    if (callbackId < eventCallbacks_.size()) {
        eventCallbacks_[callbackId] = nullptr;
    }
}

uint32_t AxonVexSystem::registerHealthCheckCallback(HealthCheckCallback callback) {
    if (!callback)
        return 0;

    std::lock_guard<std::mutex> lock(callbacksMutex_);
    uint32_t callbackId = nextCallbackId_.fetch_add(1);

    if (healthCheckCallbacks_.size() <= callbackId) {
        healthCheckCallbacks_.resize(callbackId + 1);
    }
    healthCheckCallbacks_[callbackId] = callback;

    return callbackId;
}

Logger& AxonVexSystem::getLogger() noexcept {
    return *logger_;
}

const Logger& AxonVexSystem::getLogger() const noexcept {
    return *logger_;
}

void AxonVexSystem::setDebugMode(bool enabled) noexcept {
    debugMode_.store(enabled);
}

bool AxonVexSystem::isDebugMode() const noexcept {
    return debugMode_.load();
}

std::string AxonVexSystem::getSystemReport() const {
    std::ostringstream oss;
    oss << "AxonVex System Report for '" << systemConfig_.systemName << "':\n";
    oss << "========================================\n";
    oss << "System State: " << to_string(currentState_.load()) << "\n";
    oss << "Running: " << (isRunning() ? "Yes" : "No") << "\n";
    oss << "Healthy: " << (isHealthy() ? "Yes" : "No") << "\n";
    oss << "Debug Mode: " << (isDebugMode() ? "Yes" : "No") << "\n";
    oss << "\n" << statistics_.getReport() << "\n";
    oss << getResourceReport() << "\n";
    oss << getSystemPortInfo();
    return oss.str();
}

// Update the cleanup methods to handle system ports
void AxonVexSystem::cleanupComponents() {
    // Clear system port assignments first
    systemPorts_.clear();

    // Reset components
    timingController_.reset();
    configuration_.reset();

    if (logger_) {
        logger_->stop();
        logger_.reset();
    }
}

// Update the unregisterProcessingUnit method to remove associated system ports
bool AxonVexSystem::unregisterProcessingUnit(uint32_t unitId) {
    // unitRegistry_.remove() atomically finds-and-erases under its own lock:
    // whichever concurrent caller of unregisterProcessingUnit(same id) gets
    // the non-null unique_ptr back is the exclusive owner of the rest of
    // this function (the other gets nullptr and returns false below) --
    // same "cannot observe the unit as still-present twice" guarantee the
    // pre-extraction code got from holding unitsMutex_ across the whole
    // find-through-erase span, without needing to hold any lock here.
    std::unique_ptr<ProcessingUnit> removed = unitRegistry_.remove(unitId);
    if (!removed) {
        return false;
    }

    ProcessingUnit* unitPtr = removed.get();
    std::string unitName = unitPtr->getName();

    // Remove any system ports associated with this unit. systemPorts_ takes
    // only its own internal mutex_ -- no lock-order concern versus
    // assignSystemInputPort/OutputPort (which no longer hold any unit lock
    // while touching systemPorts_ either).
    SystemPortRegistry::RemovedPorts removedPorts = systemPorts_.removeAllForOwner(unitPtr);
    for (const auto& name : removedPorts.inputs) {
        if (logger_) {
            logger_->info("System", "Removing system input port '" + name +
                                        "' due to ProcessingUnit removal");
        }
    }
    for (const auto& name : removedPorts.outputs) {
        if (logger_) {
            logger_->info("System", "Removing system output port '" + name +
                                        "' due to ProcessingUnit removal");
        }
    }

    // Remove from timing controller if running
    if (timingController_) {
        try {
            timingController_->removeProcessingUnit(unitPtr);
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->warning("System", "Failed to remove unit from timing controller: " +
                                               std::string(e.what()));
            }
        }
    }

    // Done with unitPtr; destroy the unit now (no lock held here at all --
    // unitRegistry_.remove() already released its internal lock before
    // returning ownership to us).
    removed.reset();

    // Update statistics
    statistics_.activeProcessingUnits.fetch_sub(1);

    // Publish event
    SystemEvent event;
    event.type = SystemEvent::Type::PROCESSING_UNIT_REMOVED;
    event.oldState = currentState_.load();
    event.newState = currentState_.load();
    event.description = "Processing unit unregistered: " + unitName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_id"] = std::to_string(unitId);
    event.metadata["unit_name"] = unitName;

    publishEvent(event);

    if (logger_) {
        logger_->info("System", "Unregistered processing unit: " + unitName +
                                    " (ID: " + std::to_string(unitId) + ")");
    }

    return true;
}

} // namespace axonvex::core
