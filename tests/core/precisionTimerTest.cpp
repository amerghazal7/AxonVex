/**
 * @file precisionTimerTest.cpp
 * @brief Unit tests for PrecisionTimer class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/precisionTimer.hpp>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cmath>

using namespace axonvex::core;

class PrecisionTimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
    }

    void TearDown() override {
        // Clean up test fixtures
    }

    // Helper function to simulate work
    void simulateWork(int microseconds) {
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    }

    // Helper function to simulate CPU work
    void simulateCPUWork(int iterations) {
        volatile double result = 0.0;
        for (int i = 0; i < iterations; ++i) {
            result += std::sin(i) * std::cos(i);
        }
    }
};

// Test basic timer construction
TEST_F(PrecisionTimerTest, Construction) {
    PrecisionTimer timer;
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(timer.isStatisticsEnabled());
    EXPECT_EQ(timer.getSampleCount(), 0);
    EXPECT_EQ(timer.getTotalMeasurements(), 0);
}

// Test custom capacity construction
TEST_F(PrecisionTimerTest, CustomCapacityConstruction) {
    PrecisionTimer timer(5000);
    EXPECT_FALSE(timer.isRunning());
    EXPECT_EQ(timer.getSampleCount(), 0);
    EXPECT_EQ(timer.getTotalMeasurements(), 0);
}

// Test basic start/stop operations
TEST_F(PrecisionTimerTest, BasicStartStop) {
    PrecisionTimer timer;

    // Timer should not be running initially
    EXPECT_FALSE(timer.isRunning());

    // Start timer
    timer.start();
    EXPECT_TRUE(timer.isRunning());

    // Simulate some work
    simulateWork(1000); // 1ms

    // Stop timer
    timer.stop();
    EXPECT_FALSE(timer.isRunning());

    // Check that we got a reasonable measurement
    auto elapsed = timer.getElapsedNanoseconds();
    EXPECT_GT(elapsed.count(), 500000); // At least 0.5ms
    EXPECT_LT(elapsed.count(), 5000000); // Less than 5ms
}

// Test time unit conversions
TEST_F(PrecisionTimerTest, TimeUnitConversions) {
    PrecisionTimer timer;

    timer.start();
    simulateWork(1000); // 1ms
    timer.stop();

    auto nanoseconds = timer.getElapsedNanoseconds();
    auto microseconds = timer.getElapsedMicroseconds();
    auto milliseconds = timer.getElapsedMilliseconds();
    auto seconds = timer.getElapsedSeconds();

    // Check conversions are reasonable
    EXPECT_GT(nanoseconds.count(), 500000);
    EXPECT_GT(microseconds, 500.0);
    EXPECT_GT(milliseconds, 0.5);
    EXPECT_GT(seconds, 0.0005);

    // Check conversion relationships
    EXPECT_NEAR(microseconds, nanoseconds.count() / 1000.0, 50.0);
    EXPECT_NEAR(milliseconds, microseconds / 1000.0, 0.1);
    EXPECT_NEAR(seconds, milliseconds / 1000.0, 0.001);
}

// Test lap timing
TEST_F(PrecisionTimerTest, LapTiming) {
    PrecisionTimer timer;

    timer.start();

    // Take multiple laps
    std::vector<PrecisionTimer::DurationType> lap_times;
    for (int i = 0; i < 5; ++i) {
        simulateWork(100); // 100μs each
        auto lap_time = timer.lap();
        lap_times.push_back(lap_time);
        EXPECT_GT(lap_time.count(), 50000); // At least 50μs
    }

    // All lap times should be reasonable
    for (const auto& lap_time : lap_times) {
        EXPECT_GT(lap_time.count(), 50000);
        EXPECT_LT(lap_time.count(), 1000000); // Less than 1ms
    }

    EXPECT_EQ(timer.getTotalMeasurements(), 5);
}

// Test statistics enabled/disabled
TEST_F(PrecisionTimerTest, StatisticsToggle) {
    PrecisionTimer timer;

    // Statistics should be disabled by default
    EXPECT_FALSE(timer.isStatisticsEnabled());

    // Enable statistics
    timer.enableStatistics(true);
    EXPECT_TRUE(timer.isStatisticsEnabled());

    // Disable statistics
    timer.enableStatistics(false);
    EXPECT_FALSE(timer.isStatisticsEnabled());
}

// Test statistics collection
TEST_F(PrecisionTimerTest, StatisticsCollection) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform multiple measurements
    const int num_measurements = 100;
    for (int i = 0; i < num_measurements; ++i) {
        timer.start();
        simulateCPUWork(100 + i); // Variable work
        timer.stop();
    }

    // Check sample count
    EXPECT_EQ(timer.getSampleCount(), num_measurements);
    EXPECT_EQ(timer.getTotalMeasurements(), num_measurements);

    // Get statistics
    auto stats = timer.getStatistics();
    EXPECT_TRUE(stats.isValid());
    EXPECT_EQ(stats.sample_count, num_measurements);
    EXPECT_EQ(stats.total_measurements, num_measurements);

    // Check that min <= mean <= max
    EXPECT_LE(stats.min.count(), stats.mean.count());
    EXPECT_LE(stats.mean.count(), stats.max.count());
    EXPECT_GT(stats.min.count(), 0);
    EXPECT_GT(stats.max.count(), 0);
}

// Test percentile calculations
TEST_F(PrecisionTimerTest, PercentileCalculations) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform measurements with known distribution
    const int num_measurements = 1000;
    for (int i = 0; i < num_measurements; ++i) {
        timer.start();
        simulateCPUWork(100 + (i % 100)); // Controlled variation
        timer.stop();
    }

    // Test percentile calculations
    auto p50 = timer.calculatePercentile(0.5);  // Median
    auto p95 = timer.calculatePercentile(0.95); // 95th percentile
    auto p99 = timer.calculatePercentile(0.99); // 99th percentile

    // Percentiles should be in ascending order
    EXPECT_LE(p50, p95);
    EXPECT_LE(p95, p99);
    EXPECT_GT(p50, 0);
    EXPECT_GT(p95, 0);
    EXPECT_GT(p99, 0);
}

// Test percentile edge cases
TEST_F(PrecisionTimerTest, PercentileEdgeCases) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Test with no samples
    EXPECT_THROW(timer.calculatePercentile(0.5), std::runtime_error);

    // Take one measurement
    timer.start();
    simulateCPUWork(1000);
    timer.stop();

    // Test edge percentiles
    auto p0 = timer.calculatePercentile(0.0);
    auto p100 = timer.calculatePercentile(1.0);
    EXPECT_EQ(p0, p100); // Should be same for single sample

    // Test invalid percentiles
    EXPECT_THROW(timer.calculatePercentile(-0.1), std::invalid_argument);
    EXPECT_THROW(timer.calculatePercentile(1.1), std::invalid_argument);
}

// Test statistics reset
TEST_F(PrecisionTimerTest, StatisticsReset) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform some measurements
    for (int i = 0; i < 10; ++i) {
        timer.start();
        simulateCPUWork(100);
        timer.stop();
    }

    EXPECT_EQ(timer.getSampleCount(), 10);
    EXPECT_EQ(timer.getTotalMeasurements(), 10);

    // Reset statistics (use reset() which clears everything)
    timer.reset();
    EXPECT_EQ(timer.getSampleCount(), 0);
    EXPECT_EQ(timer.getTotalMeasurements(), 0);
}

// Test timer reset
TEST_F(PrecisionTimerTest, TimerReset) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform some measurements
    timer.start();
    simulateWork(100);
    timer.stop();

    EXPECT_EQ(timer.getTotalMeasurements(), 1);

    // Reset timer
    timer.reset();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_EQ(timer.getSampleCount(), 0);
    EXPECT_EQ(timer.getTotalMeasurements(), 0);
}

// Test clear samples
TEST_F(PrecisionTimerTest, ClearSamples) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform some measurements
    for (int i = 0; i < 5; ++i) {
        timer.start();
        simulateCPUWork(100);
        timer.stop();
    }

    EXPECT_EQ(timer.getSampleCount(), 5);
    EXPECT_EQ(timer.getTotalMeasurements(), 5);

    // Clear samples
    timer.clearSamples();
    EXPECT_EQ(timer.getSampleCount(), 0);
    EXPECT_EQ(timer.getTotalMeasurements(), 5); // Total measurements should remain
}

// Test error handling
TEST_F(PrecisionTimerTest, ErrorHandling) {
    PrecisionTimer timer;

    // Test getting elapsed time without starting
    EXPECT_THROW(timer.getElapsedNanoseconds(), std::runtime_error);
    EXPECT_THROW(timer.getElapsedMicroseconds(), std::runtime_error);
    EXPECT_THROW(timer.getElapsedMilliseconds(), std::runtime_error);
    EXPECT_THROW(timer.getElapsedSeconds(), std::runtime_error);

    // Test getting statistics without samples
    EXPECT_THROW(timer.getStatistics(), std::runtime_error);
}

// Test static utility functions
TEST_F(PrecisionTimerTest, StaticUtilities) {
    // Test clock properties
    auto is_steady = PrecisionTimer::isClockSteady();
    EXPECT_TRUE(is_steady || !is_steady); // Should not throw

    // Test clock resolution
    auto resolution = PrecisionTimer::getClockResolution();
    EXPECT_GT(resolution.count(), 0);
    EXPECT_LT(resolution.count(), 1000000); // Should be less than 1ms

    // Test overhead estimation
    auto overhead = PrecisionTimer::estimateOverhead(1000);
    EXPECT_GT(overhead.count(), 0);
    EXPECT_LT(overhead.count(), 1000000); // Should be less than 1ms
}

// Test thread safety - using per-thread timers for proper thread safety
TEST_F(PrecisionTimerTest, ThreadSafety) {
    const int num_threads = 4;
    const int measurements_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> completed_measurements{0};
    std::atomic<uint64_t> total_measurements_from_all_timers{0};

    // Start multiple threads, each with its own timer
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &completed_measurements, &total_measurements_from_all_timers, measurements_per_thread]() {
            // Each thread gets its own timer instance for proper thread safety
            PrecisionTimer thread_timer;
            thread_timer.enableStatistics(true);

            for (int i = 0; i < measurements_per_thread; ++i) {
                thread_timer.start();
                simulateCPUWork(10);
                thread_timer.stop();
                completed_measurements.fetch_add(1);
            }

            // Add this thread's measurements to the total
            total_measurements_from_all_timers.fetch_add(thread_timer.getTotalMeasurements());
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Check that all measurements were recorded
    EXPECT_EQ(completed_measurements.load(), num_threads * measurements_per_thread);
    EXPECT_EQ(total_measurements_from_all_timers.load(), num_threads * measurements_per_thread);
}

// Test performance characteristics
TEST_F(PrecisionTimerTest, PerformanceTest) {
    PrecisionTimer timer;
    const int num_operations = 100000;

    // Measure timer overhead
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        timer.start();
        timer.stop();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    double overhead_per_operation = static_cast<double>(total_time.count()) / num_operations;

    std::cout << "PrecisionTimer Performance:\n";
    std::cout << "  Operations: " << num_operations << "\n";
    std::cout << "  Total time: " << total_time.count() << " ns\n";
    std::cout << "  Overhead per operation: " << overhead_per_operation << " ns\n";
    std::cout << "  Operations per second: " << (num_operations / (total_time.count() / 1e9)) << "\n";

    // Performance expectations
    EXPECT_LT(overhead_per_operation, 100.0); // Should be less than 100ns per operation
    EXPECT_GT(num_operations / (total_time.count() / 1e9), 1000000); // At least 1M ops/sec
}

// Test maximum sample capacity
TEST_F(PrecisionTimerTest, MaxSampleCapacity) {
    PrecisionTimer timer(100); // Small capacity for testing
    timer.enableStatistics(true);

    // Perform more measurements than capacity
    for (int i = 0; i < 150; ++i) {
        timer.start();
        simulateCPUWork(10);
        timer.stop();
    }

    // Should not exceed capacity
    EXPECT_LE(timer.getSampleCount(), 100);
    EXPECT_EQ(timer.getTotalMeasurements(), 150);

    // Statistics should still work
    auto stats = timer.getStatistics();
    EXPECT_TRUE(stats.isValid());
    EXPECT_EQ(stats.total_measurements, 150);
}

// Test elapsed time while running
TEST_F(PrecisionTimerTest, ElapsedTimeWhileRunning) {
    PrecisionTimer timer;

    timer.start();
    simulateWork(100);

    // Should be able to get elapsed time while running
    auto elapsed1 = timer.getElapsedNanoseconds();
    EXPECT_GT(elapsed1.count(), 50000); // At least 50μs

    simulateWork(100);

    auto elapsed2 = timer.getElapsedNanoseconds();
    EXPECT_GT(elapsed2.count(), elapsed1.count()); // Should increase

    timer.stop();
}

// Test multiple start/stop cycles
TEST_F(PrecisionTimerTest, MultipleStartStopCycles) {
    PrecisionTimer timer;
    timer.enableStatistics(true);

    // Perform multiple timing cycles
    for (int i = 0; i < 10; ++i) {
        timer.start();
        simulateCPUWork(100);
        timer.stop();
    }

    EXPECT_EQ(timer.getTotalMeasurements(), 10);
    EXPECT_EQ(timer.getSampleCount(), 10);

    // All measurements should be reasonable
    auto stats = timer.getStatistics();
    EXPECT_TRUE(stats.isValid());
    EXPECT_GT(stats.min.count(), 0);
    EXPECT_GT(stats.max.count(), 0);
    EXPECT_EQ(stats.sample_count, 10);
}
