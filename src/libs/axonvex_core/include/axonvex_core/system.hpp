/**
 * @file system.hpp
 * @brief AxonVex System Management and Lifecycle Control
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * The AxonVexSystem class provides centralized system management, orchestrating
 * all framework components including ProcessingUnits, TimingController,
 * Configuration, and resource management.
 */

#pragma once

namespace axonvex::adapters {
class AdapterInterface;
}

#include <atomic>
#include <axonvex_core/configuration.hpp>
#include <axonvex_core/detail/workerThread.hpp>
#include <axonvex_core/eventBus.hpp>
#include <axonvex_core/healthMonitor.hpp>
#include <axonvex_core/lifecycleController.hpp>
#include <axonvex_core/logger.hpp>
#include <axonvex_core/path.hpp>
#include <axonvex_core/precisionTimer.hpp>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/systemConfiguration.hpp>
#include <axonvex_core/systemEvent.hpp>
#include <axonvex_core/systemHealth.hpp>
#include <axonvex_core/systemPortRegistry.hpp>
#include <axonvex_core/timingController.hpp>
#include <axonvex_core/unitRegistry.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

class SafetyHook; // core-owned safety hook interface (safetyHook.hpp)

/**
 * @brief System performance statistics
 */
struct SystemStatistics {
    std::atomic<uint64_t> totalProcessingUnits{0};
    std::atomic<uint64_t> activeProcessingUnits{0};
    std::atomic<uint64_t> totalExecutions{0};
    std::atomic<uint64_t> successfulExecutions{0};
    std::atomic<uint64_t> failedExecutions{0};
    std::atomic<uint64_t> totalMissedDeadlines{0};

    std::atomic<uint64_t> memoryUsageBytes{0};
    std::atomic<uint64_t> peakMemoryUsageBytes{0};

    std::atomic<uint64_t> totalStateTransitions{0};
    std::atomic<uint64_t> errorCount{0};
    std::atomic<uint64_t> recoveryAttempts{0};
    std::atomic<uint64_t> successfulRecoveries{0};

    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdateTime;

    void reset();
    std::string getReport() const;
    double getUptimeSeconds() const;
    double getSuccessRate() const;
};

/**
 * @brief Main AxonVex System Management Class
 *
 * The AxonVexSystem class serves as the central orchestrator for the entire
 * AxonVex framework, providing:
 * - Centralized lifecycle management
 * - Configuration management and persistence
 * - ProcessingUnit registration and management
 * - System monitoring and health checks
 * - Error handling and recovery
 * - Performance statistics collection
 * - Resource management
 *
 * @example Basic system usage:
 * @code
 * AxonVexSystem system;
 * system.initialize("config/myapp.json");
 *
 * auto processingUnit = std::make_unique<MyProcessingUnit>("worker");
 * system.registerProcessingUnit(std::move(processingUnit));
 *
 * system.start();
 * // ... system running ...
 * system.stop();
 * @endcode
 */
class AxonVexSystem {
  public:
    using EventCallback = std::function<void(const SystemEvent&)>;
    using HealthCheckCallback = std::function<SystemHealth()>;

    /**
     * @brief Constructor with optional configuration
     */
    explicit AxonVexSystem(const SystemConfiguration& config = SystemConfiguration{});

    /**
     * @brief Destructor - ensures clean shutdown
     *
     * @warning Never let this object be destroyed from a system worker
     * thread — event-processing, monitoring, or the scheduler thread
     * (anywhere isOnWorkerThread() would return true). initialize(),
     * stop(), and reset() can refuse a worker-thread call because they are
     * ordinary member functions with a return value to refuse through; a
     * destructor has no such escape. If the last owning reference (a
     * unique_ptr going out of scope, the last shared_ptr dropped, a stack
     * object unwinding) is released from inside a worker-thread callback —
     * a ProcessingUnit task, an event callback, a health-check callback —
     * ~AxonVexSystem runs ON that worker thread while destroying members
     * the callback's own call stack (EventBus::dispatchLoop(), the callback
     * itself) is still using: undefined behavior once control unwinds back
     * into it. detail::WorkerThread::join() self-refuses rather than
     * self-joining (so this specific call no longer throws/deadlocks the
     * way a raw self-join would — C33/C36 shape), but that only avoids the
     * crash at the join() call; it does not make destroying `this` out from
     * under your own live call stack safe (the same self-join shape guarded
     * everywhere else in this class with a refusal or a deferred join —
     * C33/C36/C40/C42 — none of which a destructor can use). Keep the
     * owning AxonVexSystem alive for its entire lifetime on a thread
     * outside the system's own workers; call emergencyShutdown() from a
     * callback if the system must stop itself,
     * and destroy the object afterward from an external thread.
     *
     * @note virtual: AxonVexSystem is already polymorphic (initializeBlocksLayout()
     * is pure virtual, so this class has a vtable regardless), and
     * loadSystemFromSpec() (systemSpec.hpp) hands back ownership of a
     * SpecSystem through exactly this type -- std::unique_ptr<AxonVexSystem>.
     * A non-virtual destructor there is undefined behavior on delete and, in
     * practice on this ABI, skips every derived member's destructor: an
     * ASan LeakSanitizer run on the SpecSystem instantiation slice caught
     * this concretely (SystemSpec's owned vectors/maps in SpecSystem::spec_
     * leaking because ~SpecSystem() was never reached). Fixed here rather
     * than worked around at that one call site because any future
     * AxonVexSystem subclass stored in a unique_ptr<AxonVexSystem> would hit
     * the exact same UB.
     */
    virtual ~AxonVexSystem();

    // Non-copyable, non-movable (due to atomic members)
    AxonVexSystem(const AxonVexSystem&) = delete;
    AxonVexSystem& operator=(const AxonVexSystem&) = delete;
    AxonVexSystem(AxonVexSystem&&) = delete;
    AxonVexSystem& operator=(AxonVexSystem&&) = delete;

    // =================================================================
    // SYSTEM LIFECYCLE MANAGEMENT
    // =================================================================

    /**
     * @brief Initialize the system with configuration
     *
     * @param configPath Path to configuration file (optional)
     * @return true if initialization successful
     *
     * @note C41: refused (returns false, logs an error) when called from the
     * event-processing or monitoring thread. That teardown/replace of
     * logger_/eventBus_'s pool+queue/timingController_ would run out from
     * under the calling worker's own live stack. Use emergencyShutdown()
     * for in-callback shutdown; reinitialize from outside the worker
     * threads afterward.
     *
     * @note C42 (fixed): the scheduler thread is covered too. A
     * ProcessingUnit task body (or any callback RealTimeScheduler invokes
     * on its own worker thread, e.g. the error callback) is detected by
     * isOnWorkerThread() via TimingController::isOnSchedulerThread() and
     * refused exactly like the event/monitoring threads.
     */
    bool initialize(const std::string& configPath = "");

  protected:
    /**
     * @brief Pure virtual method for initializing the processing block layout
     *
     * This method must be implemented by derived classes to define their specific
     * system architecture. It should contain all logic for:
     * - Creating and registering ProcessingUnits
     * - Connecting ProcessingUnits together via their ports
     * - Assigning system input/output ports from ProcessingUnit ports
     *
     * This method is called during system initialization after core components
     * are set up but before the system transitions to INITIALIZED state.
     *
     * @return true if block layout initialization successful, false otherwise
     */
    virtual bool initializeBlocksLayout() = 0;

    /**
     * @brief Retrieve a previously registered adapter by URI
     *
     * Available inside initializeBlocksLayout(). Cast to the concrete
     * adapter type to access typed unit creation methods.
     */
    axonvex::adapters::AdapterInterface* getAdapter(const std::string& uri) const;

  public:
    /**
     * @brief Start the system and all registered components
     *
     * @return true if start successful
     */
    bool start();

    /**
     * @brief Pause the system (can be resumed)
     *
     * @return true if pause successful
     */
    bool pause();

    /**
     * @brief Resume from paused state
     *
     * @return true if resume successful
     */
    bool resume();

    /**
     * @brief Stop the system gracefully
     *
     * @param timeoutMs Maximum time to wait for graceful shutdown
     * @return true if stop successful; false if it fails, or if called from
     * a system worker thread (event-processing or monitoring) — stop() joins
     * those threads and destroys components, so a worker calling it would
     * join/destroy itself (C40). A refused call leaves currentState_
     * untouched. Use emergencyShutdown() from an event/health callback
     * instead.
     *
     * @note C42 (fixed): the scheduler thread is covered too. A
     * ProcessingUnit task body (or any callback RealTimeScheduler invokes
     * on its own worker thread, e.g. the error callback) is detected by
     * isOnWorkerThread() via TimingController::isOnSchedulerThread() and
     * refused exactly like the event/monitoring threads.
     */
    bool stop(std::chrono::milliseconds timeoutMs = std::chrono::milliseconds(5000));

    /**
     * @brief Emergency shutdown (immediate stop)
     */
    void emergencyShutdown();

    /**
     * @brief Reset the system to uninitialized state
     *
     * @note Refuses and returns immediately, without touching any state, if
     * called from a system worker thread (event-processing or monitoring) —
     * reset() destroys timingController_/configuration_/logger_, which a
     * worker's own call stack may still be using (C40). Use
     * emergencyShutdown() from an event/health callback instead.
     *
     * @note C42 (fixed): the scheduler thread is covered too. A
     * ProcessingUnit task body (or any callback RealTimeScheduler invokes
     * on its own worker thread, e.g. the error callback) is detected by
     * isOnWorkerThread() via TimingController::isOnSchedulerThread() and
     * refused exactly like the event/monitoring threads.
     *
     * @note Not safe against concurrent lifecycle calls otherwise. reset()
     * forces an emergencyShutdown() (which unconditionally reaches
     * FATAL_ERROR), waits for any in-flight teardown to finish (blocks on the
     * same mutex emergencyShutdown()/the destructor use), and then
     * transitions FATAL_ERROR -> UNINITIALIZED. If another thread's
     * initialize()/start()/reset() moves currentState_ away from FATAL_ERROR
     * during that window, the UNINITIALIZED transition is rejected (logged as
     * a warning) and reset() proceeds to tear down containers,
     * timingController_, configuration_, and logger_ regardless — getState()
     * can then transiently report a state (e.g. INITIALIZING) that no longer
     * has a live system behind it. Callers must serialize reset() with other
     * lifecycle calls externally; reset() does not do it for them.
     */
    void reset();

    // =================================================================
    // STATE AND MONITORING
    // =================================================================

    /**
     * @brief Get current system state
     */
    SystemState getState() const noexcept;

    /**
     * @brief Check if system is in a running state
     */
    bool isRunning() const noexcept;

    /**
     * @brief Check if system is healthy
     */
    bool isHealthy() const noexcept;

    /**
     * @brief Get system uptime in seconds
     */
    double getUptimeSeconds() const noexcept;

    /**
     * @brief Get system statistics
     */
    const SystemStatistics& getStatistics() const noexcept;

    /**
     * @brief Get current system health information
     */
    SystemHealth getHealth() const;

    /**
     * @brief Perform system health check
     */
    void performHealthCheck();

    // =================================================================
    // PROCESSING UNIT MANAGEMENT
    // =================================================================

    /**
     * @brief Register a processing unit with the system
     *
     * @param unit Processing unit to register
     * @param constraints Timing constraints for the unit
     * @return Unit ID for future operations
     */
    uint32_t registerProcessingUnit(std::unique_ptr<ProcessingUnit> unit,
                                    const TimingConstraints& constraints = {});

    /**
     * @brief Unregister a processing unit
     *
     * @param unitId ID of unit to unregister
     * @return true if successful
     */
    bool unregisterProcessingUnit(uint32_t unitId);

    /**
     * @brief Get processing unit by ID
     *
     * @param unitId Unit ID
     * @return Pointer to processing unit or nullptr
     */
    ProcessingUnit* getProcessingUnit(uint32_t unitId) const;

    /**
     * @brief Get all registered processing units
     */
    std::vector<ProcessingUnit*> getAllProcessingUnits() const;

    /**
     * @brief Get processing unit count
     */
    size_t getProcessingUnitCount() const noexcept;

    // =================================================================
    // ADAPTER INJECTION
    // =================================================================

    /**
     * @brief Register an adapter with the system under a URI key
     *
     * Call before initialize(). The system does NOT own the adapter —
     * the caller manages its lifetime. Retrieve inside
     * initializeBlocksLayout() via getAdapter().
     */
    void addAdapter(axonvex::adapters::AdapterInterface* adapter, const std::string& uri);

    // =================================================================
    // SAFETY HOOK INJECTION
    // =================================================================

    /**
     * @brief Register a core-owned SafetyHook with the system (C2)
     *
     * The system registers an emergency callback on the hook: when the hook's
     * emergency stop engages, the system performs an emergency shutdown.
     * Call before initialize(). The system does NOT own the hook — the caller
     * manages its lifetime. The hook must outlive the system OR be cleared
     * (setSafetyHook(nullptr)) before the hook is destroyed; the system's
     * destructor clears its registration on the hook automatically. Passing
     * nullptr clears the registration.
     */
    void setSafetyHook(SafetyHook* hook);

    /**
     * @brief Retrieve the previously registered SafetyHook
     *
     * @return Pointer to the SafetyHook or nullptr if none registered
     */
    SafetyHook* getSafetyHook() const;
    // =================================================================
    // CONFIGURATION MANAGEMENT
    // =================================================================

    /**
     * @brief Get system configuration
     */
    const SystemConfiguration& getSystemConfiguration() const noexcept;

    /**
     * @brief Update system configuration
     *
     * @param config New configuration
     * @return true if update successful
     */
    bool updateSystemConfiguration(const SystemConfiguration& config);

    /**
     * @brief Get framework configuration object
     */
    Configuration& getConfiguration() noexcept;
    const Configuration& getConfiguration() const noexcept;

    /**
     * @brief Load configuration from file
     *
     * @param filePath Configuration file path
     * @return true if successful
     */
    bool loadConfiguration(const Path& filePath);

    // =================================================================
    // EVENT AND CALLBACK MANAGEMENT
    // =================================================================

    /**
     * @brief Register event callback
     *
     * @param callback Event callback function
     * @return Callback ID for unregistering
     */
    uint32_t registerEventCallback(EventCallback callback);

    /**
     * @brief Unregister event callback
     *
     * @param callbackId Callback ID to unregister
     */
    void unregisterEventCallback(uint32_t callbackId);

    /**
     * @brief Register health check callback
     *
     * @param callback Health check callback
     * @return Callback ID
     */
    uint32_t registerHealthCheckCallback(HealthCheckCallback callback);

    // =================================================================
    // RESOURCE MANAGEMENT
    // =================================================================

    /**
     * @brief Get memory usage in bytes
     */
    size_t getMemoryUsage() const noexcept;

    /**
     * @brief Get peak memory usage in bytes
     */
    size_t getPeakMemoryUsage() const noexcept;

    /**
     * @brief Get resource utilization report
     */
    std::string getResourceReport() const;

    // =================================================================
    // LOGGING AND DIAGNOSTICS
    // =================================================================

    /**
     * @brief Get system logger
     */
    Logger& getLogger() noexcept;
    const Logger& getLogger() const noexcept;

    /**
     * @brief Enable/disable debug mode
     */
    void setDebugMode(bool enabled) noexcept;

    /**
     * @brief Check if debug mode is enabled
     */
    bool isDebugMode() const noexcept;

    /**
     * @brief Get comprehensive system report
     */
    std::string getSystemReport() const;

    // =================================================================
    // SYSTEM PORT MANAGEMENT
    // =================================================================

    /**
     * @brief Assign a ProcessingUnit's input port as a system input port
     *
     * This allows the system to expose internal ProcessingUnit ports as
     * external system interfaces, enabling subsystem composition and
     * cross-system data flow.
     *
     * @param systemPortName Name for the system-level port
     * @param unit ProcessingUnit that owns the port
     * @param unitPortId ID of the port within the ProcessingUnit
     * @return true if assignment successful
     */
    bool assignSystemInputPort(const std::string& systemPortName, ProcessingUnit* unit,
                               int unitPortId);

    /**
     * @brief Assign a ProcessingUnit's output port as a system output port
     *
     * @param systemPortName Name for the system-level port
     * @param unit ProcessingUnit that owns the port
     * @param unitPortId ID of the port within the ProcessingUnit
     * @return true if assignment successful
     */
    bool assignSystemOutputPort(const std::string& systemPortName, ProcessingUnit* unit,
                                int unitPortId);

    /**
     * @brief Remove a system input port assignment
     *
     * @param systemPortName Name of the system port to remove
     * @return true if removal successful
     */
    bool removeSystemInputPort(const std::string& systemPortName);

    /**
     * @brief Remove a system output port assignment
     *
     * @param systemPortName Name of the system port to remove
     * @return true if removal successful
     */
    bool removeSystemOutputPort(const std::string& systemPortName);

    /**
     * @brief Get system input port by name
     *
     * @param portName Name of the system input port
     * @return Pointer to the BasePort or nullptr if not found
     */
    BasePort* getSystemInputPort(const std::string& portName) const;

    /**
     * @brief Get system output port by name
     *
     * @param portName Name of the system output port
     * @return Pointer to the BasePort or nullptr if not found
     */
    BasePort* getSystemOutputPort(const std::string& portName) const;

    /**
     * @brief Get all system input port names
     */
    std::vector<std::string> getSystemInputPortNames() const;

    /**
     * @brief Get all system output port names
     */
    std::vector<std::string> getSystemOutputPortNames() const;

    /**
     * @brief Check if system has input port with given name
     */
    bool hasSystemInputPort(const std::string& portName) const;

    /**
     * @brief Check if system has output port with given name
     */
    bool hasSystemOutputPort(const std::string& portName) const;

    /**
     * @brief Connect this system's output to another system's input
     *
     * This enables cross-system data flow by connecting system-level ports.
     *
     * @param outputPortName Name of this system's output port
     * @param targetSystem Target system to connect to
     * @param inputPortName Name of target system's input port
     * @return true if connection successful
     */
    template <typename T>
    bool connectToSystem(const std::string& outputPortName, AxonVexSystem* targetSystem,
                         const std::string& inputPortName);

    /**
     * @brief Disconnect this system's output from another system
     *
     * @param outputPortName Name of this system's output port
     * @param targetSystem Target system to disconnect from
     * @param inputPortName Name of target system's input port
     * @return true if disconnection successful
     */
    template <typename T>
    bool disconnectFromSystem(const std::string& outputPortName, AxonVexSystem* targetSystem,
                              const std::string& inputPortName);

    /**
     * @brief Get system port connection information
     */
    std::string getSystemPortInfo() const;

  protected:
    // Core utility members for system-level performance and diagnostics
    // High-precision timer for system-level diagnostics (e.g., initialization, shutdown, health
    // checks)
    mutable PrecisionTimer systemTimer_{PrecisionTimer::DEFAULT_MAX_SAMPLES};
    // Event publishing, system-level message passing, and deferred actions
    // are eventBus_'s job (below) -- see eventBus.hpp for the pool/queue it
    // owns internally.
  private:
    // =================================================================
    // INTERNAL STATE
    // =================================================================
    // Core configuration and state
    SystemConfiguration systemConfig_;

    // Lifecycle FSM, shutdown latch, worker-thread refusal matrix, teardown
    // ownership token (Phase 2 decomposition step 6, LAST -- see
    // lifecycleController.hpp for the transition()/isOnWorkerThread()/
    // acquireTeardown() contract this class's transitionState()/
    // isOnWorkerThread() and every lifecycle method's latch/teardown-lock
    // touches now delegate to). Declared early: its constructor callables
    // capture `this` and read eventBus_/healthMonitor_/timingController_
    // lazily (only when actually invoked, always well after this whole
    // constructor has finished), so unlike healthMonitor_'s `EventBus&`
    // reference member, no other collaborator needs to exist yet at THIS
    // object's construction time -- declaration position here is a
    // readability choice (spec's collaborator list order), not a
    // correctness requirement.
    LifecycleController lifecycleController_;

    // Core components
    std::unique_ptr<TimingController> timingController_;
    std::unique_ptr<Configuration> configuration_;
    std::unique_ptr<Logger> logger_;

    // Processing unit management (Phase 2 decomposition step 2: extracted to
    // UnitRegistry -- see unitRegistry.hpp for the ownership + id-lookup
    // contract; orchestration (scheduling, events, rollback) stays here).
    UnitRegistry unitRegistry_;

    // Adapter injection
    std::unordered_map<std::string, axonvex::adapters::AdapterInterface*> adapters_;

    // Safety manager injection
    SafetyHook* safetyHook_{nullptr};

    // System port management (Phase 2 decomposition step 1: extracted to
    // SystemPortRegistry — see systemPortRegistry.hpp for the C9 lockWith()
    // protocol this class's connectToSystem<T>/disconnectFromSystem<T>
    // templates rely on).
    SystemPortRegistry systemPorts_;

    // Statistics and monitoring
    mutable SystemStatistics statistics_;
    mutable std::mutex statisticsMutex_;

    // Health and recovery
    std::atomic<bool> debugMode_{false};
    std::atomic<uint32_t> currentRecoveryAttempts_{0};

    // Event system (Phase 2 decomposition step 4: extracted to EventBus --
    // see eventBus.hpp for the pool/queue/dispatch-thread/callback-registry
    // contract this class's registerEventCallback/unregisterEventCallback and
    // every publish call site now delegate to). Declared before
    // healthMonitor_ (below): healthMonitor_'s constructor takes a live
    // `EventBus&` reference, so eventBus_ must already exist -- constructor
    // init order follows declaration order, not the order arguments are
    // written in AxonVexSystem's own constructor.
    EventBus eventBus_;

    // Monitoring thread + health checks + statistics-refresh trigger (Phase
    // 2 decomposition step 5: extracted to HealthMonitor -- see
    // healthMonitor.hpp for the monitor-thread/callback-registry/
    // performHealthCheck contract this class's performHealthCheck/getHealth/
    // registerHealthCheckCallback now delegate to). Declared LAST, after
    // eventBus_: (1) its constructor takes `EventBus&` -- eventBus_ must
    // already be constructed; (2) C++ destroys members in reverse
    // declaration order, so ~HealthMonitor() (which joins the monitor
    // thread) runs before ~EventBus() -- the same ordering every lifecycle
    // method below also enforces explicitly (stop the monitor before
    // touching anything its injected providers read).
    HealthMonitor healthMonitor_;

    // =================================================================
    // INTERNAL METHODS
    // =================================================================
    // State management: thin wrappers delegating to lifecycleController_
    // (Phase 2 decomposition step 6). notifyStateChange()/logStateTransition()
    // still build/log/publish here -- lifecycleController_ invokes them as
    // its injected onTransitioned hook, since it must not depend on
    // eventBus_/logger_/statistics_ itself.
    bool transitionState(SystemState newState);
    void notifyStateChange(SystemState oldState, SystemState newState);
    /**
     * C41/C42: true iff called from eventBus_'s dispatch thread,
     * healthMonitor_'s monitor thread, or RealTimeScheduler's own scheduler
     * thread (via timingController_->isOnSchedulerThread()) -- delegates to
     * lifecycleController_.isOnWorkerThread(), which aggregates the three
     * injected predicates wired at construction (see lifecycleController.hpp).
     */
    bool isOnWorkerThread() const noexcept;
    // Component lifecycle
    bool initializeComponents();
    bool startComponents();
    bool pauseComponents();
    bool resumeComponents();
    bool stopComponents(std::chrono::milliseconds timeout);
    void cleanupComponents();
    // Monitoring and health: façade-owned providers HealthMonitor cannot
    // read itself (touches timingController_/statisticsMutex_ directly).
    void updateStatistics();
    // Event handling
    void handleProcessingUnitError(ProcessingUnit* unit, const std::string& error);

    // Recovery
    bool attemptRecovery(const std::string& errorDescription);
    void handleFatalError(const std::string& error);
    // Utility methods
    std::string stateToString(SystemState state) const;
    void logStateTransition(SystemState from, SystemState to);
    bool validateConfiguration(const SystemConfiguration& config) const;
};

// =================================================================
// UTILITY FUNCTIONS
// =================================================================

/**
 * @brief Convert SystemState to string
 */
std::string to_string(SystemState state);

/**
 * @brief Create default system configuration
 */
SystemConfiguration createDefaultSystemConfiguration();

/**
 * @brief Load system configuration from file
 */
SystemConfiguration loadSystemConfigurationFromFile(const Path& filePath);

/**
 * @brief Save system configuration to file
 */
bool saveSystemConfigurationToFile(const SystemConfiguration& config, const Path& filePath);

// =================================================================
// TEMPLATE IMPLEMENTATIONS
// =================================================================

template <typename T>
bool AxonVexSystem::connectToSystem(const std::string& outputPortName, AxonVexSystem* targetSystem,
                                    const std::string& inputPortName) {
    if (!targetSystem) {
        return false;
    }

    auto portLocks = systemPorts_.lockWith(targetSystem->systemPorts_);

    // Find source output port
    auto outputIt = systemPorts_.outputs_.find(outputPortName);
    if (outputIt == systemPorts_.outputs_.end()) {
        if (logger_) {
            logger_->warning("System", "System output port not found: " + outputPortName);
        }
        return false;
    }

    // Find target input port
    auto inputIt = targetSystem->systemPorts_.inputs_.find(inputPortName);
    if (inputIt == targetSystem->systemPorts_.inputs_.end()) {
        if (logger_) {
            logger_->warning("System", "Target system input port not found: " + inputPortName);
        }
        return false;
    }

    // Cast to typed ports
    auto* outputPort = dynamic_cast<OutputPort<T>*>(outputIt->second);
    auto* inputPort = dynamic_cast<InputPort<T>*>(inputIt->second);

    if (!outputPort || !inputPort) {
        if (logger_) {
            logger_->error("System", "Port type mismatch in system connection");
        }
        return false;
    }

    // Make the connection
    try {
        outputPort->connect(inputPort);

        if (logger_) {
            logger_->info("System", "Connected system '" + systemConfig_.systemName + "." +
                                        outputPortName + "' to '" +
                                        targetSystem->systemConfig_.systemName + "." +
                                        inputPortName + "'");
        }

        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to connect systems: " + std::string(e.what()));
        }
        return false;
    }
}

template <typename T>
bool AxonVexSystem::disconnectFromSystem(const std::string& outputPortName,
                                         AxonVexSystem* targetSystem,
                                         const std::string& inputPortName) {
    if (!targetSystem) {
        return false;
    }

    auto portLocks = systemPorts_.lockWith(targetSystem->systemPorts_);

    // Find source output port
    auto outputIt = systemPorts_.outputs_.find(outputPortName);
    if (outputIt == systemPorts_.outputs_.end()) {
        return false;
    }

    // Find target input port
    auto inputIt = targetSystem->systemPorts_.inputs_.find(inputPortName);
    if (inputIt == targetSystem->systemPorts_.inputs_.end()) {
        return false;
    }

    // Cast to typed ports
    auto* outputPort = dynamic_cast<OutputPort<T>*>(outputIt->second);
    auto* inputPort = dynamic_cast<InputPort<T>*>(inputIt->second);

    if (!outputPort || !inputPort) {
        return false;
    }

    // Make the disconnection
    try {
        outputPort->disconnect(inputPort);

        if (logger_) {
            logger_->info("System", "Disconnected system '" + systemConfig_.systemName + "." +
                                        outputPortName + "' from '" +
                                        targetSystem->systemConfig_.systemName + "." +
                                        inputPortName + "'");
        }

        return true;
    } catch (const std::exception& e) {
        if (logger_) {
            logger_->error("System", "Failed to disconnect systems: " + std::string(e.what()));
        }
        return false;
    }
}

} // namespace axonvex::core
