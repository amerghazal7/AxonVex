/**
 * @file ports.hpp
 * @brief Advanced Port System for AxonVex Framework
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * Advanced port system integer indexing,
 * async ports, and advanced connection patterns while maintaining
 * AxonVex's modern C++ design principles.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include <typeinfo>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <axonvex/core/memoryPool.hpp>

namespace axonvex::core {

// Forward declarations
class ProcessingUnit;

/**
 * @brief Port types for port system
 */
enum class PortType {
    SYNC_INPUT,
    SYNC_OUTPUT,
    ASYNC_INPUT,
    ASYNC_OUTPUT
};

/**
 * @brief Base port class for the port system
 */
class BasePort {
public:
    explicit BasePort(int id, const std::string& name, PortType type, ProcessingUnit* owner);
    virtual ~BasePort() = default;
    
    // Port identification
    int getId() const noexcept { return id_; }
    const std::string& getName() const noexcept { return name_; }
    PortType getType() const noexcept { return type_; }
    ProcessingUnit* getOwner() const noexcept { return owner_; }
    
    // Port UID generation (block_uid*256 + port_idx)
    uint32_t getPortUID() const noexcept;
    void setPortUID(uint32_t uid) noexcept { portUID_ = uid; }
    
    // Type information
    virtual std::string getDataTypeName() const = 0;
    virtual size_t getDataTypeSize() const = 0;
    
    // Statistics
    uint64_t getTotalMessages() const noexcept { return totalMessages_.load(); }
    uint64_t getValidMessages() const noexcept { return validMessages_.load(); }
    uint64_t getInvalidMessages() const noexcept { return invalidMessages_.load(); }
    
    // Debug and validation
    void setDescription(const std::string& desc) { description_ = desc; }
    const std::string& getDescription() const noexcept { return description_; }
    
    // Thread safety control
    virtual void setThreadSafe(bool threadSafe) = 0;
    virtual bool isThreadSafe() const noexcept = 0;
    
    // MemoryPool support for thread-safe data storage
    virtual void setMemoryPoolSize(size_t poolSize) = 0;
    virtual size_t getMemoryPoolSize() const noexcept = 0;
    
    // Reset functionality
    virtual void reset() = 0;

protected:
    int id_;
    std::string name_;
    std::string description_;
    PortType type_;
    ProcessingUnit* owner_;
    uint32_t portUID_{0};
    
    // Statistics
    std::atomic<uint64_t> totalMessages_{0};
    std::atomic<uint64_t> validMessages_{0};
    std::atomic<uint64_t> invalidMessages_{0};
    
    // Validation helpers
    template<typename T>
    bool validateData(const T& data);
    
    void incrementTotalMessages() { totalMessages_++; }
    void incrementValidMessages() { validMessages_++; }
    void incrementInvalidMessages() { invalidMessages_++; }
};

/**
 * @brief Synchronous Input Port
 */
template<typename T>
class InputPort : public BasePort {
public:
    using ValidationCallback = std::function<bool(const T&)>;
    using DataCallback = std::function<void(const T&)>;
    
    explicit InputPort(int id, const std::string& name, ProcessingUnit* owner);
    ~InputPort() override = default;
    
    // Data access
    T read() const;
    bool hasNewData() const noexcept;
    void clearNewDataFlag() noexcept;
    
    // Connection management (called by OutputPort)
    void writeData(const T& data);
    
    // Callbacks and validation
    void setValidationCallback(ValidationCallback callback);
    void setDataCallback(DataCallback callback);
    
    // Bridge ports for complex routing
    void addBridgedPort(InputPort<T>* bridgedPort);
    void removeBridgedPort(InputPort<T>* bridgedPort);
    bool isBridgePort() const noexcept { return isBridgePort_; }
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    size_t getDataTypeSize() const override { return sizeof(T); }
    
    // Thread safety
    void setThreadSafe(bool threadSafe) override;
    bool isThreadSafe() const noexcept override { return isThreadSafe_; }
    
    // MemoryPool support
    void setMemoryPoolSize(size_t poolSize) override;
    size_t getMemoryPoolSize() const noexcept override;
    
    // Reset
    void reset() override;
    
private:
    mutable std::mutex dataMutex_;
    T data_;
    std::atomic<bool> hasNewData_{false};
    std::atomic<bool> isThreadSafe_{false};
    
    // MemoryPool for thread-safe data storage
    std::unique_ptr<MemoryPool<T>> memoryPool_;
    std::atomic<T*> pooledData_{nullptr};
    size_t poolSize_{MemoryPool<T>::DEFAULT_POOL_SIZE};
    
    // Callbacks
    ValidationCallback validationCallback_;
    DataCallback dataCallback_;
    
    // Bridge port management
    bool isBridgePort_{false};
    std::vector<InputPort<T>*> bridgedPorts_;
    mutable std::mutex bridgeMutex_;
    
    // NaN detection for floating point types
    bool hasNanWarned_{false};
};

/**
 * @brief Synchronous Output Port
 */
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
    bool isConnected() const noexcept;
    size_t getConnectionCount() const noexcept;
    
    // Get connected ports
    std::vector<InputPort<T>*> getConnectedPorts() const;
    
    // Callbacks
    void setOutputCallback(OutputCallback callback);
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    size_t getDataTypeSize() const override { return sizeof(T); }
    
    // Thread safety
    void setThreadSafe(bool threadSafe) override;
    bool isThreadSafe() const noexcept override { return isThreadSafe_; }
    
    // MemoryPool support
    void setMemoryPoolSize(size_t poolSize) override;
    size_t getMemoryPoolSize() const noexcept override;
    
    // Reset
    void reset() override;
    
private:
    mutable std::mutex connectionMutex_;
    std::vector<InputPort<T>*> connectedPorts_;
    std::atomic<bool> isThreadSafe_{false};
    
    // Current data for thread-safe access
    mutable std::mutex dataMutex_;
    T currentData_;
    
    // MemoryPool for thread-safe data storage
    std::unique_ptr<MemoryPool<T>> memoryPool_;
    std::atomic<T*> pooledData_{nullptr};
    size_t poolSize_{MemoryPool<T>::DEFAULT_POOL_SIZE};
    
    OutputCallback outputCallback_;
    bool hasNanWarned_{false};
};

/**
 * @brief Asynchronous Input Port
 */
template<typename T>
class AsyncInputPort : public BasePort {
public:
    using ValidationCallback = std::function<bool(const T&)>;
    using DataCallback = std::function<void(const T&)>;
    
    explicit AsyncInputPort(int id, const std::string& name, ProcessingUnit* owner);
    ~AsyncInputPort() override = default;
    
    // Async data operations
    void update(const T& data);
    bool wasUpdated() const noexcept;
    void read(T& data);
    T read();
    
    // Callbacks and validation
    void setValidationCallback(ValidationCallback callback);
    void setDataCallback(DataCallback callback);
    
    // Bridge ports for complex routing
    void addBridgedPort(AsyncInputPort<T>* bridgedPort);
    void removeBridgedPort(AsyncInputPort<T>* bridgedPort);
    bool isBridgePort() const noexcept { return isBridgePort_; }
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    size_t getDataTypeSize() const override { return sizeof(T); }
    
    // Thread safety
    void setThreadSafe(bool threadSafe) override;
    bool isThreadSafe() const noexcept override { return isThreadSafe_; }
    
    // MemoryPool support
    void setMemoryPoolSize(size_t poolSize) override;
    size_t getMemoryPoolSize() const noexcept override;
    
    // Reset
    void reset() override;
    
private:
    mutable std::mutex dataMutex_;
    T data_;
    std::atomic<bool> wasUpdated_{false};
    std::atomic<bool> isThreadSafe_{false};
    
    // MemoryPool for thread-safe data storage
    std::unique_ptr<MemoryPool<T>> memoryPool_;
    std::atomic<T*> pooledData_{nullptr};
    size_t poolSize_{MemoryPool<T>::DEFAULT_POOL_SIZE};
    
    // Callbacks
    ValidationCallback validationCallback_;
    DataCallback dataCallback_;
    
    // Bridge port management
    bool isBridgePort_{false};
    std::vector<AsyncInputPort<T>*> bridgedPorts_;
    mutable std::mutex bridgeMutex_;
};

/**
 * @brief Asynchronous Output Port
 */
template<typename T>
class AsyncOutputPort : public BasePort {
public:
    using OutputCallback = std::function<void(const T&)>;
    
    explicit AsyncOutputPort(int id, const std::string& name, ProcessingUnit* owner);
    ~AsyncOutputPort() override = default;
    
    // Async data operations
    void write(const T& data);
    
    // Connection management
    void connect(AsyncInputPort<T>* inputPort);
    void disconnect(AsyncInputPort<T>* inputPort);
    void disconnectAll();
    bool isConnected() const noexcept;
    size_t getConnectionCount() const noexcept;
    
    // Get connected ports
    std::vector<AsyncInputPort<T>*> getConnectedPorts() const;
    
    // Callbacks
    void setOutputCallback(OutputCallback callback);
    
    // Port information
    std::string getDataTypeName() const override { return typeid(T).name(); }
    size_t getDataTypeSize() const override { return sizeof(T); }
    
    // Thread safety
    void setThreadSafe(bool threadSafe) override;
    bool isThreadSafe() const noexcept override { return isThreadSafe_; }
    
    // MemoryPool support
    void setMemoryPoolSize(size_t poolSize) override;
    size_t getMemoryPoolSize() const noexcept override;
    
    // Reset
    void reset() override;
    
    // Control async write logging
    void setLogAsyncWriteEvent(bool log) noexcept { logAsyncWriteEvent_ = log; }
    bool getLogAsyncWriteEvent() const noexcept { return logAsyncWriteEvent_; }
    
private:
    mutable std::mutex connectionMutex_;
    std::vector<AsyncInputPort<T>*> connectedPorts_;
    std::atomic<bool> isThreadSafe_{false};
    std::atomic<bool> logAsyncWriteEvent_{false};
    
    // MemoryPool for consistency (AsyncOutput doesn't store data but maintains interface)
    size_t poolSize_{MemoryPool<T>::DEFAULT_POOL_SIZE};
    
    OutputCallback outputCallback_;
};

// Template implementations

template<typename T>
InputPort<T>::InputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, PortType::SYNC_INPUT, owner) {
    data_ = T{};
}

template<typename T>
T InputPort<T>::read() const {
    if (isThreadSafe_ && memoryPool_) {
        // Use MemoryPool for thread-safe access
        T* pooled = pooledData_.load(std::memory_order_acquire);
        if (pooled) {
            return *pooled;
        }
        return T{}; // Default value if no data in pool
    } else if (isThreadSafe_) {
        // Fallback to mutex-based thread safety
        std::lock_guard<std::mutex> lock(dataMutex_);
        return data_;
    }
    return data_;
}

template<typename T>
bool InputPort<T>::hasNewData() const noexcept {
    return hasNewData_.load();
}

template<typename T>
void InputPort<T>::clearNewDataFlag() noexcept {
    hasNewData_.store(false);
}

template<typename T>
void InputPort<T>::writeData(const T& data) {
    incrementTotalMessages();
    
    // Validate data if callback is set
    if (validationCallback_ && !validationCallback_(data)) {
        incrementInvalidMessages();
        return;
    }
    
    // Check for NaN in floating point data
    if (!validateData(data)) {
        incrementInvalidMessages();
        return;
    }
    
    incrementValidMessages();
    
    // Store data
    if (isThreadSafe_ && memoryPool_) {
        // Use MemoryPool for high-performance thread-safe storage
        T* pooled = memoryPool_->allocateObject(data);
        if (pooled) {
            // Atomically update the pointer and deallocate old data
            T* old = pooledData_.exchange(pooled, std::memory_order_acq_rel);
            if (old) {
                memoryPool_->deallocateObject(old);
            }
        } else {
            // Pool exhausted, fallback to mutex-based storage
            std::lock_guard<std::mutex> lock(dataMutex_);
            data_ = data;
        }
    } else if (isThreadSafe_) {
        // Fallback to mutex-based thread safety
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = data;
    } else {
        data_ = data;
    }
    
    hasNewData_.store(true);
    
    // Call data callback if set
    if (dataCallback_) {
        dataCallback_(data);
    }
    
    // Propagate to bridged ports
    if (isBridgePort_) {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        for (auto* bridgedPort : bridgedPorts_) {
            if (bridgedPort) {
                bridgedPort->writeData(data);
            }
        }
    }
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
void InputPort<T>::addBridgedPort(InputPort<T>* bridgedPort) {
    if (!bridgedPort) return;
    
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    auto it = std::find(bridgedPorts_.begin(), bridgedPorts_.end(), bridgedPort);
    if (it == bridgedPorts_.end()) {
        bridgedPorts_.push_back(bridgedPort);
        isBridgePort_ = true;
    }
}

template<typename T>
void InputPort<T>::removeBridgedPort(InputPort<T>* bridgedPort) {
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    auto it = std::find(bridgedPorts_.begin(), bridgedPorts_.end(), bridgedPort);
    if (it != bridgedPorts_.end()) {
        bridgedPorts_.erase(it);
        isBridgePort_ = !bridgedPorts_.empty();
    }
}

template<typename T>
void InputPort<T>::setThreadSafe(bool threadSafe) {
    if (threadSafe && !isThreadSafe_) {
        // Initialize MemoryPool when enabling thread safety
        if (!memoryPool_) {
            memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
        }
    } else if (!threadSafe && isThreadSafe_) {
        // Clean up when disabling thread safety
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old && memoryPool_) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_.reset();
    }
    isThreadSafe_ = threadSafe;
}

template<typename T>
void InputPort<T>::setMemoryPoolSize(size_t poolSize) {
    poolSize_ = std::max(static_cast<size_t>(16), poolSize);
    if (isThreadSafe_ && memoryPool_) {
        // Recreate memory pool with new size
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
    }
}

template<typename T>
size_t InputPort<T>::getMemoryPoolSize() const noexcept {
    return poolSize_;
}

template<typename T>
void InputPort<T>::reset() {
    if (isThreadSafe_ && memoryPool_) {
        // Reset MemoryPool data
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
    } else if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = T{};
    } else {
        data_ = T{};
    }
    hasNewData_.store(false);
    hasNanWarned_ = false;
}

// Helper function for NaN validation
template<typename T>
bool BasePort::validateData(const T& data) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(data)) {
            // Log warning only once per port
            return false;
        }
    }
    return true;
}

// OutputPort template implementations
template<typename T>
OutputPort<T>::OutputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, PortType::SYNC_OUTPUT, owner) {
    currentData_ = T{};
}

template<typename T>
void OutputPort<T>::write(const T& data) {
    incrementTotalMessages();
    
    // Check for NaN in floating point data
    if (!validateData(data)) {
        if (!hasNanWarned_) {
            hasNanWarned_ = true;
            // Log NaN warning - in full implementation would use logger
        }
        return;
    }
    
    // Store current data for thread-safe access
    if (isThreadSafe_ && memoryPool_) {
        // Use MemoryPool for high-performance thread-safe storage
        T* pooled = memoryPool_->allocateObject(data);
        if (pooled) {
            // Atomically update the pointer and deallocate old data
            T* old = pooledData_.exchange(pooled, std::memory_order_acq_rel);
            if (old) {
                memoryPool_->deallocateObject(old);
            }
        } else {
            // Pool exhausted, fallback to mutex-based storage
            std::lock_guard<std::mutex> lock(dataMutex_);
            currentData_ = data;
        }
    } else if (isThreadSafe_) {
        // Fallback to mutex-based thread safety
        std::lock_guard<std::mutex> lock(dataMutex_);
        currentData_ = data;
    } else {
        currentData_ = data;
    }
    
    // Call output callback if set
    if (outputCallback_) {
        outputCallback_(data);
    }
    
    // Send to all connected input ports
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
bool OutputPort<T>::isConnected() const noexcept {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return !connectedPorts_.empty();
}

template<typename T>
size_t OutputPort<T>::getConnectionCount() const noexcept {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return connectedPorts_.size();
}

template<typename T>
std::vector<InputPort<T>*> OutputPort<T>::getConnectedPorts() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return connectedPorts_;
}

template<typename T>
void OutputPort<T>::setOutputCallback(OutputCallback callback) {
    outputCallback_ = std::move(callback);
}

template<typename T>
void OutputPort<T>::setThreadSafe(bool threadSafe) {
    if (threadSafe && !isThreadSafe_) {
        // Initialize MemoryPool when enabling thread safety
        if (!memoryPool_) {
            memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
        }
    } else if (!threadSafe && isThreadSafe_) {
        // Clean up when disabling thread safety
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old && memoryPool_) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_.reset();
    }
    isThreadSafe_ = threadSafe;
}

template<typename T>
void OutputPort<T>::setMemoryPoolSize(size_t poolSize) {
    poolSize_ = std::max(static_cast<size_t>(16), poolSize);
    if (isThreadSafe_ && memoryPool_) {
        // Recreate memory pool with new size
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
    }
}

template<typename T>
size_t OutputPort<T>::getMemoryPoolSize() const noexcept {
    return poolSize_;
}

template<typename T>
void OutputPort<T>::reset() {
    if (isThreadSafe_ && memoryPool_) {
        // Reset MemoryPool data
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
    } else if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        currentData_ = T{};
    } else {
        currentData_ = T{};
    }
    hasNanWarned_ = false;
}

// AsyncInputPort template implementations
template<typename T>
AsyncInputPort<T>::AsyncInputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, PortType::ASYNC_INPUT, owner) {
    data_ = T{};
}

template<typename T>
void AsyncInputPort<T>::update(const T& data) {
    incrementTotalMessages();
    
    // Validate data if callback is set
    if (validationCallback_ && !validationCallback_(data)) {
        incrementInvalidMessages();
        return;
    }
    
    // Check for NaN in floating point data
    if (!validateData(data)) {
        incrementInvalidMessages();
        return;
    }
    
    incrementValidMessages();
    
    // Store data
    if (isThreadSafe_ && memoryPool_) {
        // Use MemoryPool for high-performance thread-safe storage
        T* pooled = memoryPool_->allocateObject(data);
        if (pooled) {
            // Atomically update the pointer and deallocate old data
            T* old = pooledData_.exchange(pooled, std::memory_order_acq_rel);
            if (old) {
                memoryPool_->deallocateObject(old);
            }
        } else {
            // Pool exhausted, fallback to mutex-based storage
            std::lock_guard<std::mutex> lock(dataMutex_);
            data_ = data;
        }
    } else if (isThreadSafe_) {
        // Fallback to mutex-based thread safety
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = data;
    } else {
        data_ = data;
    }
    
    wasUpdated_.store(true);
    
    // Call data callback if set
    if (dataCallback_) {
        dataCallback_(data);
    }
    
    // Propagate to bridged ports
    if (isBridgePort_) {
        std::lock_guard<std::mutex> lock(bridgeMutex_);
        for (auto* bridgedPort : bridgedPorts_) {
            if (bridgedPort) {
                bridgedPort->update(data);
            }
        }
    }
}

template<typename T>
bool AsyncInputPort<T>::wasUpdated() const noexcept {
    return wasUpdated_.load();
}

template<typename T>
void AsyncInputPort<T>::read(T& data) {
    if (!wasUpdated_.load()) {
        std::stringstream errorMessage;
        errorMessage << "Error: Async Port " << getId() << " was read without being updated";
        throw std::runtime_error(errorMessage.str());
    }
    
    wasUpdated_.store(false);
    
    if (isThreadSafe_ && memoryPool_) {
        // Use MemoryPool for thread-safe access
        T* pooled = pooledData_.load(std::memory_order_acquire);
        if (pooled) {
            data = *pooled;
        } else {
            data = T{}; // Default value if no data in pool
        }
    } else if (isThreadSafe_) {
        // Fallback to mutex-based thread safety
        std::lock_guard<std::mutex> lock(dataMutex_);
        data = data_;
    } else {
        data = data_;
    }
}

template<typename T>
T AsyncInputPort<T>::read() {
    T data;
    read(data);
    return data;
}

template<typename T>
void AsyncInputPort<T>::setValidationCallback(ValidationCallback callback) {
    validationCallback_ = std::move(callback);
}

template<typename T>
void AsyncInputPort<T>::setDataCallback(DataCallback callback) {
    dataCallback_ = std::move(callback);
}

template<typename T>
void AsyncInputPort<T>::addBridgedPort(AsyncInputPort<T>* bridgedPort) {
    if (!bridgedPort) return;
    
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    auto it = std::find(bridgedPorts_.begin(), bridgedPorts_.end(), bridgedPort);
    if (it == bridgedPorts_.end()) {
        bridgedPorts_.push_back(bridgedPort);
        isBridgePort_ = true;
    }
}

template<typename T>
void AsyncInputPort<T>::removeBridgedPort(AsyncInputPort<T>* bridgedPort) {
    std::lock_guard<std::mutex> lock(bridgeMutex_);
    auto it = std::find(bridgedPorts_.begin(), bridgedPorts_.end(), bridgedPort);
    if (it != bridgedPorts_.end()) {
        bridgedPorts_.erase(it);
        isBridgePort_ = !bridgedPorts_.empty();
    }
}

template<typename T>
void AsyncInputPort<T>::setThreadSafe(bool threadSafe) {
    if (threadSafe && !isThreadSafe_) {
        // Initialize MemoryPool when enabling thread safety
        if (!memoryPool_) {
            memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
        }
    } else if (!threadSafe && isThreadSafe_) {
        // Clean up when disabling thread safety
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old && memoryPool_) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_.reset();
    }
    isThreadSafe_ = threadSafe;
}

template<typename T>
void AsyncInputPort<T>::setMemoryPoolSize(size_t poolSize) {
    poolSize_ = std::max(static_cast<size_t>(16), poolSize);
    if (isThreadSafe_ && memoryPool_) {
        // Recreate memory pool with new size
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
        memoryPool_ = std::make_unique<MemoryPool<T>>(poolSize_);
    }
}

template<typename T>
size_t AsyncInputPort<T>::getMemoryPoolSize() const noexcept {
    return poolSize_;
}

template<typename T>
void AsyncInputPort<T>::reset() {
    if (isThreadSafe_ && memoryPool_) {
        // Reset MemoryPool data
        T* old = pooledData_.exchange(nullptr, std::memory_order_acq_rel);
        if (old) {
            memoryPool_->deallocateObject(old);
        }
    } else if (isThreadSafe_) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        data_ = T{};
    } else {
        data_ = T{};
    }
    wasUpdated_.store(false);
}

// AsyncOutputPort template implementations
template<typename T>
AsyncOutputPort<T>::AsyncOutputPort(int id, const std::string& name, ProcessingUnit* owner)
    : BasePort(id, name, PortType::ASYNC_OUTPUT, owner) {
}

template<typename T>
void AsyncOutputPort<T>::write(const T& data) {
    incrementTotalMessages();
    
    if (connectedPorts_.empty()) {
        // Log warning about unconnected port
        return;
    }
    
    // Call output callback if set
    if (outputCallback_) {
        outputCallback_(data);
    }
    
    // Send to all connected async input ports
    std::lock_guard<std::mutex> lock(connectionMutex_);
    for (auto* inputPort : connectedPorts_) {
        if (inputPort) {
            inputPort->update(data);
        }
    }
}

template<typename T>
void AsyncOutputPort<T>::connect(AsyncInputPort<T>* inputPort) {
    if (!inputPort) return;
    
    std::lock_guard<std::mutex> lock(connectionMutex_);
    auto it = std::find(connectedPorts_.begin(), connectedPorts_.end(), inputPort);
    if (it == connectedPorts_.end()) {
        connectedPorts_.push_back(inputPort);
    }
}

template<typename T>
void AsyncOutputPort<T>::disconnect(AsyncInputPort<T>* inputPort) {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    auto it = std::find(connectedPorts_.begin(), connectedPorts_.end(), inputPort);
    if (it != connectedPorts_.end()) {
        connectedPorts_.erase(it);
    }
}

template<typename T>
void AsyncOutputPort<T>::disconnectAll() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    connectedPorts_.clear();
}

template<typename T>
bool AsyncOutputPort<T>::isConnected() const noexcept {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return !connectedPorts_.empty();
}

template<typename T>
size_t AsyncOutputPort<T>::getConnectionCount() const noexcept {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return connectedPorts_.size();
}

template<typename T>
std::vector<AsyncInputPort<T>*> AsyncOutputPort<T>::getConnectedPorts() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    return connectedPorts_;
}

template<typename T>
void AsyncOutputPort<T>::setOutputCallback(OutputCallback callback) {
    outputCallback_ = std::move(callback);
}

template<typename T>
void AsyncOutputPort<T>::setThreadSafe(bool threadSafe) {
    isThreadSafe_ = threadSafe;
    // AsyncOutputPort doesn't store data, so no MemoryPool needed
}

template<typename T>
void AsyncOutputPort<T>::setMemoryPoolSize(size_t poolSize) {
    poolSize_ = std::max(static_cast<size_t>(16), poolSize);
    // AsyncOutputPort doesn't store data, just maintain size for interface consistency
}

template<typename T>
size_t AsyncOutputPort<T>::getMemoryPoolSize() const noexcept {
    return poolSize_;
}

template<typename T>
void AsyncOutputPort<T>::reset() {
    // Async output ports don't maintain state
}

} // namespace axonvex::core 