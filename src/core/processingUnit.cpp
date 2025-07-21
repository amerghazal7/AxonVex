#include "axonvex/core/processingUnit.hpp"
#include <algorithm>
#include <stdexcept>
#include <mutex>
#include <vector>

namespace axonvex::core {

// BasePort implementation
BasePort::BasePort(int id, const std::string& name, ProcessingUnit* owner)
    : id_(id), name_(name), owner_(owner) {
    if (!owner) {
        throw std::invalid_argument("BasePort owner cannot be null");
    }
}

// ProcessingUnit implementation
ProcessingUnit::ProcessingUnit(const std::string& name)
    : name_(name) {
    if (name.empty()) {
        throw std::invalid_argument("ProcessingUnit name cannot be empty");
    }
}

ProcessingUnit::~ProcessingUnit() {
    // Clean up ports
    std::lock_guard<std::mutex> lock(portsMutex_);
    portsById_.clear();
    portsByName_.clear();
}

BasePort* ProcessingUnit::getPort(int id) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = portsById_.find(id);
    return (it != portsById_.end()) ? it->second.get() : nullptr;
}

BasePort* ProcessingUnit::getPort(const std::string& name) const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    auto it = portsByName_.find(name);
    return (it != portsByName_.end()) ? it->second : nullptr;
}

std::vector<BasePort*> ProcessingUnit::getAllPorts() const {
    std::lock_guard<std::mutex> lock(portsMutex_);
    std::vector<BasePort*> ports;
    ports.reserve(portsById_.size());
    
    for (const auto& pair : portsById_) {
        ports.push_back(pair.second.get());
    }
    
    return ports;
}

// Type-erased port access removed - use getAllPorts() and cast as needed

PerformanceMetrics ProcessingUnit::getPerformanceMetrics() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    return metrics_;
}

void ProcessingUnit::resetPerformanceMetrics() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    metrics_.reset();
}

void ProcessingUnit::setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    lastError_ = error;
    setState(ExecutionState::ERROR);
}

void ProcessingUnit::reset() {
    // Reset execution state
    setState(ExecutionState::STOPPED);
    
    // Reset performance metrics
    resetPerformanceMetrics();
    
    // Clear last error
    {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        lastError_.clear();
    }
}

void ProcessingUnit::updatePerformanceMetrics(std::chrono::microseconds executionTime) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    
    metrics_.executionCount++;
    metrics_.executionTime = executionTime;
    
    // Update min/max execution times
    if (executionTime < metrics_.minExecutionTime) {
        metrics_.minExecutionTime = executionTime;
    }
    if (executionTime > metrics_.maxExecutionTime) {
        metrics_.maxExecutionTime = executionTime;
    }
    
    // Update average execution time using running average
    if (metrics_.executionCount == 1) {
        metrics_.averageExecutionTime = executionTime;
    } else {
        // Running average: avg = (old_avg * (n-1) + new_value) / n
        auto oldAvg = metrics_.averageExecutionTime.count();
        auto newAvg = (oldAvg * (metrics_.executionCount - 1) + executionTime.count()) / metrics_.executionCount;
        metrics_.averageExecutionTime = std::chrono::microseconds(static_cast<long long>(newAvg));
    }
    
    // Calculate throughput (processing rate)
    if (executionTime.count() > 0) {
        metrics_.throughput = 1000000.0 / executionTime.count(); // items per second
    }
    
    // Check for deadline misses (if execution time exceeds period)
    if (executionTime > executionPeriod_) {
        metrics_.missedDeadlines++;
    }
}

} // namespace axonvex::core 