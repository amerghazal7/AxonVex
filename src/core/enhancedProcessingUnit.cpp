/**
 * @file enhancedProcessingUnit.cpp
 * @brief Enhanced Processing Unit Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include "axonvex/core/enhancedProcessingUnit.hpp"
#include <iostream>
#include <algorithm>

namespace axonvex::core {

EnhancedProcessingUnit::EnhancedProcessingUnit(const std::string& name)
    : name_(name), instanceDescription_(name) {
    // Create built-in control ports
    resetPort_ = createAsyncInputPort<int>(ControlPorts::RESET, "Reset");
    disablePort_ = createAsyncInputPort<int>(ControlPorts::DISABLE, "Disable");
}

EnhancedProcessingUnit::~EnhancedProcessingUnit() {
    // Cleanup is handled by unique_ptr in ownedPorts_
}

void EnhancedProcessingUnit::processSyncBase() {
    intraSampleCounter_++;
    if (intraSampleCounter_ >= downSamplingFactor_) {
        if (!isDisabled_.load()) {
            executionTimer_.start();
            processSyncDerived();
            executionTimer_.stop();
            auto duration = executionTimer_.getElapsedNanoseconds();
            updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(duration));
        }
        intraSampleCounter_ = 0;
    }
}

void EnhancedProcessingUnit::processAsyncBase() {
    // Handle built-in control ports
    if (resetPort_->wasUpdated_AsyncIP()) {
        int msg = resetPort_->read_AsyncIP();
        if (msg != 0) {
            resetBlock();
        }
    }
    
    if (disablePort_->wasUpdated_AsyncIP()) {
        int msg = disablePort_->read_AsyncIP();
        setDisabled(msg != 0);
    }
    
    if (!isDisabled_.load()) {
        executionTimer_.start();
        processAsyncDerived();
        executionTimer_.stop();
        auto duration = executionTimer_.getElapsedNanoseconds();
        updateAsyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(duration));
    }
}

void EnhancedProcessingUnit::setDownSamplingFactor(int factor) {
    if (factor < 1) {
        throw std::invalid_argument("Down-sampling factor must be >= 1");
    }
    downSamplingFactor_ = factor;
    intraSampleCounter_ = 0; // Reset counter
}

void EnhancedProcessingUnit::inheritDownSamplingFactor(const EnhancedProcessingUnit* source) {
    if (source) {
        setDownSamplingFactor(source->getDownSamplingFactor());
    }
}

void EnhancedProcessingUnit::setBlockSamplingPeriod(std::chrono::microseconds period) {
    samplingPeriod_ = period;
}

void EnhancedProcessingUnit::setBlockUID(uint32_t uid) {
    blockUID_ = uid;
    hasBeenAddedToSystem_ = true;
    
    // Update all port UIDs
    std::lock_guard<std::mutex> lock(portsMutex_);
    for (auto& [idx, port] : inputPorts_) {
        port->setPortUID(uid * 256 + idx);
    }
    for (auto& [idx, port] : outputPorts_) {
        port->setPortUID(uid * 256 + idx);
    }
    for (auto& [idx, port] : asyncInputPorts_) {
        port->setPortUID(uid * 256 + idx);
    }
    for (auto& [idx, port] : asyncOutputPorts_) {
        port->setPortUID(uid * 256 + idx);
    }
}

void EnhancedProcessingUnit::setURL(const std::string& url) {
    relativeURL_ = url;
    // For now, absolute URL is the same as relative
    // In a full system, this would be computed from parent hierarchy
    absoluteURL_ = relativeURL_;
    hasURLBeenSet_ = true;
}

void EnhancedProcessingUnit::setDisabled(bool disabled) {
    isDisabled_.store(disabled);
    if (disabled) {
        setState(EnhancedExecutionState::DISABLED);
    } else {
        setState(EnhancedExecutionState::RUNNING);
    }
}

void EnhancedProcessingUnit::resetBlock() {
    reset(); // Call derived class reset
    resetPorts();
    intraSampleCounter_ = 0;
    setState(EnhancedExecutionState::INITIALIZED);
}

void EnhancedProcessingUnit::resetPorts() {
    std::lock_guard<std::mutex> lock(portsMutex_);
    for (auto& [idx, port] : inputPorts_) {
        port->reset();
    }
    for (auto& [idx, port] : outputPorts_) {
        port->reset();
    }
    for (auto& [idx, port] : asyncInputPorts_) {
        port->reset();
    }
    for (auto& [idx, port] : asyncOutputPorts_) {
        port->reset();
    }
}

void EnhancedProcessingUnit::setPortsThreadSafe(bool threadSafe) {
    std::lock_guard<std::mutex> lock(portsMutex_);
    for (auto& [idx, port] : inputPorts_) {
        port->setThreadSafe(threadSafe);
    }
    for (auto& [idx, port] : outputPorts_) {
        port->setThreadSafe(threadSafe);
    }
    for (auto& [idx, port] : asyncInputPorts_) {
        port->setThreadSafe(threadSafe);
    }
    for (auto& [idx, port] : asyncOutputPorts_) {
        port->setThreadSafe(threadSafe);
    }
}

std::string EnhancedProcessingUnit::getInputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = inputPortNames_.find(idx);
    return it != inputPortNames_.end() ? it->second : "";
}

std::string EnhancedProcessingUnit::getOutputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = outputPortNames_.find(idx);
    return it != outputPortNames_.end() ? it->second : "";
}

std::string EnhancedProcessingUnit::getAsyncInputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncInputPortNames_.find(idx);
    return it != asyncInputPortNames_.end() ? it->second : "";
}

std::string EnhancedProcessingUnit::getAsyncOutputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncOutputPortNames_.find(idx);
    return it != asyncOutputPortNames_.end() ? it->second : "";
}

void EnhancedProcessingUnit::updateInstanceDescription(const std::string& description) {
    instanceDescription_ = description;
}

EnhancedProcessingUnit::ExecutionStats EnhancedProcessingUnit::getExecutionStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void EnhancedProcessingUnit::resetExecutionStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = ExecutionStats{};
}

void EnhancedProcessingUnit::updateSyncExecutionStats(std::chrono::microseconds executionTime) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.syncExecutionCount++;
    stats_.totalSyncTime += executionTime;
    stats_.avgSyncTime = stats_.totalSyncTime / stats_.syncExecutionCount;
    if (executionTime > stats_.maxSyncTime) {
        stats_.maxSyncTime = executionTime;
    }
}

void EnhancedProcessingUnit::updateAsyncExecutionStats(std::chrono::microseconds executionTime) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.asyncExecutionCount++;
    stats_.totalAsyncTime += executionTime;
    stats_.avgAsyncTime = stats_.totalAsyncTime / stats_.asyncExecutionCount;
    if (executionTime > stats_.maxAsyncTime) {
        stats_.maxAsyncTime = executionTime;
    }
}

} // namespace axonvex::core 