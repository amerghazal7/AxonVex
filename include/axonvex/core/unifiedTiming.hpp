#pragma once

#include <axonvex/core/precisionTimer.hpp>
#include <axonvex/core/unifiedStatistics.hpp>
#include <chrono>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <iostream>

namespace axonvex::core {

/**
 * @brief Unified timing system that consolidates all timing utilities
 * Replaces: PrecisionTimer, PerformanceProfiler, TimingUtils redundancies
 */
class UnifiedTimer {
public:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Duration = std::chrono::nanoseconds;

    /**
     * @brief Constructor with optional statistics tracking
     */
    explicit UnifiedTimer(const std::string& name = "Timer", bool enableStats = false, size_t maxSamples = 10000)
        : timer_(maxSamples), name_(name), stats_(std::make_unique<UnifiedStatistics>()) {
        timer_.enableStatistics(enableStats);
        if (enableStats) {
            stats_->setComponentName(name);
        }
    }

    // Core timing operations (delegates to PrecisionTimer)
    void start() noexcept { 
        start_time_ = now();
        timer_.start(); 
    }
    
    void stop() noexcept { 
        timer_.stop();
        if (timer_.isStatisticsEnabled()) {
            auto duration = timer_.getElapsedNanoseconds();
            stats_->recordSuccessfulOperation(duration);
        }
    }
    
    void reset() noexcept { 
        timer_.reset(); 
        stats_->reset();
    }
    
    Duration lap() noexcept { 
        auto result = timer_.lap();
        if (timer_.isStatisticsEnabled()) {
            stats_->recordSuccessfulOperation(result);
        }
        return result;
    }

    // Time queries
    Duration getElapsed() const { return timer_.getElapsedNanoseconds(); }
    double getElapsedSeconds() const { return timer_.getElapsedSeconds(); }
    double getElapsedMilliseconds() const { return timer_.getElapsedMilliseconds(); }
    double getElapsedMicroseconds() const { return timer_.getElapsedMicroseconds(); }

    // State queries
    bool isRunning() const noexcept { return timer_.isRunning(); }
    const std::string& getName() const noexcept { return name_; }

    // Statistics access
    const UnifiedStatistics& getStatistics() const { return *stats_; }
    std::string getReport() const { return stats_->getReport(); }

    // Legacy PrecisionTimer compatibility
    void enableStatistics(bool enable) noexcept { timer_.enableStatistics(enable); }
    bool isStatisticsEnabled() const noexcept { return timer_.isStatisticsEnabled(); }
    TimingStatistics getTimingStatistics() const { return timer_.getStatistics(); }
    double calculatePercentile(double percentile) const { return timer_.calculatePercentile(percentile); }

    // Static utilities (consolidated from PrecisionTimer and PerformanceProfiler)
    static Duration estimateOverhead(size_t iterations = 1000) {
        return PrecisionTimer::estimateOverhead(iterations);
    }
    
    static Duration getClockResolution() {
        return PrecisionTimer::getClockResolution();
    }
    
    static bool isClockSteady() {
        return PrecisionTimer::isClockSteady();
    }
    
    static TimePoint now() noexcept {
        return std::chrono::high_resolution_clock::now();
    }

private:
    PrecisionTimer timer_;
    std::string name_;
    std::unique_ptr<UnifiedStatistics> stats_;
    TimePoint start_time_;
};

/**
 * @brief Global timer registry (replaces PerformanceProfiler static functionality)
 */
class TimerRegistry {
public:
    static TimerRegistry& getInstance() {
        static TimerRegistry instance;
        return instance;
    }

    /**
     * @brief Start a named timer
     */
    void startTimer(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& timer = getOrCreateTimer(name);
        timer.start();
    }

    /**
     * @brief Stop a named timer and return duration
     */
    Duration stopTimer(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(name);
        if (it != timers_.end()) {
            it->second.stop();
            return it->second.getElapsed();
        }
        return Duration::zero();
    }

    /**
     * @brief Get last duration for a named timer
     */
    Duration getLastDuration(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(name);
        return (it != timers_.end()) ? it->second.getElapsed() : Duration::zero();
    }

    /**
     * @brief Get statistics for a named timer
     */
    std::string getTimerReport(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(name);
        return (it != timers_.end()) ? it->second.getReport() : "Timer not found";
    }

    /**
     * @brief Print all timer results
     */
    void printResults() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "=== Timer Registry Results ===\n";
        for (const auto& [name, timer] : timers_) {
            std::cout << timer.getReport() << "\n";
        }
    }

    /**
     * @brief Reset all timers
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, timer] : timers_) {
            timer.reset();
        }
    }

    /**
     * @brief Clear all timers
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        timers_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, UnifiedTimer> timers_;

    UnifiedTimer& getOrCreateTimer(const std::string& name) {
        auto it = timers_.find(name);
        if (it == timers_.end()) {
            it = timers_.emplace(name, UnifiedTimer(name, true)).first;
        }
        return it->second;
    }
};

/**
 * @brief RAII scoped timer (replaces TimingUtils::ScopedTimer)
 */
template<typename Callback>
class ScopedTimer {
public:
    explicit ScopedTimer(Callback&& callback, const std::string& name = "ScopedTimer")
        : callback_(std::forward<Callback>(callback)), timer_(name, true) {
        timer_.start();
    }

    ~ScopedTimer() {
        timer_.stop();
        callback_(timer_.getElapsed());
    }

private:
    Callback callback_;
    UnifiedTimer timer_;
};

/**
 * @brief Factory function for scoped timers
 */
template<typename Callback>
auto makeScopedTimer(Callback&& callback, const std::string& name = "ScopedTimer") {
    return ScopedTimer<Callback>(std::forward<Callback>(callback), name);
}

/**
 * @brief Convenience macros for timing
 */
#define AXONVEX_TIME_SCOPE(name) \
    auto AXONVEX_UNIQUE_NAME(timer) = axonvex::core::makeScopedTimer( \
        [](auto duration) { \
            std::cout << name << " took: " << duration.count() << " ns\n"; \
        }, name)

#define AXONVEX_TIME_FUNCTION() AXONVEX_TIME_SCOPE(__FUNCTION__)

#define AXONVEX_UNIQUE_NAME_II(name, line) name##line
#define AXONVEX_UNIQUE_NAME_I(name, line) AXONVEX_UNIQUE_NAME_II(name, line)
#define AXONVEX_UNIQUE_NAME(name) AXONVEX_UNIQUE_NAME_I(name, __LINE__)

/**
 * @brief Type aliases for backward compatibility
 */
using PerformanceProfiler = TimerRegistry;

/**
 * @brief Legacy TimingUtils namespace for backward compatibility
 */
namespace TimingUtils {
    using TimePoint = UnifiedTimer::TimePoint;
    using Duration = UnifiedTimer::Duration;
    
    inline TimePoint now() noexcept { return UnifiedTimer::now(); }
    inline Duration elapsed(TimePoint start, TimePoint end = now()) noexcept {
        return std::chrono::duration_cast<Duration>(end - start);
    }
    
    template<typename Callback>
    using ScopedTimer = axonvex::core::ScopedTimer<Callback>;
    
    template<typename Callback>
    auto makeScopedTimer(Callback&& callback) {
        return axonvex::core::makeScopedTimer(std::forward<Callback>(callback));
    }
}

} // namespace axonvex::core