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

namespace axonvex::adapters { class AdapterInterface; }
namespace axonvex::safety { class SafetyManager; }

#include <atomic>
#include <axonvex_core/configuration.hpp>
#include <axonvex_core/logger.hpp>
#include <axonvex_core/utils/containers/memoryPool.hpp>
#include <axonvex_core/path.hpp>
#include <axonvex_core/precisionTimer.hpp>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/utils/containers/threadSafeQueue.hpp>
#include <axonvex_core/timingController.hpp>
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

// Bring utils containers into core namespace for convenience
using axonvex::utils::containers::MemoryPool;
using axonvex::utils::containers::ThreadSafeQueue;

/**
 * @brief System state enumeration
 */
enum class SystemState {
    UNINITIALIZED = 0, ///< System not yet initialized
    INITIALIZING,      ///< System currently initializing
    INITIALIZED,       ///< System initialized but not started
    STARTING,          ///< System currently starting
    RUNNING,           ///< System running normally
    PAUSING,           ///< System currently pausing
    PAUSED,            ///< System paused
    RESUMING,          ///< System resuming from pause
    STOPPING,          ///< System currently stopping
    STOPPED,           ///< System stopped
    ERROR,             ///< System in error state
    FATAL_ERROR        ///< System in unrecoverable error state
};

/**
 * @brief System configuration structure
 */
struct SystemConfiguration {
    // Core settings
    std::string systemName{"AxonVex-System"};
    std::string version{"1.0.0"};
    LogLevel logLevel{LogLevel::Info};

    // Timing settings
    SchedulingPolicy defaultSchedulingPolicy{SchedulingPolicy::PRIORITY_BASED};
    std::chrono::microseconds systemTickRate{std::chrono::microseconds(100)};
    bool enableRealTimeScheduling{true};

    // Resource limits
    size_t maxProcessingUnits{1000};
    size_t maxMemoryPoolSize{64 * 1024 * 1024}; // 64MB
    size_t loggerQueueSize{16384};

    // Monitoring settings
    bool enablePerformanceMonitoring{true};
    std::chrono::seconds statisticsUpdateInterval{1};
    std::chrono::seconds healthCheckInterval{5};

    // Recovery settings
    bool enableAutoRecovery{true};
    uint32_t maxRecoveryAttempts{3};
    std::chrono::seconds recoveryTimeout{10};

    // File paths
    std::string configFilePath{"config/system.json"};
    std::string logFilePath{"logs/axonvex_system.log"};
    bool enableFileLogging{true};

    // Event system configuration
    size_t eventPoolSize{1024};
    size_t eventQueueSize{256};

    void validate() const;
    std::string toString() const;
};

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
 * @brief System event structure for callbacks
 */
struct SystemEvent {
    enum class Type {
        STATE_CHANGE,
        PROCESSING_UNIT_ADDED,
        PROCESSING_UNIT_REMOVED,
        PROCESSING_UNIT_ERROR,
        PERFORMANCE_ALERT,
        CONFIGURATION_CHANGED,
        RECOVERY_STARTED,
        RECOVERY_COMPLETED,
        HEALTH_CHECK,
        SHUTDOWN_REQUESTED
    };

    Type type;
    SystemState oldState;
    SystemState newState;
    std::string description;
    std::chrono::steady_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
};

/**
 * @brief System health information
 */
struct SystemHealth {
    enum class Status { HEALTHY, WARNING, CRITICAL, FAILURE };

    Status overallStatus{Status::HEALTHY};
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::chrono::steady_clock::time_point lastCheckTime;

    // Component health
    bool timingControllerHealthy{true};
    bool configurationHealthy{true};
    bool loggerHealthy{true};
    bool memoryHealthy{true};

    // Performance indicators
    double cpuUtilization{0.0};
    double memoryUtilization{0.0};
    double averageExecutionTime{0.0};
    double missedDeadlineRatio{0.0};

    std::string getStatusString() const;
    bool isHealthy() const {
        return overallStatus == Status::HEALTHY;
    }
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
    using RecoveryCallback = std::function<bool(const std::string&)>;

    /**
     * @brief Constructor with optional configuration
     */
    explicit AxonVexSystem(const SystemConfiguration& config = SystemConfiguration{});

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~AxonVexSystem();

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
     */
    bool initialize(const std::string& configPath = "");

    /**
     * @brief Initialize from existing configuration object
     *
     * @param config Configuration object
     * @return true if initialization successful
     */
    bool initialize(const Configuration& config);

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
     * @return true if stop successful
     */
    bool stop(std::chrono::milliseconds timeoutMs = std::chrono::milliseconds(5000));

    /**
     * @brief Emergency shutdown (immediate stop)
     */
    void emergencyShutdown();

    /**
     * @brief Reset the system to uninitialized state
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
    void addAdapter(axonvex::adapters::AdapterInterface* adapter,
                    const std::string& uri);

    // =================================================================
    // SAFETY MANAGER INJECTION
    // =================================================================

    /**
     * @brief Register a SafetyManager with the system
     *
     * Call before initialize(). The system does NOT own the manager —
     * the caller manages its lifetime. Retrieve inside
     * initializeBlocksLayout() via getSafetyManager().
     */
    void setSafetyManager(axonvex::safety::SafetyManager* manager);

  protected:
    /**
     * @brief Retrieve the previously registered SafetyManager
     *
     * Available inside initializeBlocksLayout() and during runtime.
     *
     * @return Pointer to the SafetyManager or nullptr if none registered
     */
    axonvex::safety::SafetyManager* getSafetyManager() const;

  public:
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

    /**
     * @brief Save configuration to file
     *
     * @param filePath Configuration file path (optional)
     * @return true if successful
     */
    bool saveConfiguration(const Path& filePath = Path{}) const;

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

    /**
     * @brief Register recovery callback
     *
     * @param callback Recovery callback
     * @return Callback ID
     */
    uint32_t registerRecoveryCallback(RecoveryCallback callback);

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
     * @brief Trigger garbage collection
     */
    void collectGarbage();

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

    /**
     * @brief Export system diagnostics to file
     */
    bool exportDiagnostics(const Path& filePath) const;

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
    // Thread-safe queue for event publishing, system-level message passing, or deferred actions
    // Example: ThreadSafeQueue<SystemEvent> eventQueue_;
    // Memory pool for real-time safe allocation of system event objects
    // Example: MemoryPool<SystemEvent> eventPool_;
    // Usage hooks:
    // - Use systemTimer_ for timing system operations
    // - Use ThreadSafeQueue for event/message passing
    // - Use MemoryPool for system event allocation
  private:
    // =================================================================
    // INTERNAL STATE
    // =================================================================
    // Core configuration and state
    SystemConfiguration systemConfig_;
    mutable std::atomic<SystemState> currentState_{SystemState::UNINITIALIZED};
    mutable std::mutex stateMutex_;

    // Core components
    std::unique_ptr<TimingController> timingController_;
    std::unique_ptr<Configuration> configuration_;
    std::unique_ptr<Logger> logger_;

    // Processing unit management
    mutable std::mutex unitsMutex_;
    std::unordered_map<uint32_t, std::unique_ptr<ProcessingUnit>> processingUnits_;
    std::unordered_map<ProcessingUnit*, uint32_t> unitToIdMap_;
    std::atomic<uint32_t> nextUnitId_{1};

    // Adapter injection
    std::unordered_map<std::string, axonvex::adapters::AdapterInterface*> adapters_;

    // Safety manager injection
    axonvex::safety::SafetyManager* safetyManager_{nullptr};

    // System port management
    mutable std::mutex systemPortsMutex_;
    std::unordered_map<std::string, BasePort*> systemInputPorts_;
    std::unordered_map<std::string, BasePort*> systemOutputPorts_;

    // Statistics and monitoring
    mutable SystemStatistics statistics_;
    mutable std::mutex statisticsMutex_;
    std::unique_ptr<std::thread> monitoringThread_;
    std::atomic<bool> monitoringEnabled_{false};

    // Event system
    std::unique_ptr<ThreadSafeQueue<SystemEvent*>> eventQueue_;
    std::unique_ptr<MemoryPool<SystemEvent>> eventPool_;
    std::unique_ptr<std::thread> eventProcessingThread_;
    std::atomic<bool> eventProcessingRunning_{false};
    std::vector<EventCallback> eventCallbacks_;
    std::vector<HealthCheckCallback> healthCheckCallbacks_;
    std::vector<RecoveryCallback> recoveryCallbacks_;
    mutable std::mutex callbacksMutex_;
    std::atomic<uint32_t> nextCallbackId_{1};

    // Health and recovery
    mutable SystemHealth lastHealth_;
    std::atomic<bool> debugMode_{false};
    std::atomic<uint32_t> currentRecoveryAttempts_{0};
    std::atomic<bool> isShuttingDown_{false}; // New flag for graceful shutdown

    // =================================================================
    // INTERNAL METHODS
    // =================================================================
    // State management
    bool transitionState(SystemState newState);
    void notifyStateChange(SystemState oldState, SystemState newState);
    // Component lifecycle
    bool initializeComponents();
    bool startComponents();
    bool pauseComponents();
    bool resumeComponents();
    bool stopComponents(std::chrono::milliseconds timeout);
    void cleanupComponents();
    // Monitoring and health
    void monitoringLoop();
    void updateStatistics();
    SystemHealth performInternalHealthCheck() const;
    // Event handling
    void publishEvent(const SystemEvent& event);
    void handleProcessingUnitError(ProcessingUnit* unit, const std::string& error);
    void eventProcessingLoop(); // New method for processing events

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

    std::lock_guard<std::mutex> lock1(systemPortsMutex_);
    std::lock_guard<std::mutex> lock2(targetSystem->systemPortsMutex_);

    // Find source output port
    auto outputIt = systemOutputPorts_.find(outputPortName);
    if (outputIt == systemOutputPorts_.end()) {
        if (logger_) {
            logger_->warning("System", "System output port not found: " + outputPortName);
        }
        return false;
    }

    // Find target input port
    auto inputIt = targetSystem->systemInputPorts_.find(inputPortName);
    if (inputIt == targetSystem->systemInputPorts_.end()) {
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

    std::lock_guard<std::mutex> lock1(systemPortsMutex_);
    std::lock_guard<std::mutex> lock2(targetSystem->systemPortsMutex_);

    // Find source output port
    auto outputIt = systemOutputPorts_.find(outputPortName);
    if (outputIt == systemOutputPorts_.end()) {
        return false;
    }

    // Find target input port
    auto inputIt = targetSystem->systemInputPorts_.find(inputPortName);
    if (inputIt == targetSystem->systemInputPorts_.end()) {
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
