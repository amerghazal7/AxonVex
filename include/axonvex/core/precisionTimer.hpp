/**
 * @file precision_timer.hpp
 * @brief High-Precision Timer for Real-Time Applications
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This file implements a high-precision timer optimized for real-time applications
 * with nanosecond accuracy and minimal overhead.
 */

#pragma once

#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>

namespace axonvex::core {

struct TimingStatistics {
    std::chrono::nanoseconds min{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max{std::chrono::nanoseconds::zero()};
    std::chrono::nanoseconds mean{std::chrono::nanoseconds::zero()};
    std::chrono::nanoseconds median{std::chrono::nanoseconds::zero()};

    uint64_t sample_count{0};
    uint64_t total_measurements{0};

    bool isValid() const {
        return sample_count > 0 && total_measurements > 0;
    }

    void reset() {
        min = std::chrono::nanoseconds::max();
        max = std::chrono::nanoseconds::zero();
        mean = std::chrono::nanoseconds::zero();
        median = std::chrono::nanoseconds::zero();
        sample_count = 0;
        total_measurements = 0;
    }
};

/**
 * @brief High-precision timer for performance measurements
 *
 * Provides nanosecond-precision timing with statistical analysis capabilities.
 * Thread-safe and suitable for real-time applications with minimal overhead.
 *
 * @example
 * PrecisionTimer timer;
 * timer.start();
 * // ... do work ...
 * auto elapsed = timer.getElapsedNanoseconds();
 */
class PrecisionTimer {
public:
    using ClockType = std::chrono::high_resolution_clock;
    using TimePointType = ClockType::time_point;
    using DurationType = std::chrono::nanoseconds;

    static constexpr size_t DEFAULT_MAX_SAMPLES = 10000;

    explicit PrecisionTimer(size_t max_samples = DEFAULT_MAX_SAMPLES);
    ~PrecisionTimer() = default;

    // Core functionality
    void start() noexcept;
    void stop() noexcept;
    void reset() noexcept;
    DurationType lap() noexcept;

    // Timing queries
    DurationType getElapsedNanoseconds() const;
    double getElapsedSeconds() const;
    double getElapsedMilliseconds() const;
    double getElapsedMicroseconds() const;

    // State queries
    bool isRunning() const noexcept;

    // Statistics
    void enableStatistics(bool enable) noexcept;
    bool isStatisticsEnabled() const noexcept;
    size_t getSampleCount() const noexcept;
    uint64_t getTotalMeasurements() const noexcept;
    void clearSamples() noexcept;
    TimingStatistics getStatistics() const;

    // Percentile calculations
    double calculatePercentile(double percentile) const;

    // Static utilities
    static DurationType estimateOverhead(size_t iterations = 1000);
    static DurationType getClockResolution();
    static bool isClockSteady();

private:
    std::atomic<TimePointType> start_time_;
    std::atomic<TimePointType> stop_time_;
    std::atomic<bool> running_;
    std::atomic<bool> statistics_enabled_;
    std::atomic<uint64_t> total_measurements_;
    std::atomic<DurationType> last_measurement_;

    const size_t max_samples_;

    mutable std::mutex samples_mutex_;
    std::vector<DurationType> samples_;

    void addSample(DurationType duration) noexcept;
    static DurationType calculatePercentileImpl(const std::vector<DurationType>& sorted_samples, double percentile);
};

} // namespace axonvex::core
