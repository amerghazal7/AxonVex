/**
 * @file performanceProfiler.cpp
 * @brief Performance profiler implementation
 */

#include <axonvex/utils/profiling/performanceProfiler.hpp>
#include <iostream>
#include <iomanip>

namespace axonvex::utils::profiling {

std::unordered_map<std::string, PerformanceProfiler::TimePoint> PerformanceProfiler::start_times_;
std::unordered_map<std::string, PerformanceProfiler::Duration> PerformanceProfiler::durations_;

void PerformanceProfiler::startTimer(const std::string& name) {
    start_times_[name] = std::chrono::high_resolution_clock::now();
}

PerformanceProfiler::Duration PerformanceProfiler::stopTimer(const std::string& name) {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto it = start_times_.find(name);
    if (it != start_times_.end()) {
        auto duration = std::chrono::duration_cast<Duration>(end_time - it->second);
        durations_[name] = duration;
        start_times_.erase(it);
        return duration;
    }
    return Duration::zero();
}

PerformanceProfiler::Duration PerformanceProfiler::getLastDuration(const std::string& name) {
    auto it = durations_.find(name);
    return (it != durations_.end()) ? it->second : Duration::zero();
}

void PerformanceProfiler::reset() {
    start_times_.clear();
    durations_.clear();
}

void PerformanceProfiler::printResults() {
    std::cout << "Performance Profile Results:\n";
    std::cout << std::setw(20) << "Timer Name" << std::setw(15) << "Duration (ns)" << std::setw(15) << "Duration (ms)" << "\n";
    std::cout << std::string(50, '-') << "\n";
    
    for (const auto& pair : durations_) {
        auto ns = pair.second.count();
        auto ms = ns / 1000000.0;
        std::cout << std::setw(20) << pair.first 
                  << std::setw(15) << ns 
                  << std::setw(15) << std::fixed << std::setprecision(3) << ms << "\n";
    }
}

} // namespace axonvex::utils::profiling