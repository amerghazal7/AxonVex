#pragma once

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <typeinfo>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <algorithm>
#include "precisionTimer.hpp"
#include "threadSafeQueue.hpp"
#include "memoryPool.hpp"

namespace axonvex::core {

// Forward declarations
class ProcessingUnit;
class TimingController;
class PerformanceMonitor;
class ConfigurationManager;

// Performance metrics structure
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

// Process priority levels
enum class ProcessPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    REAL_TIME = 3
};

// Execution state of processing unit
enum class ExecutionState {
    UNINITIALIZED,
    INITIALIZED,
    RUNNING,
    PAUSED,
    STOPPED,
    ERROR
};

// Base port class for type erasure
class BasePort {
public:
    explicit BasePort(int id, const std::string& name, ProcessingUnit* owner);
    virtual ~BasePort() = default;
    
    int getId() const { return id_; }
    const std::string& getName() const { return name_; }
    ProcessingUnit* getOwner() const { return owner_; }
    
    virtual std::string getDataTypeName() const = 0;
    virtual bool hasNewData() const = 0;
    virtual void clearNewDataFlag() = 0;
    
protected:
    int id_;
    std::string name_;
    ProcessingUnit* owner_;
};

// Input port template class
template<typename T>
class InputPort : public BasePort {
public:
    using ValidationCallback = std::function<bool(const T&)>;
    using DataCallback = std::function<void(const T&)>;
    
    explicit InputPort(int id, const std::string& name, ProcessingUnit* owner);
    ~InputPort() override = default;
    
    // Data access
    T read() const;
    bool hasNewData() const override;
    void clearNewDataFlag() override;
    
    // Validation and callbacks
    void setValidationCallback(ValidationCallback callback);
    void setDataCallback(DataCallback callback);
    
    // Connection management
    void writeData(const T& data);
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    
    // Statistics
    uint64_t getTotalMessages() const { return totalMessages_; }
    uint64_t getValidMessages() const { return validMessages_; }
    uint64_t getInvalidMessages() const { return invalidMessages_; }
    
private:
    mutable std::mutex dataMutex_;
    T data_;
    std::atomic<bool> hasNewData_{false};
    ValidationCallback validationCallback_;
    DataCallback dataCallback_;
    
    // Statistics
    std::atomic<uint64_t> totalMessages_{0};
    std::atomic<uint64_t> validMessages_{0};
    std::atomic<uint64_t> invalidMessages_{0};
};

// Output port template class
template<typename T>
class OutputPort : public BasePort {
public:
    using OutputCallback = std::function<void(const T&)>;
    
    explicit OutputPort(int id, const std::string& name, ProcessingUnit* owner);
    ~OutputPort() override = default;
    
    // Data output
    void write(const T& data);
    
    // Connection management
    void connect(InputPort<T>* inputPort);
    void disconnect(InputPort<T>* inputPort);
    void disconnectAll();
    bool isConnected() const { return !connectedPorts_.empty(); }
    size_t getConnectionCount() const { return connectedPorts_.size(); }
    
    // Callbacks
    void setOutputCallback(OutputCallback callback);
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    bool hasNewData() const override { return false; } // Output ports don't have new data flag
    void clearNewDataFlag() override {} // No-op for output ports
    
    // Statistics
    uint64_t getTotalMessages() const { return totalMessages_; }
    
private:
    std::mutex connectionMutex_;
    std::vector<InputPort<T>*> connectedPorts_;
    OutputCallback outputCallback_;
    
    // Statistics
    std::atomic<uint64_t> totalMessages_{0};
};

// Processing unit base class
class ProcessingUnit {
public:
    explicit ProcessingUnit(const std::string& name);
    virtual ~ProcessingUnit();
    
    // Core execution interface - to be implemented by derived classes
    virtual void processSync() = 0;
    virtual void processAsync() = 0;
    virtual void reset() = 0;
    virtual void initialize() = 0;
    virtual void finalize() {} // Optional cleanup
    
    // Lifecycle management
    ExecutionState getState() const { return state_; }
    bool isInitialized() const { return state_ != ExecutionState::UNINITIALIZED; }
    bool isRunning() const { return state_ == ExecutionState::RUNNING; }
    
    // Port management
    template<typename T>
    InputPort<T>* createInputPort(int id, const std::string& name);
    
    template<typename T>
    OutputPort<T>* createOutputPort(int id, const std::string& name);
    
    BasePort* getPort(int id) const;
    BasePort* getPort(const std::string& name) const;
    std::vector<BasePort*> getAllPorts() const;
    // Note: Type-erased port access would require a more sophisticated type registry
    // For now, use getAllPorts() and cast as needed, or use port names/IDs
    
    // Performance monitoring
    PerformanceMetrics getPerformanceMetrics() const;
    void resetPerformanceMetrics();
    
    // Configuration
    const std::string& getName() const { return name_; }
    void setProcessPriority(ProcessPriority priority) { priority_ = priority; }
    ProcessPriority getProcessPriority() const { return priority_; }
    
    // Execution timing
    void setExecutionPeriod(std::chrono::microseconds period) { executionPeriod_ = period; }
    std::chrono::microseconds getExecutionPeriod() const { return executionPeriod_; }
    
    // Error handling
    bool hasError() const { return state_ == ExecutionState::ERROR; }
    const std::string& getLastError() const { return lastError_; }
    
    // Debug and diagnostics
    void setDebugMode(bool enable) { debugMode_ = enable; }
    bool isDebugMode() const { return debugMode_; }
    
protected:
    // Core utility members for real-time performance
    // High-precision timer for profiling processing steps
    mutable PrecisionTimer executionTimer_{PrecisionTimer::DEFAULT_MAX_SAMPLES};
    // Thread-safe queue for input/output buffering (optional, can be used by derived classes or ports)
    // Example: ThreadSafeQueue<std::vector<uint8_t>> inputQueue_;
    // Example: ThreadSafeQueue<std::vector<uint8_t>> outputQueue_;
    // For generic use, leave as void* or template in derived classes
    // Memory pool for real-time safe temporary allocations
    // Example: MemoryPool<std::vector<uint8_t>> tempBufferPool_;
    // These can be initialized in derived classes as needed
    // Usage hooks:
    // - Use executionTimer_ to time processSync/processAsync
    // - Use ThreadSafeQueue for port or internal buffering
    // - Use MemoryPool for temporary object/buffer allocation
    // Helper methods for derived classes
    void setState(ExecutionState state) { state_ = state; }
    void setError(const std::string& error);
    void updatePerformanceMetrics(std::chrono::microseconds executionTime);
    
    // System integration
    TimingController* getTimingController() const { return timingController_; }
    PerformanceMonitor* getPerformanceMonitor() const { return performanceMonitor_; }
    ConfigurationManager* getConfigurationManager() const { return configManager_; }
    
    // Allow system to set these
    friend class AxonVexSystem;
    void setTimingController(TimingController* controller) { timingController_ = controller; }
    void setPerformanceMonitor(PerformanceMonitor* monitor) { performanceMonitor_ = monitor; }
    void setConfigurationManager(ConfigurationManager* manager) { configManager_ = manager; }
    
private:
    std::string name_;
    std::atomic<ExecutionState> state_{ExecutionState::UNINITIALIZED};
    ProcessPriority priority_{ProcessPriority::NORMAL};
    std::chrono::microseconds executionPeriod_{std::chrono::milliseconds{10}}; // Default 10ms
    
    // Ports
    mutable std::mutex portsMutex_;
    std::unordered_map<int, std::unique_ptr<BasePort>> portsById_;
    std::unordered_map<std::string, BasePort*> portsByName_;
    
    // Performance metrics
    mutable std::mutex metricsMutex_;
    PerformanceMetrics metrics_;
    
    // Error handling
    std::string lastError_;
    bool debugMode_{false};
    
    // System integration
    TimingController* timingController_{nullptr};
    PerformanceMonitor* performanceMonitor_{nullptr};
    ConfigurationManager* configManager_{nullptr};
};

// Template implementations

template<typename T>
InputPort<T>::InputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, owner) {
}

template<typename T>
T InputPort<T>::read() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return data_;
}

template<typename T>
bool InputPort<T>::hasNewData() const {
    return hasNewData_.load();
}

template<typename T>
void InputPort<T>::clearNewDataFlag() {
    hasNewData_.store(false);
}

template<typename T>
void InputPort<T>::setValidationCallback(ValidationCallback callback) {
    validationCallback_ = std::move(callback);
}

template<typename T>
void InputPort<T>::setDataCallback(DataCallback callback) {
    dataCallback_ = std::move(callback);
}

template<typename T>
void InputPort<T>::writeData(const T& data) {
    totalMessages_++;
    
    // Validate data if callback is set
    if (validationCallback_ && !validationCallback_(data)) {
        invalidMessages_++;
        return;
    }
    
    validMessages_++;
    
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = data;
    }
    
    hasNewData_.store(true);
    
    // Call data callback if set
    if (dataCallback_) {
        dataCallback_(data);
    }
}

template<typename T>
OutputPort<T>::OutputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, owner) {
}

template<typename T>
void OutputPort<T>::write(const T& data) {
    totalMessages_++;
    
    // Call output callback if set
    if (outputCallback_) {
        outputCallback_(data);
    }
    
    // Send data to all connected input ports
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (auto* inputPort : connectedPorts_) {
        if (inputPort) {
            inputPort->writeData(data);
        }
    }
}

template<typename T>
void OutputPort<T>::connect(InputPort<T>* inputPort) {
    if (!inputPort) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    auto it = std::find(connectedPorts_.begin(), connectedPorts_.end(), inputPort);
    if (it == connectedPorts_.end()) {
        connectedPorts_.push_back(inputPort);
    }
}

template<typename T>
void OutputPort<T>::disconnect(InputPort<T>* inputPort) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    auto it = std::find(connectedPorts_.begin(), connectedPorts_.end(), inputPort);
    if (it != connectedPorts_.end()) {
        connectedPorts_.erase(it);
    }
}

template<typename T>
void OutputPort<T>::disconnectAll() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedPorts_.clear();
}

template<typename T>
void OutputPort<T>::setOutputCallback(OutputCallback callback) {
    outputCallback_ = std::move(callback);
}

template<typename T>
InputPort<T>* ProcessingUnit::createInputPort(int id, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    // Check if port with this ID already exists
    if (portsById_.find(id) != portsById_.end()) {
        throw std::runtime_error("Port with ID " + std::to_string(id) + " already exists");
    }
    
    // Check if port with this name already exists
    if (portsByName_.find(name) != portsByName_.end()) {
        throw std::runtime_error("Port with name '" + name + "' already exists");
    }
    
    auto port = std::make_unique<InputPort<T>>(id, name, this);
    auto* portPtr = port.get();
    
    portsById_[id] = std::move(port);
    portsByName_[name] = portPtr;
    
    return portPtr;
}

template<typename T>
OutputPort<T>* ProcessingUnit::createOutputPort(int id, const std::string& name) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    
    // Check if port with this ID already exists
    if (portsById_.find(id) != portsById_.end()) {
        throw std::runtime_error("Port with ID " + std::to_string(id) + " already exists");
    }
    
    // Check if port with this name already exists
    if (portsByName_.find(name) != portsByName_.end()) {
        throw std::runtime_error("Port with name '" + name + "' already exists");
    }
    
    auto port = std::make_unique<OutputPort<T>>(id, name, this);
    auto* portPtr = port.get();
    
    portsById_[id] = std::move(port);
    portsByName_[name] = portPtr;
    
    return portPtr;
}

} // namespace axonvex::core 