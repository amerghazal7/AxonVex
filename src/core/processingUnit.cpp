/**
 * @file processingUnit.cpp
 * @brief Advanced Processing Unit Implementation
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 */

#include "axonvex/core/processingUnit.hpp"
#include <iostream>
#include <algorithm>

namespace axonvex::core {

ProcessingUnit::ProcessingUnit(const std::string& name)
    : name_(name), instanceDescription_(name) {
    // Create built-in control ports
    resetPort_ = createAsyncInputPort<int>(ControlPorts::RESET, "Reset");
    disablePort_ = createAsyncInputPort<int>(ControlPorts::DISABLE, "Disable");
}

ProcessingUnit::~ProcessingUnit() {
    // Cleanup is handled by unique_ptr in ownedPorts_
}

void ProcessingUnit::processSyncBase() {
    intraSampleCounter_++;
    if (intraSampleCounter_ >= downSamplingFactor_) {
        if (!isDisabled_.load()) {
            executionTimer_.start();
            processSync();
            executionTimer_.stop();
            auto duration = executionTimer_.getElapsedNanoseconds();
            updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(duration));
        }
        intraSampleCounter_ = 0;
    }
}

void ProcessingUnit::processAsyncBase() {
    // Handle built-in control ports
            if (resetPort_->wasUpdated()) {
            int msg = resetPort_->read();
        if (msg != 0) {
            resetBlock();
        }
    }

            if (disablePort_->wasUpdated()) {
            int msg = disablePort_->read();
        setDisabled(msg != 0);
    }

    if (!isDisabled_.load()) {
        executionTimer_.start();
        processAsync();
        executionTimer_.stop();
        auto duration = executionTimer_.getElapsedNanoseconds();
        updateAsyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(duration));
    }
}

void ProcessingUnit::setDownSamplingFactor(int factor) {
    if (factor < 1) {
        throw std::invalid_argument("Down-sampling factor must be >= 1");
    }
    downSamplingFactor_ = factor;
    intraSampleCounter_ = 0; // Reset counter
}

void ProcessingUnit::inheritDownSamplingFactor(const ProcessingUnit* source) {
    if (source) {
        setDownSamplingFactor(source->getDownSamplingFactor());
    }
}

void ProcessingUnit::setBlockSamplingPeriod(std::chrono::microseconds period) {
    samplingPeriod_ = period;
}

void ProcessingUnit::setBlockUID(uint32_t uid) {
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

void ProcessingUnit::setURL(const std::string& url) {
    relativeURL_ = url;
    // For now, absolute URL is the same as relative
    // In a full system, this would be computed from parent hierarchy
    absoluteURL_ = relativeURL_;
    hasURLBeenSet_ = true;
}

void ProcessingUnit::setDisabled(bool disabled) {
    isDisabled_.store(disabled);
    if (disabled) {
        setState(ExecutionState::DISABLED);
    } else {
        setState(ExecutionState::RUNNING);
    }
}

void ProcessingUnit::resetBlock() {
    reset(); // Call derived class reset
    resetPorts();
    intraSampleCounter_ = 0;
    setState(ExecutionState::INITIALIZED);
}

void ProcessingUnit::resetPorts() {
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

void ProcessingUnit::setPortsThreadSafe(bool threadSafe) {
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

std::string ProcessingUnit::getInputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = inputPortNames_.find(idx);
    return it != inputPortNames_.end() ? it->second : "";
}

std::string ProcessingUnit::getOutputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = outputPortNames_.find(idx);
    return it != outputPortNames_.end() ? it->second : "";
}

std::string ProcessingUnit::getAsyncInputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncInputPortNames_.find(idx);
    return it != asyncInputPortNames_.end() ? it->second : "";
}

std::string ProcessingUnit::getAsyncOutputPortName(int idx) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = asyncOutputPortNames_.find(idx);
    return it != asyncOutputPortNames_.end() ? it->second : "";
}

void ProcessingUnit::updateInstanceDescription(const std::string& description) {
    instanceDescription_ = description;
}

ProcessingUnit::ExecutionStats ProcessingUnit::getExecutionStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void ProcessingUnit::resetExecutionStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = ExecutionStats{};
}

ProcessingUnit::PerformanceMetrics ProcessingUnit::getPerformanceMetrics() const {
    auto stats = getExecutionStats();
    PerformanceMetrics metrics;

    // Convert execution stats to legacy format
    metrics.executionCount = stats.syncExecutionCount + stats.asyncExecutionCount;
    if (metrics.executionCount > 0) {
        auto totalTime = stats.totalSyncTime + stats.totalAsyncTime;
        metrics.executionTime = totalTime;
        metrics.averageExecutionTime = totalTime / metrics.executionCount;
        metrics.maxExecutionTime = std::max(stats.maxSyncTime, stats.maxAsyncTime);
        metrics.minExecutionTime = std::min(
            stats.maxSyncTime > std::chrono::microseconds{0} ? stats.maxSyncTime : std::chrono::microseconds::max(),
            stats.maxAsyncTime > std::chrono::microseconds{0} ? stats.maxAsyncTime : std::chrono::microseconds::max()
        );
    }

    return metrics;
}

void ProcessingUnit::resetPerformanceMetrics() {
    resetExecutionStats();
}

void ProcessingUnit::setError(const std::string& error) {
    lastError_ = error;
    setState(ExecutionState::ERROR);
}

void ProcessingUnit::updateSyncExecutionStats(std::chrono::microseconds executionTime) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.syncExecutionCount++;
    stats_.totalSyncTime += executionTime;
    stats_.avgSyncTime = stats_.totalSyncTime / stats_.syncExecutionCount;
    if (executionTime > stats_.maxSyncTime) {
        stats_.maxSyncTime = executionTime;
    }
}

void ProcessingUnit::updateAsyncExecutionStats(std::chrono::microseconds executionTime) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.asyncExecutionCount++;
    stats_.totalAsyncTime += executionTime;
    stats_.avgAsyncTime = stats_.totalAsyncTime / stats_.asyncExecutionCount;
    if (executionTime > stats_.maxAsyncTime) {
        stats_.maxAsyncTime = executionTime;
    }
}

} // namespace axonvex::core
