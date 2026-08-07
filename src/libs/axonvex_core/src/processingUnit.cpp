/**
 * @file processingUnit.cpp
 * @brief Advanced Processing Unit Implementation
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 */

#include "axonvex_core/processingUnit.hpp"

#include <algorithm>
#include <iostream>

namespace axonvex::core {

ProcessingUnit::ProcessingUnit(const std::string& name) : name_(name), instanceDescription_(name) {
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
            updateSyncExecutionStats(
                std::chrono::duration_cast<std::chrono::microseconds>(duration));
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
    for (auto& kv : inputPorts_) {
        kv.second->setPortUID(uid * 256 + kv.first);
    }
    for (auto& kv : outputPorts_) {
        kv.second->setPortUID(uid * 256 + kv.first);
    }
    for (auto& kv : asyncInputPorts_) {
        kv.second->setPortUID(uid * 256 + kv.first);
    }
    for (auto& kv : asyncOutputPorts_) {
        kv.second->setPortUID(uid * 256 + kv.first);
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
    for (auto& kv : inputPorts_) {
        kv.second->reset();
    }
    for (auto& kv : outputPorts_) {
        kv.second->reset();
    }
    for (auto& kv : asyncInputPorts_) {
        kv.second->reset();
    }
    for (auto& kv : asyncOutputPorts_) {
        kv.second->reset();
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
    // V8: no lock — see the field comments in the header. Each load is relaxed;
    // this is a diagnostics snapshot, not a linearization point.
    ExecutionStats stats;
    stats.syncExecutionCount = syncExecutionCount_.load(std::memory_order_relaxed);
    stats.asyncExecutionCount = asyncExecutionCount_.load(std::memory_order_relaxed);
    stats.totalSyncTime =
        std::chrono::microseconds(totalSyncTimeUs_.load(std::memory_order_relaxed));
    stats.totalAsyncTime =
        std::chrono::microseconds(totalAsyncTimeUs_.load(std::memory_order_relaxed));
    stats.avgSyncTime = std::chrono::microseconds(avgSyncTimeUs_.load(std::memory_order_relaxed));
    stats.avgAsyncTime = std::chrono::microseconds(avgAsyncTimeUs_.load(std::memory_order_relaxed));
    stats.maxSyncTime = std::chrono::microseconds(maxSyncTimeUs_.load(std::memory_order_relaxed));
    stats.maxAsyncTime = std::chrono::microseconds(maxAsyncTimeUs_.load(std::memory_order_relaxed));
    return stats;
}

void ProcessingUnit::resetExecutionStats() {
    // V8: relaxed stores; may interleave with an in-flight update from the
    // scheduler thread (same tolerance as the loads above — diagnostics, not a
    // correctness-critical counter).
    syncExecutionCount_.store(0, std::memory_order_relaxed);
    asyncExecutionCount_.store(0, std::memory_order_relaxed);
    totalSyncTimeUs_.store(0, std::memory_order_relaxed);
    totalAsyncTimeUs_.store(0, std::memory_order_relaxed);
    avgSyncTimeUs_.store(0, std::memory_order_relaxed);
    avgAsyncTimeUs_.store(0, std::memory_order_relaxed);
    maxSyncTimeUs_.store(0, std::memory_order_relaxed);
    maxAsyncTimeUs_.store(0, std::memory_order_relaxed);
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
            stats.maxSyncTime > std::chrono::microseconds{0} ? stats.maxSyncTime
                                                             : std::chrono::microseconds::max(),
            stats.maxAsyncTime > std::chrono::microseconds{0} ? stats.maxAsyncTime
                                                              : std::chrono::microseconds::max());
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
    // V8: single-writer (scheduler thread only) — plain arithmetic, then relaxed
    // stores to publish. No RMW race is possible because nothing else ever
    // writes these fields.
    uint64_t count = syncExecutionCount_.load(std::memory_order_relaxed) + 1;
    auto total =
        std::chrono::microseconds(totalSyncTimeUs_.load(std::memory_order_relaxed)) + executionTime;
    int64_t maxSoFar = maxSyncTimeUs_.load(std::memory_order_relaxed);

    syncExecutionCount_.store(count, std::memory_order_relaxed);
    totalSyncTimeUs_.store(total.count(), std::memory_order_relaxed);
    avgSyncTimeUs_.store((total / count).count(), std::memory_order_relaxed);
    if (executionTime.count() > maxSoFar) {
        maxSyncTimeUs_.store(executionTime.count(), std::memory_order_relaxed);
    }
}

void ProcessingUnit::updateAsyncExecutionStats(std::chrono::microseconds executionTime) {
    // V8: same single-writer argument as updateSyncExecutionStats above.
    uint64_t count = asyncExecutionCount_.load(std::memory_order_relaxed) + 1;
    auto total = std::chrono::microseconds(totalAsyncTimeUs_.load(std::memory_order_relaxed)) +
                 executionTime;
    int64_t maxSoFar = maxAsyncTimeUs_.load(std::memory_order_relaxed);

    asyncExecutionCount_.store(count, std::memory_order_relaxed);
    totalAsyncTimeUs_.store(total.count(), std::memory_order_relaxed);
    avgAsyncTimeUs_.store((total / count).count(), std::memory_order_relaxed);
    if (executionTime.count() > maxSoFar) {
        maxAsyncTimeUs_.store(executionTime.count(), std::memory_order_relaxed);
    }
}

} // namespace axonvex::core
