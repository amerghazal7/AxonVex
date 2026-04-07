/**
 * @file precision_timer_example.cpp
 * @brief Example demonstrating PrecisionTimer usage
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This example demonstrates the basic usage of the PrecisionTimer class
 * including timing measurements and statistics collection.
 */

#include <axonvex_core/axonvex.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

using namespace axonvex::core;

// Simulate some work
void simulateWork(int microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

// Simulate CPU-intensive work
void simulateCPUWork(int iterations) {
    volatile double result = 0.0;
    for (int i = 0; i < iterations; ++i) {
        result += std::sin(i) * std::cos(i);
    }
}

int main() {
    try {
        // Print welcome message
        axonvex::printWelcome();

        std::cout << "=== AxonVex PrecisionTimer Example ===\n\n";

        // Test 1: Basic timing
        std::cout << "Test 1: Basic timing measurement\n";
        std::cout << "-----------------------------------\n";

        PrecisionTimer timer;

        timer.start();
        simulateWork(1000); // 1ms work
        timer.stop();

        std::cout << "Work duration: " << timer.getElapsedMicroseconds() << " μs\n";
        std::cout << "Work duration: " << timer.getElapsedMilliseconds() << " ms\n";
        std::cout << "Work duration: " << timer.getElapsedNanoseconds().count() << " ns\n";
        std::cout << "\n";

        // Test 2: Clock information
        std::cout << "Test 2: Clock information\n";
        std::cout << "-------------------------\n";

        std::cout << "Clock is steady: " << (PrecisionTimer::isClockSteady() ? "Yes" : "No")
                  << "\n";
        std::cout << "Clock resolution: " << PrecisionTimer::getClockResolution().count()
                  << " ns\n";
        std::cout << "Timer overhead: " << PrecisionTimer::estimateOverhead(1000).count()
                  << " ns\n";
        std::cout << "\n";

        // Test 3: Statistics collection
        std::cout << "Test 3: Statistics collection\n";
        std::cout << "-----------------------------\n";

        PrecisionTimer statsTimer;
        statsTimer.enableStatistics(true);

        std::cout << "Performing 100 timing measurements...\n";

        // Perform multiple measurements
        for (int i = 0; i < 100; ++i) {
            statsTimer.start();
            simulateCPUWork(1000 + i * 10); // Variable work
            statsTimer.stop();
        }

        // Get statistics
        auto stats = statsTimer.getStatistics();

        std::cout << "Statistics:\n";
        std::cout << "  Sample count: " << stats.sample_count << "\n";
        std::cout << "  Total measurements: " << stats.total_measurements << "\n";
        std::cout << "  Min: " << stats.min.count() << " ns\n";
        std::cout << "  Max: " << stats.max.count() << " ns\n";
        std::cout << "  Mean: " << stats.mean.count() << " ns\n";
        std::cout << "  Median: " << stats.median.count() << " ns\n";
        std::cout << "\n";

        // Test 4: Percentile calculations
        std::cout << "Test 4: Percentile calculations\n";
        std::cout << "-------------------------------\n";

        std::cout << "95th percentile: " << statsTimer.calculatePercentile(0.95) << " ns\n";
        std::cout << "99th percentile: " << statsTimer.calculatePercentile(0.99) << " ns\n";
        std::cout << "99.9th percentile: " << statsTimer.calculatePercentile(0.999) << " ns\n";
        std::cout << "\n";

        // Test 5: Lap timing
        std::cout << "Test 5: Lap timing\n";
        std::cout << "------------------\n";

        PrecisionTimer lapTimer;
        lapTimer.start();

        for (int i = 0; i < 5; ++i) {
            simulateWork(200); // 200μs work
            auto lapTime = lapTimer.lap();
            std::cout << "Lap " << (i + 1) << ": " << lapTime.count() << " ns\n";
        }

        std::cout << "\n";

        // Test 6: Performance comparison
        std::cout << "Test 6: Performance comparison\n";
        std::cout << "------------------------------\n";

        const int iterations = 10000;

        // Test overhead of timer operations
        auto startTime = std::chrono::high_resolution_clock::now();

        PrecisionTimer perfTimer;
        for (int i = 0; i < iterations; ++i) {
            perfTimer.start();
            perfTimer.stop();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);

        std::cout << "Timer overhead per operation: " << (totalTime.count() / iterations)
                  << " ns\n";
        std::cout << "Total time for " << iterations << " operations: " << totalTime.count()
                  << " ns\n";

        std::cout << "\n";
        std::cout << "=== Example completed successfully! ===\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
