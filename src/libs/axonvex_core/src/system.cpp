/**
 * @file system.cpp
 * @brief AxonVex System Management and Lifecycle Control Implementation
 *
 * Usage documentation for core utilities:
 * - Use systemTimer_ for timing system-level operations (init, shutdown, health checks).
 * - Event publishing/message passing and deferred actions go through eventBus_
 *   (see eventBus.hpp) -- it owns the pool and queue internally.
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
    : systemConfig_(config),
      // onTransitioned/onInvalidTransition read logger_/statistics_/eventBus_
      // (via notifyStateChange()) -- façade-owned, lifecycleController_ never
      // touches them directly. The three isOn*Thread predicates aggregate
      // EventBus's/HealthMonitor's/the scheduler's published ids -- each is
      // a lazy lambda over `this`, invoked only well after this whole
      // constructor returns, so eventBus_/healthMonitor_/timingController_
      // not existing yet at THIS point is not a hazard (see
      // lifecycleController_'s declaration comment in system.hpp).
      lifecycleController_(
          [this](SystemState oldState, SystemState newState) {
              statistics_.totalStateTransitions.fetch_add(1);
              logStateTransition(oldState, newState);
              notifyStateChange(oldState, newState);
          },
          [this](SystemState from, SystemState to) {
              if (logger_) {
                  logger_->warning("System", "Invalid state transition from " +
                                                 stateToString(from) + " to " + stateToString(to));
              }
          },
          [this] { return eventBus_.isOnDispatchThread(); },
          [this] { return healthMonitor_.isOnMonitorThread(); },
          [this] { return timingController_ && timingController_->isOnSchedulerThread(); }),
      unitRegistry_(systemConfig_.maxProcessingUnits),
      // C25/C38: isShuttingDown/shutdownRequested now delegate to
      // lifecycleController_ for real -- see eventBus.hpp's class doc.
      eventBus_([this] { return logger_.get(); },
                [this] { return lifecycleController_.isShuttingDown(); },
                [this] { return lifecycleController_.shutdownRequested(); },
                [this] { statistics_.errorCount.fetch_add(1); }),
      // Providers read façade members that are themselves replaced/reset
      // across initialize()/cleanupComponents() -- see healthMonitor.hpp's
      // constructor doc for why that is safe (teardown-ordering guarantee).
      healthMonitor_(
          systemConfig_, [this] { return lifecycleController_.shutdownRequested(); }, eventBus_,
          [this] { return logger_.get(); },
          HealthMonitor::Providers{
              [this] { return lifecycleController_.state(); },
              [this] { return timingController_ != nullptr; },
              [this] { return configuration_ != nullptr; },
              [this] { return logger_ != nullptr && logger_->isRunning(); },
              [this] { return getMemoryUsage(); },
              [this] { return statistics_.getSuccessRate(); },
          },
          [this](const std::string& desc) { attemptRecovery(desc); },
          [this] { statistics_.errorCount.fetch_add(1); }, [this] { updateStatistics(); }) {

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
    if (lifecycleController_.state() != SystemState::UNINITIALIZED &&
        lifecycleController_.state() != SystemState::STOPPED) {
        emergencyShutdown();
    }
    // If a concurrent emergencyShutdown owned the teardown (our call above
    // skipped via try_lock), wait for it to finish before members are
    // destroyed. This only synchronizes with mutex-owning teardowns: a
    // deferred self-join emergencyShutdown (running ON eventBus_'s dispatch
    // thread or healthMonitor_'s monitor thread) releases the mutex via try_lock's early
    // return while still unwinding user-code frames on that thread — the
    // joins below cover that window. Legal here (no self-join risk): a
    // worker thread cannot reach the destructor without going through
    // initialize()/stop()/reset(), all of which refuse on
    // isOnWorkerThread().
    { auto wait = lifecycleController_.acquireTeardown(); }
    eventBus_.join();
    healthMonitor_.join();
}

// =================================================================
// SYSTEM LIFECYCLE MANAGEMENT
// =================================================================

bool AxonVexSystem::initialize(const std::string& configPath) {
    // C41: lifecycle teardown destroys components this worker's own stack is
    // using; refusal is the only honest behavior. emergencyShutdown() remains
    // the sanctioned from-any-thread path (it stops but never destroys).
    // Must be the first statement: everything below (including the C39
    // joins just after this) assumes it is not running on eventBus_'s
    // dispatch thread or healthMonitor_'s monitor thread.
    if (isOnWorkerThread()) {
        if (logger_) {
            logger_->error("System", "initialize() called from a system worker thread -- refused. "
                                     "Use emergencyShutdown() from callbacks.");
        }
        return false;
    }

    // C39: join and clear any stale thread handles BEFORE
    // initializeComponents() below replaces logger_/timingController_ and
    // eventBus_.reinitialize() replaces the event pool/queue. A stale thread
    // can still be running here — from emergencyShutdown's deferred
    // self-join (it runs on the very thread it would otherwise join, so it
    // skips the join and leaves the handle set, C33/C36) or from a throw
    // between a previous initialize()'s thread start and its INITIALIZED
    // transition (the catch path's cleanupComponents() never touches thread
    // handles) — and that stale thread dereferences logger_/timingController_
    // inside emergencyShutdown()/monitoringLoop()/EventBus::dispatchLoop().
    // Freeing those objects out from under it (by calling
    // initializeComponents() first) is a use-after-free window that
    // reset()'s sleep_for(100ms) only ever masked. This join must stay
    // above initializeComponents().
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
    healthMonitor_.join();
    eventBus_.join();

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
            // Must actually transition (not just log) -- isHealthy() reads
            // currentState_, and a failed spec/blocks-layout deploy must not
            // leave the system reporting itself healthy while INITIALIZING.
            transitionState(SystemState::ERROR);
            cleanupComponents();
            return false;
        }

        // C38: initialize() is the single owner of clearing the shutdown
        // latch (lifecycleController_.clearShutdownLatch()).
        // stop()/emergencyShutdown() set it; nothing else may clear it. This
        // does NOT decide a race against a concurrent e-stop: initialize()
        // clears the latch, starts healthMonitor_ (below), and starts
        // eventBus_ unconditionally regardless of who wins. What actually
        // guarantees the e-stop wins is transitionState(): emergencyShutdown()
        // sets the lifecycle state to FATAL_ERROR (legal from any state),
        // which trips the "state() >= SystemState::STOPPING" half of
        // lifecycleController_.shutdownRequested() -- polled by
        // healthMonitor_'s and eventBus_'s worker loops -- even for threads
        // this call is about to start, while this call's own
        // transitionState(INITIALIZED) is refused (FATAL_ERROR only accepts
        // a transition to UNINITIALIZED), so initialize() itself returns
        // false. That >= STOPPING guard is load-bearing for this invariant —
        // do not remove or weaken it.
        lifecycleController_.clearShutdownLatch();

        // Start monitoring if enabled
        if (systemConfig_.enablePerformanceMonitoring) {
            healthMonitor_.start();
        }

        // Start the event dispatch thread
        eventBus_.start();

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

    lifecycleController_.requestShutdown(); // Signal shutdown to all threads
    systemTimer_.start();                   // Start timing shutdown

    try {
        // Thread-handle teardown serialized against emergencyShutdown (C2),
        // which may fire concurrently from any thread via the SafetyHook.
        // Flags are set before locking so a system thread that enters
        // emergencyShutdown (try_lock fails) still exits its loop promptly.
        eventBus_.requestStop();
        healthMonitor_.requestStop();
        // Held through component stop/cleanup as well: a hook-triggered
        // emergencyShutdown must not touch timingController_ while
        // cleanupComponents() resets it (it try_locks and skips instead)
        std::unique_lock<std::mutex> teardownLock = lifecycleController_.acquireTeardown();
        // Stop event processing thread first. Not a self-join risk here --
        // stop() already refused above if called on the dispatch thread.
        eventBus_.stopAndJoin();
        // Stop monitoring thread next
        healthMonitor_.join();
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
    // C7: validated transition (legal from any state). Runs before the
    // shutdown latch is set so the STATE_CHANGE event isn't dropped by
    // EventBus::publish()'s shutdown guard.
    transitionState(SystemState::FATAL_ERROR);
    lifecycleController_.requestShutdown();
    eventBus_.requestStop();
    healthMonitor_.requestStop();

    // Single-owner teardown: e-stop can fire from any thread via the SafetyHook
    // (C2), racing stop() or another e-stop on the thread handles. try_lock, not
    // lock: a system thread entering here while stop() joins it under the mutex
    // must return (flags above end its loop) or the join would deadlock. The
    // teardown owner finishes the joins; the destructor waits on this mutex.
    std::unique_lock<std::mutex> teardownLock = lifecycleController_.tryAcquireTeardown();
    if (!teardownLock.owns_lock()) {
        return;
    }

    // Stop event processing thread first. Self-join guard (now inside
    // eventBus_'s dispatch-thread join itself): the emergency callback may
    // run on a system thread; that call refuses and returns false rather
    // than joining/resetting — the destructor's second pass joins from the
    // owner thread instead.
    eventBus_.stopAndJoin();
    // Unconditional: events can be queued before the event thread ever starts
    // (e.g. registerProcessingUnit during a failed initializeBlocksLayout) and
    // would otherwise leak when the pool is torn down (C25). stopAndJoin()
    // above only drains if it actually reaped a thread; this covers the
    // never-started case too. Idempotent if already drained.
    eventBus_.drain();
    // Stop monitoring thread next
    healthMonitor_.join();
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
    // below is safe to run on eventBus_'s dispatch thread or
    // healthMonitor_'s monitor thread.
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
    // self-join emergencyShutdown: that variant runs ON eventBus_'s
    // dispatch thread or healthMonitor_'s monitor thread and releases the
    // mutex via try_lock's early
    // return while still unwinding user-code frames on that thread. The
    // joins below close that window — legal here because worker threads
    // can't reach this point (refused above), so no self-join is possible.
    { auto wait = lifecycleController_.acquireTeardown(); }
    eventBus_.join();
    healthMonitor_.join();

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

    healthMonitor_.resetCallbacks();
    eventBus_.resetCallbacks();

    // Reset components
    timingController_.reset();
    configuration_.reset();
    logger_.reset();
}

// =================================================================
// STATE AND MONITORING IMPLEMENTATION
// =================================================================

SystemState AxonVexSystem::getState() const noexcept {
    return lifecycleController_.state();
}

bool AxonVexSystem::isRunning() const noexcept {
    return lifecycleController_.state() == SystemState::RUNNING;
}

bool AxonVexSystem::isHealthy() const noexcept {
    SystemState state = lifecycleController_.state();
    return state != SystemState::ERROR && state != SystemState::FATAL_ERROR;
}

double AxonVexSystem::getUptimeSeconds() const noexcept {
    return statistics_.getUptimeSeconds();
}

const SystemStatistics& AxonVexSystem::getStatistics() const noexcept {
    return statistics_;
}

SystemHealth AxonVexSystem::getHealth() const {
    return healthMonitor_.check();
}

void AxonVexSystem::performHealthCheck() {
    healthMonitor_.performHealthCheck();
}

// =================================================================
// PROCESSING UNIT MANAGEMENT
// =================================================================

uint32_t AxonVexSystem::registerProcessingUnit(std::unique_ptr<ProcessingUnit> unit,
                                               const TimingConstraints& constraints) {
    if (!unit) {
        throw std::invalid_argument("Processing unit cannot be null");
    }

    // addAndRun() keeps "insert" and "use the pointer" atomic against a
    // concurrent emergencyShutdown()/reset() -> UnitRegistry::clear(): the
    // scheduling call below runs while unitRegistry_ still holds its lock,
    // so clear() cannot free unitPtr out from under it (ASan-confirmed
    // heap-use-after-free pre-fix: ProcessingUnit::getName() below used to
    // run unlocked after add() returned, racing clear()). unitName is
    // captured under that same lock so the event/log statements after
    // addAndRun returns use a copy, never the (by-then possibly freed)
    // pointer.
    std::string unitName;
    uint32_t unitId = unitRegistry_.addAndRun(
        std::move(unit), [this, &constraints, &unitName](uint32_t /*id*/, ProcessingUnit* unitPtr) {
            unitName = unitPtr->getName();

            if (timingController_ && isRunning()) {
                TimingConstraints finalConstraints = constraints;
                if (finalConstraints.period.count() == 0) {
                    // Use default constraints based on system tick rate
                    finalConstraints.period = systemConfig_.systemTickRate;
                    finalConstraints.deadline = finalConstraints.period;
                    finalConstraints.wcet = finalConstraints.period / 10;
                }

                // Throwing here rolls the unit back inside addAndRun (still
                // under the lock) before the exception reaches our caller.
                timingController_->scheduleProcessingUnit(unitPtr, finalConstraints);
            }
        });

    // Update statistics
    statistics_.totalProcessingUnits.fetch_add(1);
    statistics_.activeProcessingUnits.fetch_add(1);

    // Publish event
    SystemEvent event;
    event.type = SystemEvent::Type::PROCESSING_UNIT_ADDED;
    event.oldState = lifecycleController_.state();
    event.newState = event.oldState;
    event.description = "Processing unit registered: " + unitName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_id"] = std::to_string(unitId);
    event.metadata["unit_name"] = unitName;

    eventBus_.publish(event);

    if (logger_) {
        logger_->info("System", "Registered processing unit: " + unitName +
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
    event.oldState = lifecycleController_.state();
    event.newState = event.oldState;
    event.description = "System input port assigned: " + systemPortName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["port_name"] = systemPortName;
    event.metadata["unit_name"] = unit->getName();
    event.metadata["unit_port_id"] = std::to_string(unitPortId);

    eventBus_.publish(event);

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
    event.oldState = lifecycleController_.state();
    event.newState = event.oldState;
    event.description = "System output port assigned: " + systemPortName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["port_name"] = systemPortName;
    event.metadata["unit_name"] = unit->getName();
    event.metadata["unit_port_id"] = std::to_string(unitPortId);

    eventBus_.publish(event);

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
    // The FSM table + C7 (FATAL_ERROR legal from any state) live in
    // lifecycleController_.transition() now (Phase 2 decomposition step 6);
    // this wrapper is the only thing that survives on the façade. The
    // stats-increment/log/notify tail and the invalid-transition warning log
    // run as lifecycleController_'s injected onTransitioned/
    // onInvalidTransition hooks (wired in AxonVexSystem's constructor) --
    // both still call back into logStateTransition()/notifyStateChange()
    // below, unchanged.
    return lifecycleController_.transition(newState);
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

        // Initialize the event pool and queue
        eventBus_.reinitialize(systemConfig_.eventPoolSize, systemConfig_.eventQueueSize);

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

        // forEach() holds unitRegistry_'s lock for the WHOLE loop, so a
        // concurrent emergencyShutdown()/reset() -> clear() cannot free a
        // unit out from under scheduleProcessingUnit() mid-iteration —
        // restores the atomicity the old unitsMutex_-held span gave
        // pre-extraction (a snapshot-then-unlocked-iterate here is the same
        // ASan-confirmed heap-use-after-free shape as registerProcessingUnit,
        // just via all() instead of add()).
        unitRegistry_.forEach([this](ProcessingUnit* unit) {
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
        });

        return true;

    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Component start failed: " + std::string(e.what()));
        }
        return false;
    }
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

    eventBus_.publish(event);
}

void AxonVexSystem::handleProcessingUnitError(ProcessingUnit* unit, const std::string& error) {
    if (!unit)
        return;

    // Set error state in the processing unit
    unit->setError(error);

    // Create and publish error event
    SystemEvent event;
    event.type = SystemEvent::Type::PROCESSING_UNIT_ERROR;
    event.oldState = lifecycleController_.state();
    event.newState = event.oldState;
    event.description = "ProcessingUnit '" + unit->getName() + "' error: " + error;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_name"] = unit->getName();
    event.metadata["error_message"] = error;

    eventBus_.publish(event);

    // Attempt recovery if enabled
    if (systemConfig_.enableAutoRecovery) {
        attemptRecovery("ProcessingUnit error: " + error);
    }
}

bool AxonVexSystem::isOnWorkerThread() const noexcept {
    // C40/C41/C42: the refusal-matrix aggregation itself now lives in
    // lifecycleController_.isOnWorkerThread() (Phase 2 decomposition step
    // 6), driven by the three predicates wired at construction (see
    // AxonVexSystem's constructor). The scheduler-thread predicate's
    // aliasing argument for the unlocked timingController_ read -- every
    // caller is either a lifecycle method checking this on entry before
    // mutating any component, or a call arriving from inside a scheduler-
    // thread task/error-callback where timingController_ being alive is
    // implied by the call itself (the task runs inside the very
    // TimingController this thread belongs to) -- lives at that lambda,
    // not in lifecycleController_, which never reads timingController_
    // directly.
    return lifecycleController_.isOnWorkerThread();
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
    if (lifecycleController_.state() != SystemState::UNINITIALIZED) {
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
    return eventBus_.registerCallback(std::move(callback));
}

void AxonVexSystem::unregisterEventCallback(uint32_t callbackId) {
    eventBus_.unregisterCallback(callbackId);
}

uint32_t AxonVexSystem::registerHealthCheckCallback(HealthCheckCallback callback) {
    return healthMonitor_.registerHealthCallback(std::move(callback));
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
    oss << "System State: " << to_string(lifecycleController_.state()) << "\n";
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

    // Unconditional, same as emergencyShutdown()'s eventBus_.drain() call:
    // events can be queued before the event thread ever starts (e.g.
    // registerProcessingUnit() during a failed initializeBlocksLayout()) and
    // would otherwise leak (C25) when the next initialize() call's
    // eventBus_.reinitialize() replaces the pool/queue without ever having
    // destructed the SystemEvent objects still sitting in the old ones.
    eventBus_.drain();

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
    event.oldState = lifecycleController_.state();
    event.newState = event.oldState;
    event.description = "Processing unit unregistered: " + unitName;
    event.timestamp = std::chrono::steady_clock::now();
    event.metadata["unit_id"] = std::to_string(unitId);
    event.metadata["unit_name"] = unitName;

    eventBus_.publish(event);

    if (logger_) {
        logger_->info("System", "Unregistered processing unit: " + unitName +
                                    " (ID: " + std::to_string(unitId) + ")");
    }

    return true;
}

} // namespace axonvex::core
