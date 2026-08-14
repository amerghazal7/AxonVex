/**
 * @file healthMonitor.hpp
 * @brief Monitoring thread + health checks + statistics refresh
 * @author AxonVex Development Team
 *
 * Phase 2 core decomposition, migration step 5 (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.5). Extracted from AxonVexSystem's
 * monitoringWorker_/monitoringEnabled_/healthCheckCallbacks_/callbacksMutex_/
 * nextCallbackId_/monitoringLoop()/performHealthCheck()/
 * performInternalHealthCheck().
 *
 * LifecycleController (migration step 6, last) will eventually own the
 * shutdown-requested predicate this class is handed at construction; until
 * then AxonVexSystem supplies it as a callable, same split as EventBus's
 * constructor (see eventBus.hpp).
 */

#pragma once

#include <atomic>
#include <axonvex_core/detail/workerThread.hpp>
#include <axonvex_core/eventBus.hpp>
#include <axonvex_core/precisionTimer.hpp>
#include <axonvex_core/systemConfiguration.hpp>
#include <axonvex_core/systemEvent.hpp>
#include <axonvex_core/systemHealth.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace axonvex::core {

class Logger; // non-owned; see the constructor's `logger` parameter doc.

/**
 * @brief Owns the monitoring thread, health-check callback registry, and
 * health-check logic for one AxonVexSystem.
 *
 * Threading contract: check() is const and lock-free (every read is either
 * an injected provider call or an immutable SystemConfiguration field) --
 * safe from any thread. performHealthCheck() is C18 snapshot-then-dispatch:
 * healthCallbacks_ is copied under callbacksMutex_, the lock is released,
 * then each callback runs -- never under the lock. registerHealthCallback()
 * is callable from any thread, including from inside a callback running on
 * the monitor thread itself (the reentrant-registration regression test).
 *
 * Inherited invariants (do not re-derive; carried verbatim from
 * AxonVexSystem's pre-extraction monitoringLoop()/performHealthCheck()/
 * performInternalHealthCheck()):
 *  - C18: performHealthCheck() snapshots healthCallbacks_ under
 *    callbacksMutex_, releases, then invokes -- never under the lock. The
 *    HealthCheckCallbackReentrantRegistration regression test pins this.
 *  - C38: the monitor loop polls `shutdownRequested()` (merges the old
 *    `monitoringEnabled_.load() && !isShuttingDown_.load()` while-condition
 *    and the loop-body's `currentState_.load() >= SystemState::STOPPING`
 *    break into one predicate call -- same recipe as EventBus's dispatch
 *    loop).
 *  - loop-LOCAL health-check timer: the old shared `lastHealth_` epoch was
 *    already deleted pre-extraction (a prior C18 fix) -- monitoringLoop()'s
 *    `lastHealthCheck` local variable moves here unchanged; do not
 *    reintroduce shared epoch state read/written from multiple threads.
 *  - C41/C39: id publish/clear and join-before-assign are
 *    detail::WorkerThread's job (monitorThread_ below); this class never
 *    duplicates that discipline.
 *  - Recovery orchestration (attemptRecovery/handleFatalError) stays on the
 *    façade per the design spec -- this class only invokes the injected
 *    onCriticalHealth hook, never a lifecycle transition directly.
 */
class HealthMonitor {
  public:
    using HealthCheckCallback = std::function<SystemHealth()>;
    using Predicate = std::function<bool()>;
    using LoggerAccessor = std::function<Logger*()>;
    using StateAccessor = std::function<SystemState()>;
    using RecoveryHook = std::function<void(const std::string&)>;

    /**
     * @brief Component-health + resource providers.
     *
     * performInternalHealthCheck() used to read timingController_/
     * configuration_/logger_/getMemoryUsage()/statistics_ directly off
     * AxonVexSystem. Bundled into one struct (rather than six positional
     * constructor parameters sharing the same std::function<bool()>/
     * std::function<size_t()>/std::function<double()> shapes) so a caller
     * cannot transpose two of them by accident -- each field is named at
     * the call site.
     */
    struct Providers {
        StateAccessor getState;                 // currentState_.load()
        Predicate timingControllerHealthy;      // timingController_ != nullptr
        Predicate configurationHealthy;         // configuration_ != nullptr
        Predicate loggerHealthy;                // logger_ && logger_->isRunning()
        std::function<size_t()> getMemoryUsage; // façade's own getMemoryUsage()
        std::function<double()> getSuccessRate; // statistics_.getSuccessRate()
    };

    /**
     * @param cfg Non-owned reference into the façade's SystemConfiguration
     * -- read for statisticsUpdateInterval/healthCheckInterval (loop
     * cadence) and maxMemoryPoolSize/enableAutoRecovery (health-check
     * thresholds). SystemConfiguration is immutable once initialize()
     * begins (façade contract; see AxonVexSystem::updateSystemConfiguration
     * -- it refuses updates once the state leaves UNINITIALIZED), so a
     * stored reference is safe for this object's entire lifetime: unlike
     * timingController_/configuration_/logger_, systemConfig_ is never
     * reset/reassigned by any lifecycle method.
     * @param shutdownRequested Mirrors "isShuttingDown_ || currentState_ >=
     * SystemState::STOPPING" (C38); the monitor loop polls this, not two
     * separate flags.
     * @param events Non-owned reference to the façade's EventBus -- used to
     * publish the HEALTH_CHECK event. Must outlive this object. The façade
     * guarantees this two ways: (1) declaration order -- healthMonitor_ is
     * declared AFTER eventBus_ in system.hpp, so C++'s reverse-declaration-
     * order member destruction runs this object's destructor (which joins
     * the monitor thread) before eventBus_'s; (2) every lifecycle path
     * (initialize()'s C39 pre-join, stop(), emergencyShutdown(), reset(),
     * the destructor) explicitly stops/joins this monitor's thread before
     * eventBus_ is ever reset or destroyed -- the same ordering the
     * `providers` callables below rely on.
     * @param logger Non-owned accessor into the façade's Logger, same
     * "call at each use, never cache" rationale as EventBus::LoggerAccessor
     * -- may return null.
     * @param providers Component-health + resource read hooks (see
     * Providers doc above); safe under the same teardown-ordering guarantee
     * `events` documents.
     * @param onCriticalHealth Invoked with a formatted error description
     * when a health check's merged overall status is CRITICAL or FAILURE
     * AND cfg.enableAutoRecovery is true (façade wires this to
     * attemptRecovery()). May be empty (no-op) -- recovery orchestration
     * stays on the façade per the design spec; this class never calls back
     * into a lifecycle transition itself.
     * @param onLoopError Invoked (no args) when an iteration of the monitor
     * loop throws std::exception -- mirrors the façade's former
     * `statistics_.errorCount.fetch_add(1)` in monitoringLoop()'s catch
     * block. May be empty (no-op).
     * @param updateStatistics Invoked on the monitor thread every
     * cfg.statisticsUpdateInterval -- façade-owned (touches
     * timingController_ and statisticsMutex_/statistics_ directly, none of
     * which this class reads); mirrors the façade's former
     * AxonVexSystem::updateStatistics() call inside monitoringLoop().
     */
    HealthMonitor(const SystemConfiguration& cfg, Predicate shutdownRequested, EventBus& events,
                  LoggerAccessor logger, Providers providers, RecoveryHook onCriticalHealth,
                  std::function<void()> onLoopError, std::function<void()> updateStatistics);
    ~HealthMonitor() = default;

    // Holds a mutex_ (via callbacksMutex_) and a detail::WorkerThread:
    // non-copyable, non-movable (delete, never =default).
    HealthMonitor(const HealthMonitor&) = delete;
    HealthMonitor& operator=(const HealthMonitor&) = delete;
    HealthMonitor(HealthMonitor&&) = delete;
    HealthMonitor& operator=(HealthMonitor&&) = delete;

    /// Starts the monitor thread via detail::WorkerThread. enabled_ is
    /// flipped true from INSIDE WorkerThread::start()'s beforeSpawn hook --
    /// after the stale-handle join, before the new thread exists -- same
    /// C46 rationale as EventBus::start() (see workerThread.hpp's
    /// beforeSpawn doc for why that ordering matters).
    void start();

    /// Bare join: no flag touch. For callers (initialize()'s C39 pre-join)
    /// that only need a stale handle out of the way before other components
    /// are torn down/replaced. Refuses (returns false) on self-join, same
    /// as detail::WorkerThread::join().
    bool join();

    /// Non-blocking, callable from any thread including the monitor thread
    /// itself: sets enabled_ = false. Kept separate from stopAndJoin() for
    /// the same C2 reason as EventBus::requestStop() -- a caller can flip it
    /// BEFORE taking a teardown lock a concurrent emergencyShutdown()
    /// running on the monitor thread might be try_locking; that racing
    /// thread's own loop must see enabled_ go false promptly, without
    /// waiting for this call's join.
    void requestStop() noexcept;

    /// requestStop() + join().
    bool stopAndJoin();

    bool isOnMonitorThread() const noexcept;

    /// Returns 0 (without touching the id counter) for a null callback --
    /// matches the façade's former registerHealthCheckCallback() guard.
    uint32_t registerHealthCallback(HealthCheckCallback cb);

    /// Clears every registered callback and resets the id counter to 1 --
    /// the health-callback half of AxonVexSystem::reset()'s former
    /// callbacksMutex_ block (EventBus::resetCallbacks() is the other half
    /// of that same reset() body).
    void resetCallbacks() noexcept;

    /// = the façade's former performInternalHealthCheck(): reads the
    /// injected providers, no callback dispatch, no event publish. Safe to
    /// call from any thread (const, no locks -- every read is either a
    /// provider call or one of cfg_'s immutable fields).
    SystemHealth check() const;

    /// C18 snapshot-then-dispatch: merges every registered user health
    /// callback's SystemHealth into check()'s result, publishes a
    /// HEALTH_CHECK event via events_, and -- iff the merged status is
    /// CRITICAL/FAILURE and cfg_.enableAutoRecovery -- invokes
    /// onCriticalHealth_.
    void performHealthCheck();

  private:
    void monitoringLoop();

    const SystemConfiguration& cfg_;
    Predicate shutdownRequested_;
    EventBus& events_;
    LoggerAccessor logger_;
    Providers providers_;
    RecoveryHook onCriticalHealth_;
    std::function<void()> onLoopError_;
    std::function<void()> updateStatistics_;

    detail::WorkerThread monitorThread_;
    std::atomic<bool> enabled_{false};

    mutable std::mutex callbacksMutex_;
    std::vector<HealthCheckCallback> healthCallbacks_;
    std::atomic<uint32_t> nextCallbackId_{1};

    // Own timer, deliberately NOT the façade's systemTimer_: that member is
    // also start()/stop()-ed by initialize()/stop() on the calling thread
    // while this monitor's loop calls performHealthCheck() concurrently on
    // its own thread -- sharing one PrecisionTimer across two threads doing
    // unrelated start()/stop() pairs was a pre-existing latent race in the
    // pre-extraction code (never triggered because the two call sites are
    // rarely concurrent in practice); a dedicated instance here removes it
    // for free, same spirit as UnitRegistry::remove()'s C18-shape fix.
    PrecisionTimer healthCheckTimer_{PrecisionTimer::DEFAULT_MAX_SAMPLES};
};

} // namespace axonvex::core
