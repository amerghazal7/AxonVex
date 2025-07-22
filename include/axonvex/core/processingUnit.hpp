/**
 * @file processingUnit.hpp
 * @brief Advanced Processing Unit for AxonVex Framework
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * Advanced ProcessingUnit Block architecture with
 * integer-indexed ports, dual sync/async processing, and built-in
 * control functionality.
 */

#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>
#include <filesystem>
#include <stdexcept>

#include "ports.hpp"
#include <axonvex/core/precisionTimer.hpp>

namespace axonvex::core {

// Forward declarations
class AxonVexSystem;

/**
 * @brief Execution state for processing units
 */
enum class ExecutionState {
    UNINITIALIZED = 0,
    INITIALIZED,
    RUNNING,
    DISABLED,
    ERROR
};

/**
 * @brief Built-in control port indices (reserved range 100-199)
 */
namespace ControlPorts {
    constexpr int RESET = 100;
    constexpr int DISABLE = 101;
    constexpr int ENABLE = 102;
}

/**
 * @brief Advanced Processing Unit
 * 
 * Key improvements over basic ProcessingUnit:
 * - Integer-indexed ports for O(1) lookup performance
 * - Separate sync and async processing methods
 * - Built-in reset and disable functionality
 * - Down-sampling factor with automatic propagation
 * - Hierarchical URL/URI addressing system
 * - Bridge port support for complex routing
 */
class ProcessingUnit {
public:
    explicit ProcessingUnit(const std::string& name);
    virtual ~ProcessingUnit();
    
    // Non-copyable, movable
    ProcessingUnit(const ProcessingUnit&) = delete;
    ProcessingUnit& operator=(const ProcessingUnit&) = delete;
    ProcessingUnit(ProcessingUnit&&) = default;
    ProcessingUnit& operator=(ProcessingUnit&&) = default;
    
    // =================================================================
    // CORE PROCESSING INTERFACE
    // =================================================================
    
    /**
     * @brief Pure virtual synchronous processing method
     * 
     * Called at regular intervals based on system timing.
     * Should be deterministic and real-time safe.
     */
    virtual void processSync() = 0;
    
    /**
     * @brief Pure virtual asynchronous processing method
     * 
     * Called when async input ports receive data.
     * Used for event-driven processing and control signals.
     */
    virtual void processAsync() = 0;
    
    /**
     * @brief Pure virtual reset method
     * 
     * Called to reset the processing unit to initial state.
     * Must be implemented by derived classes.
     */
    virtual void reset() = 0;
    
    /**
     * @brief Pure virtual initialization method
     * 
     * Called once during system initialization.
     * Must be implemented by derived classes.
     */
    virtual void initialize() = 0;
    
    /**
     * @brief Virtual finalization method
     * 
     * Called during cleanup.
     * Override to perform custom cleanup.
     */
    virtual void finalize() {} // Optional cleanup
    
    /**
     * @brief Get type description for debugging/visualization
     */
    virtual std::string getTypeDescription() = 0;
    
    // =================================================================
    // BASE PROCESSING METHODS (internal use)
    // =================================================================
    
    /**
     * @brief Base synchronous processing with down-sampling
     * 
     * Handles down-sampling counter and calls processSync()
     */
    void processSyncBase();
    
    /**
     * @brief Base asynchronous processing with built-in controls
     * 
     * Handles reset/disable ports and calls processAsync()
     */
    void processAsyncBase();
    
    // =================================================================
    // PORT MANAGEMENT
    // =================================================================
    
    /**
     * @brief Create synchronous input port
     * 
     * @param idx Integer index for the port (must be unique)
     * @param name Descriptive name for the port
     * @return Pointer to created input port
     */
    template<typename T>
    InputPort<T>* createInputPort(int idx, const std::string& name);
    
    /**
     * @brief Create synchronous output port
     * 
     * @param idx Integer index for the port (must be unique)
     * @param name Descriptive name for the port
     * @return Pointer to created output port
     */
    template<typename T>
    OutputPort<T>* createOutputPort(int idx, const std::string& name);
    
    /**
     * @brief Create asynchronous input port
     * 
     * @param idx Integer index for the port (must be unique)
     * @param name Descriptive name for the port
     * @return Pointer to created async input port
     */
    template<typename T>
    AsyncInputPort<T>* createAsyncInputPort(int idx, const std::string& name);
    
    /**
     * @brief Create asynchronous output port
     * 
     * @param idx Integer index for the port (must be unique)
     * @param name Descriptive name for the port
     * @return Pointer to created async output port
     */
    template<typename T>
    AsyncOutputPort<T>* createAsyncOutputPort(int idx, const std::string& name);
    
    /**
     * @brief Get synchronous input port by index
     * 
     * @param idx Port index
     * @return Pointer to input port or nullptr if not found
     */
    template<typename T>
    InputPort<T>* getInputPort(int idx);
    
    /**
     * @brief Get synchronous input port (default - single port behavior)
     * 
     * @return Pointer to the single input port if only one exists
     * @throws std::runtime_error if multiple ports exist
     */
    template<typename T>
    InputPort<T>* getInputPort();
    
    /**
     * @brief Get synchronous output port by index
     */
    template<typename T>
    OutputPort<T>* getOutputPort(int idx);
    
    /**
     * @brief Get synchronous output port (default - single port behavior)
     */
    template<typename T>
    OutputPort<T>* getOutputPort();
    
    /**
     * @brief Get asynchronous input port by index
     */
    template<typename T>
    AsyncInputPort<T>* getAsyncInputPort(int idx);
    
    /**
     * @brief Get asynchronous input port (default - single port behavior)
     */
    template<typename T>
    AsyncInputPort<T>* getAsyncInputPort();
    
    /**
     * @brief Get asynchronous output port by index
     */
    template<typename T>
    AsyncOutputPort<T>* getAsyncOutputPort(int idx);
    
    /**
     * @brief Get asynchronous output port (default - single port behavior)
     */
    template<typename T>
    AsyncOutputPort<T>* getAsyncOutputPort();
    
    // Port name access
    std::string getInputPortName(int idx) const;
    std::string getOutputPortName(int idx) const;
    std::string getAsyncInputPortName(int idx) const;
    std::string getAsyncOutputPortName(int idx) const;
    
    // Port collection access
    const std::map<int, BasePort*>& getInputPorts() const { return inputPorts_; }
    const std::map<int, BasePort*>& getOutputPorts() const { return outputPorts_; }
    const std::map<int, BasePort*>& getAsyncInputPorts() const { return asyncInputPorts_; }
    const std::map<int, BasePort*>& getAsyncOutputPorts() const { return asyncOutputPorts_; }
    
    // =================================================================
    // DOWN-SAMPLING CONTROL
    // =================================================================
    
    /**
     * @brief Set down-sampling factor
     * 
     * Controls how often processSync() is called relative to
     * the base system frequency.
     * 
     * @param factor Down-sampling factor (1 = every cycle, 2 = every other cycle, etc.)
     */
    void setDownSamplingFactor(int factor);
    
    /**
     * @brief Get current down-sampling factor
     */
    int getDownSamplingFactor() const noexcept { return downSamplingFactor_; }
    
    /**
     * @brief Inherit down-sampling factor from another processing unit
     * 
     * Used during connection to propagate sampling rates through the system.
     * 
     * @param source Source processing unit to inherit from
     */
    void inheritDownSamplingFactor(const ProcessingUnit* source);
    
    /**
     * @brief Set block sampling period
     */
    void setBlockSamplingPeriod(std::chrono::microseconds period);
    
    /**
     * @brief Get block sampling period
     */
    std::chrono::microseconds getBlockSamplingPeriod() const noexcept { return samplingPeriod_; }
    
    // =================================================================
    // UNIQUE ID MANAGEMENT
    // =================================================================
    
    /**
     * @brief Get unique block ID
     */
    uint32_t getBlockUID() const noexcept { return blockUID_; }
    
    /**
     * @brief Set unique block ID (called by system)
     */
    void setBlockUID(uint32_t uid);
    
    /**
     * @brief Check if block has been added to a system
     */
    bool hasBeenAddedToSystem() const noexcept { return hasBeenAddedToSystem_; }
    
    // =================================================================
    // HIERARCHICAL ADDRESSING
    // =================================================================
    
    /**
     * @brief Set URL/URI for hierarchical addressing
     */
    void setURL(const std::string& url);
    
    /**
     * @brief Get relative URL
     */
    std::filesystem::path getRelativeURL() const { return relativeURL_; }
    
    /**
     * @brief Get absolute URL
     */
    std::filesystem::path getAbsoluteURL() const { return absoluteURL_; }
    
    /**
     * @brief Set parent block for hierarchical systems
     */
    void setParentBlock(ProcessingUnit* parent) { parentBlock_ = parent; }
    
    /**
     * @brief Get parent block
     */
    ProcessingUnit* getParentBlock() const { return parentBlock_; }
    
    // =================================================================
    // STATE MANAGEMENT
    // =================================================================
    
    /**
     * @brief Get current execution state
     */
    ExecutionState getState() const noexcept { return state_.load(); }
    
    /**
     * @brief Check if unit is initialized
     */
    bool isInitialized() const { return state_ != ExecutionState::UNINITIALIZED; }
    
    /**
     * @brief Check if unit is running
     */
    bool isRunning() const { return state_ == ExecutionState::RUNNING; }
    
    /**
     * @brief Enable/disable the processing unit
     */
    void setDisabled(bool disabled);
    
    /**
     * @brief Check if processing unit is disabled
     */
    bool isDisabled() const noexcept { return isDisabled_.load(); }
    
    /**
     * @brief Reset the processing unit
     */
    void resetBlock();
    
    /**
     * @brief Reset all ports
     */
    void resetPorts();
    
    /**
     * @brief Set thread safety for all ports
     */
    void setPortsThreadSafe(bool threadSafe);
    
    // =================================================================
    // IDENTIFICATION AND DESCRIPTION
    // =================================================================
    
    /**
     * @brief Get processing unit name
     */
    const std::string& getName() const noexcept { return name_; }
    
    /**
     * @brief Update instance description
     */
    void updateInstanceDescription(const std::string& description);
    
    /**
     * @brief Get instance description
     */
    const std::string& getInstanceDescription() const noexcept { return instanceDescription_; }
    
    // =================================================================
    // PERFORMANCE MONITORING
    // =================================================================
    
    /**
     * @brief Get execution statistics
     */
    struct ExecutionStats {
        uint64_t syncExecutionCount{0};
        uint64_t asyncExecutionCount{0};
        std::chrono::microseconds totalSyncTime{0};
        std::chrono::microseconds totalAsyncTime{0};
        std::chrono::microseconds avgSyncTime{0};
        std::chrono::microseconds avgAsyncTime{0};
        std::chrono::microseconds maxSyncTime{0};
        std::chrono::microseconds maxAsyncTime{0};
    };
    
    ExecutionStats getExecutionStats() const;
    void resetExecutionStats();
    
    // Performance metrics (legacy compatibility)
    struct PerformanceMetrics {
        std::chrono::microseconds executionTime{0};
        std::chrono::microseconds averageExecutionTime{0};
        std::chrono::microseconds maxExecutionTime{0};
        std::chrono::microseconds minExecutionTime{std::chrono::microseconds::max()};
        uint64_t executionCount{0};
        uint64_t missedDeadlines{0};
        double cpuUtilization{0.0};
        double memoryUsage{0.0};
        double throughput{0.0}; // data items per second
        
        void reset() {
            executionTime = std::chrono::microseconds{0};
            averageExecutionTime = std::chrono::microseconds{0};
            maxExecutionTime = std::chrono::microseconds{0};
            minExecutionTime = std::chrono::microseconds::max();
            executionCount = 0;
            missedDeadlines = 0;
            cpuUtilization = 0.0;
            memoryUsage = 0.0;
            throughput = 0.0;
        }
    };
    
    PerformanceMetrics getPerformanceMetrics() const;
    void resetPerformanceMetrics();
    
    // =================================================================
    // EXECUTION TIMING
    // =================================================================
    
    void setExecutionPeriod(std::chrono::microseconds period) { executionPeriod_ = period; }
    std::chrono::microseconds getExecutionPeriod() const { return executionPeriod_; }
    
    // =================================================================
    // ERROR HANDLING
    // =================================================================
    
    bool hasError() const { return state_ == ExecutionState::ERROR; }
    const std::string& getLastError() const { return lastError_; }
    
    // =================================================================
    // DEBUG AND DIAGNOSTICS
    // =================================================================
    
    void setDebugMode(bool enable) { debugMode_ = enable; }
    bool isDebugMode() const { return debugMode_; }

protected:
    // Core utility members for real-time performance
    mutable PrecisionTimer executionTimer_{PrecisionTimer::DEFAULT_MAX_SAMPLES};
    
    // Allow system to access protected members
    friend class AxonVexSystem;
    
    // State management
    void setState(ExecutionState state) { state_.store(state); }
    void setError(const std::string& error);
    
    // Performance tracking
    void updateSyncExecutionStats(std::chrono::microseconds executionTime);
    void updateAsyncExecutionStats(std::chrono::microseconds executionTime);
    
private:
    // Basic information
    std::string name_;
    std::string instanceDescription_;
    uint32_t blockUID_{0};
    bool hasBeenAddedToSystem_{false};
    
    // State management
    std::atomic<ExecutionState> state_{ExecutionState::UNINITIALIZED};
    std::atomic<bool> isDisabled_{false};
    std::string lastError_;
    bool debugMode_{false};
    
    // Execution timing
    std::chrono::microseconds executionPeriod_{std::chrono::milliseconds(10)};
    
    // Down-sampling
    int downSamplingFactor_{1};
    int intraSampleCounter_{0};
    std::chrono::microseconds samplingPeriod_{std::chrono::milliseconds(10)};
    
    // Hierarchical addressing
    std::filesystem::path relativeURL_;
    std::filesystem::path absoluteURL_;
    ProcessingUnit* parentBlock_{nullptr};
    bool hasURLBeenSet_{false};
    
    // Port management
    mutable std::mutex portsMutex_;
    std::map<int, BasePort*> inputPorts_;
    std::map<int, BasePort*> outputPorts_;
    std::map<int, BasePort*> asyncInputPorts_;
    std::map<int, BasePort*> asyncOutputPorts_;
    
    // Port name mappings
    std::map<int, std::string> inputPortNames_;
    std::map<int, std::string> outputPortNames_;
    std::map<int, std::string> asyncInputPortNames_;
    std::map<int, std::string> asyncOutputPortNames_;
    
    // Built-in control ports
    AsyncInputPort<int>* resetPort_{nullptr};
    AsyncInputPort<int>* disablePort_{nullptr};
    
    // Performance statistics
    mutable std::mutex statsMutex_;
    ExecutionStats stats_;
    
    // Port ownership (for automatic cleanup)
    std::vector<std::unique_ptr<BasePort>> ownedPorts_;
};

// Template implementations

template<typename T>
InputPort<T>* ProcessingUnit::createInputPort(int idx, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    // Check if port with this index already exists
    if (inputPorts_.find(idx) != inputPorts_.end()) {
        throw std::runtime_error("Input port with index " + std::to_string(idx) + " already exists");
    }
    
    // Create the port
    auto port = std::make_unique<InputPort<T>>(idx, name, this);
    auto* portPtr = port.get();
    
    // Store in maps
    inputPorts_[idx] = portPtr;
    inputPortNames_[idx] = name;
    
    // Transfer ownership
    ownedPorts_.push_back(std::move(port));
    
    // Set port UID if block UID is available
    if (blockUID_ != 0) {
        portPtr->setPortUID(blockUID_ * 256 + idx);
    }
    
    return portPtr;
}

template<typename T>
OutputPort<T>* ProcessingUnit::createOutputPort(int idx, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    if (outputPorts_.find(idx) != outputPorts_.end()) {
        throw std::runtime_error("Output port with index " + std::to_string(idx) + " already exists");
    }
    
    auto port = std::make_unique<OutputPort<T>>(idx, name, this);
    auto* portPtr = port.get();
    
    outputPorts_[idx] = portPtr;
    outputPortNames_[idx] = name;
    ownedPorts_.push_back(std::move(port));
    
    if (blockUID_ != 0) {
        portPtr->setPortUID(blockUID_ * 256 + idx);
    }
    
    return portPtr;
}

template<typename T>
AsyncInputPort<T>* ProcessingUnit::createAsyncInputPort(int idx, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    if (asyncInputPorts_.find(idx) != asyncInputPorts_.end()) {
        throw std::runtime_error("Async input port with index " + std::to_string(idx) + " already exists");
    }
    
    auto port = std::make_unique<AsyncInputPort<T>>(idx, name, this);
    auto* portPtr = port.get();
    
    asyncInputPorts_[idx] = portPtr;
    asyncInputPortNames_[idx] = name;
    ownedPorts_.push_back(std::move(port));
    
    if (blockUID_ != 0) {
        portPtr->setPortUID(blockUID_ * 256 + idx);
    }
    
    return portPtr;
}

template<typename T>
AsyncOutputPort<T>* ProcessingUnit::createAsyncOutputPort(int idx, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    if (asyncOutputPorts_.find(idx) != asyncOutputPorts_.end()) {
        throw std::runtime_error("Async output port with index " + std::to_string(idx) + " already exists");
    }
    
    auto port = std::make_unique<AsyncOutputPort<T>>(idx, name, this);
    auto* portPtr = port.get();
    
    asyncOutputPorts_[idx] = portPtr;
    asyncOutputPortNames_[idx] = name;
    ownedPorts_.push_back(std::move(port));
    
    if (blockUID_ != 0) {
        portPtr->setPortUID(blockUID_ * 256 + idx);
    }
    
    return portPtr;
}

template<typename T>
InputPort<T>* ProcessingUnit::getInputPort(int idx) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = inputPorts_.find(idx);
    if (it != inputPorts_.end()) {
        return static_cast<InputPort<T>*>(it->second);
    }
    return nullptr;
}

template<typename T>
InputPort<T>* ProcessingUnit::getInputPort() {
    std::lock_guard<std::mutex> lock(portsMutex_);
    if (inputPorts_.size() == 1) {
        return static_cast<InputPort<T>*>(inputPorts_.begin()->second);
    } else {
        std::stringstream errorMessage;
        errorMessage << "Error in Block " << blockUID_ << " at getInputPort(), no default port available";
        throw std::runtime_error(errorMessage.str());
    }
}

template<typename T>
OutputPort<T>* ProcessingUnit::getOutputPort(int idx) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = outputPorts_.find(idx);
    if (it != outputPorts_.end()) {
        return static_cast<OutputPort<T>*>(it->second);
    }
    return nullptr;
}

template<typename T>
OutputPort<T>* ProcessingUnit::getOutputPort() {
    std::lock_guard<std::mutex> lock(portsMutex_);
    if (outputPorts_.size() == 1) {
        return static_cast<OutputPort<T>*>(outputPorts_.begin()->second);
    } else {
        std::stringstream errorMessage;
        errorMessage << "Error in Block " << blockUID_ << " at getOutputPort(), no default port available";
        throw std::runtime_error(errorMessage.str());
    }
}

template<typename T>
AsyncInputPort<T>* ProcessingUnit::getAsyncInputPort(int idx) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncInputPorts_.find(idx);
    if (it != asyncInputPorts_.end()) {
        return static_cast<AsyncInputPort<T>*>(it->second);
    }
    return nullptr;
}

template<typename T>
AsyncInputPort<T>* ProcessingUnit::getAsyncInputPort() {
    std::lock_guard<std::mutex> lock(portsMutex_);
    if (asyncInputPorts_.size() == 3) { // 3 because reset and disable ports are defaults
        // Find the non-control port
        for (auto& [idx, port] : asyncInputPorts_) {
            if (idx != ControlPorts::RESET && idx != ControlPorts::DISABLE) {
                return static_cast<AsyncInputPort<T>*>(port);
            }
        }
    }
    std::stringstream errorMessage;
    errorMessage << "Error in Block " << blockUID_ << " at getAsyncInputPort(), no default port available";
    throw std::runtime_error(errorMessage.str());
}

template<typename T>
AsyncOutputPort<T>* ProcessingUnit::getAsyncOutputPort(int idx) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncOutputPorts_.find(idx);
    if (it != asyncOutputPorts_.end()) {
        return static_cast<AsyncOutputPort<T>*>(it->second);
    }
    return nullptr;
}

template<typename T>
AsyncOutputPort<T>* ProcessingUnit::getAsyncOutputPort() {
    std::lock_guard<std::mutex> lock(portsMutex_);
    if (asyncOutputPorts_.size() == 1) {
        return static_cast<AsyncOutputPort<T>*>(asyncOutputPorts_.begin()->second);
    } else {
        std::stringstream errorMessage;
        errorMessage << "Error in Block " << blockUID_ << " at getAsyncOutputPort(), no default port available";
        throw std::runtime_error(errorMessage.str());
    }
}

} // namespace axonvex::core 