/**
 * @file precision_timer.cpp
 * @brief High-Precision Timer Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * This file implements the PrecisionTimer class for high-resolution timing
 * with nanosecond accuracy and minimal overhead.
 */

#include <axonvex/core/precisionTimer.hpp>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace axonvex::core {

PrecisionTimer::PrecisionTimer(size_t max_samples)
    : start_time_(ClockType::now())
    , stop_time_(ClockType::now())
    , running_(false)
    , statistics_enabled_(false)
    , total_measurements_(0)
    , last_measurement_(DurationType::zero())
    , max_samples_(max_samples) {
    samples_.reserve(max_samples);
}

void PrecisionTimer::start() noexcept {
    start_time_ = ClockType::now();
    running_ = true;
}

void PrecisionTimer::stop() noexcept {
    if (!running_) {
        return;
    }
    
    auto stop_time = ClockType::now();
    stop_time_ = stop_time;
    running_ = false;
    
    auto start_time = start_time_.load();
    auto elapsed = std::chrono::duration_cast<DurationType>(stop_time - start_time);
    last_measurement_ = elapsed;
    total_measurements_++;
    
    if (statistics_enabled_) {
        addSample(elapsed);
    }
}

void PrecisionTimer::reset() noexcept {
    running_ = false;
    total_measurements_ = 0;
    last_measurement_ = DurationType::zero();
    
    std::lock_guard<std::mutex> lock(samples_mutex_);
    samples_.clear();
}

PrecisionTimer::DurationType PrecisionTimer::lap() noexcept {
    if (!running_) {
        return DurationType::zero();
    }
    
    auto current_time = ClockType::now();
    auto start_time = start_time_.load();
    auto elapsed = std::chrono::duration_cast<DurationType>(current_time - start_time);
    
    // Reset start time for next lap
    start_time_ = current_time;
    
    last_measurement_ = elapsed;
    total_measurements_++;
    
    if (statistics_enabled_) {
        addSample(elapsed);
    }
    
    return elapsed;
}

PrecisionTimer::DurationType PrecisionTimer::getElapsedNanoseconds() const {
    if (!running_ && total_measurements_ == 0) {
        throw std::runtime_error("Timer has not been started");
    }
    
    auto current_time = ClockType::now();
    auto start_time = start_time_.load();
    
    if (running_) {
        return std::chrono::duration_cast<DurationType>(current_time - start_time);
    } else {
        return last_measurement_;
    }
}

double PrecisionTimer::getElapsedSeconds() const {
    auto nanoseconds = getElapsedNanoseconds();
    return nanoseconds.count() / 1e9;
}

double PrecisionTimer::getElapsedMilliseconds() const {
    auto nanoseconds = getElapsedNanoseconds();
    return nanoseconds.count() / 1e6;
}

double PrecisionTimer::getElapsedMicroseconds() const {
    auto nanoseconds = getElapsedNanoseconds();
    return nanoseconds.count() / 1e3;
}

bool PrecisionTimer::isRunning() const noexcept {
    return running_;
}

void PrecisionTimer::enableStatistics(bool enable) noexcept {
    statistics_enabled_ = enable;
}

bool PrecisionTimer::isStatisticsEnabled() const noexcept {
    return statistics_enabled_;
}

size_t PrecisionTimer::getSampleCount() const noexcept {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    return samples_.size();
}

uint64_t PrecisionTimer::getTotalMeasurements() const noexcept {
    return total_measurements_;
}

void PrecisionTimer::clearSamples() noexcept {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    samples_.clear();
}

TimingStatistics PrecisionTimer::getStatistics() const {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    if (samples_.empty()) {
        throw std::runtime_error("No samples collected for statistics");
    }
    
    TimingStatistics stats;
    stats.sample_count = samples_.size();
    stats.total_measurements = total_measurements_;
    
    // Create sorted copy for percentile calculations
    std::vector<DurationType> sorted_samples = samples_;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    stats.min = sorted_samples.front();
    stats.max = sorted_samples.back();
    
    // Calculate mean
    auto sum = std::accumulate(sorted_samples.begin(), sorted_samples.end(), DurationType::zero());
    stats.mean = DurationType(sum.count() / sorted_samples.size());
    
    // Calculate median (50th percentile)
    stats.median = calculatePercentileImpl(sorted_samples, 0.5);
    
    return stats;
}

double PrecisionTimer::calculatePercentile(double percentile) const {
    if (percentile < 0.0 || percentile > 1.0) {
        throw std::invalid_argument("Percentile must be between 0.0 and 1.0");
    }
    
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    if (samples_.empty()) {
        throw std::runtime_error("No samples collected for percentile calculation");
    }
    
    // Create sorted copy
    std::vector<DurationType> sorted_samples = samples_;
    std::sort(sorted_samples.begin(), sorted_samples.end());
    
    auto result = calculatePercentileImpl(sorted_samples, percentile);
    return result.count();
}

PrecisionTimer::DurationType PrecisionTimer::estimateOverhead(size_t iterations) {
    PrecisionTimer timer;
    std::vector<DurationType> measurements;
    measurements.reserve(iterations);
    
    // Warm up
    for (size_t i = 0; i < 10; ++i) {
        timer.start();
        timer.stop();
    }
    
    // Measure overhead
    for (size_t i = 0; i < iterations; ++i) {
        timer.start();
        timer.stop();
        measurements.push_back(timer.getElapsedNanoseconds());
    }
    
    // Return minimum time (best case overhead)
    return *std::min_element(measurements.begin(), measurements.end());
}

PrecisionTimer::DurationType PrecisionTimer::getClockResolution() {
    // Measure smallest observable time difference
    auto t1 = ClockType::now();
    auto t2 = ClockType::now();
    
    while (t2 == t1) {
        t2 = ClockType::now();
    }
    
    return std::chrono::duration_cast<DurationType>(t2 - t1);
}

bool PrecisionTimer::isClockSteady() {
    return ClockType::is_steady;
}

void PrecisionTimer::addSample(DurationType duration) noexcept {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    
    if (samples_.size() >= max_samples_) {
        // Remove oldest sample to make room for new one
        samples_.erase(samples_.begin());
    }
    
    samples_.push_back(duration);
}

PrecisionTimer::DurationType PrecisionTimer::calculatePercentileImpl(
    const std::vector<DurationType>& sorted_samples, double percentile) {
    
    if (sorted_samples.empty()) {
        return DurationType::zero();
    }
    
    if (sorted_samples.size() == 1) {
        return sorted_samples[0];
    }
    
    // Calculate index for percentile
    double index = percentile * (sorted_samples.size() - 1);
    size_t lower_index = static_cast<size_t>(index);
    size_t upper_index = lower_index + 1;
    
    if (upper_index >= sorted_samples.size()) {
        return sorted_samples.back();
    }
    
    // Linear interpolation between the two nearest values
    double weight = index - lower_index;
    auto lower_value = sorted_samples[lower_index];
    auto upper_value = sorted_samples[upper_index];
    
    auto interpolated = lower_value + 
        DurationType(static_cast<long long>(weight * (upper_value - lower_value).count()));
    
    return interpolated;
}

} // namespace axonvex::core 