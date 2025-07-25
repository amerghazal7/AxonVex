/**
 * @file performanceProfiler.hpp
 * @brief Performance profiling and measurement tools
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace axonvex::utils::profiling {

class PerformanceProfiler {
public:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Duration = std::chrono::nanoseconds;
    
    static void startTimer(const std::string& name);
    static Duration stopTimer(const std::string& name);
    static Duration getLastDuration(const std::string& name);
    static void reset();
    static void printResults();
    
private:
    static std::unordered_map<std::string, TimePoint> start_times_;
    static std::unordered_map<std::string, Duration> durations_;
};

} // namespace axonvex::utils::profiling