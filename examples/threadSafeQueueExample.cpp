/**
 * @file threadSafeQueueExample.cpp
 * @brief Example demonstrating ThreadSafeQueue usage in multi-threaded scenarios
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This example demonstrates the ThreadSafeQueue class with:
 * - Multi-threaded producer-consumer scenarios
 * - Performance benchmarking
 * - Statistics collection and analysis
 * - Error handling and edge cases
 */

#include <atomic>
#include <axonvex/axonvex.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace axonvex::core;

// Test message structure
struct TestMessage {
    int id;
    std::string data;
    std::chrono::high_resolution_clock::time_point timestamp;

    TestMessage() = default;
    TestMessage(int _id, const std::string& _data)
        : id(_id), data(_data), timestamp(std::chrono::high_resolution_clock::now()) {}
};

// Producer function
void producer(ThreadSafeQueue<TestMessage>& queue, int producer_id, int message_count,
              std::atomic<int>& messages_produced) {

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> delay_dist(1, 10);

    for (int i = 0; i < message_count; ++i) {
        TestMessage msg(producer_id * 1000 + i, "Producer " + std::to_string(producer_id) +
                                                    " Message " + std::to_string(i));

        bool success = queue.enqueue(std::move(msg));
        if (success) {
            messages_produced.fetch_add(1);
        }

        // Simulate variable work time
        std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
    }
}

// Consumer function
void consumer(ThreadSafeQueue<TestMessage>& queue, int consumer_id, int expected_messages,
              std::atomic<int>& messages_consumed, std::atomic<bool>& should_stop) {

    while (!should_stop.load() || !queue.isEmpty()) {
        auto msg = queue.dequeue();
        if (msg.has_value()) {
            messages_consumed.fetch_add(1);

            // Calculate message latency
            auto now = std::chrono::high_resolution_clock::now();
            auto latency =
                std::chrono::duration_cast<std::chrono::microseconds>(now - msg->timestamp).count();

            // Simulate processing time
            std::this_thread::sleep_for(std::chrono::microseconds(5));

            // Print occasional status
            if (msg->id % 100 == 0) {
                std::cout << "Consumer " << consumer_id << " processed message " << msg->id
                          << " (latency: " << latency << " μs)" << std::endl;
            }
        } else {
            // Brief pause when queue is empty
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

// Performance benchmark function
template <typename T>
void benchmarkQueue(ThreadSafeQueue<T>& queue, int iterations, const std::string& type_name) {

    std::cout << "\n=== Performance Benchmark: " << type_name << " ===\n";

    PrecisionTimer timer;

    // Benchmark enqueue operations
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        T item{};
        queue.enqueue(std::move(item));
    }
    timer.stop();

    double enqueue_time = timer.getElapsedMicroseconds();
    double enqueue_rate = iterations / (enqueue_time / 1e6);

    std::cout << "Enqueue performance:\n";
    std::cout << "  Total time: " << enqueue_time << " μs\n";
    std::cout << "  Average per operation: " << (enqueue_time / iterations) << " μs\n";
    std::cout << "  Operations per second: " << enqueue_rate << "\n";

    // Benchmark dequeue operations
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        auto item = queue.dequeue();
        (void)item; // Suppress unused variable warning
    }
    timer.stop();

    double dequeue_time = timer.getElapsedMicroseconds();
    double dequeue_rate = iterations / (dequeue_time / 1e6);

    std::cout << "Dequeue performance:\n";
    std::cout << "  Total time: " << dequeue_time << " μs\n";
    std::cout << "  Average per operation: " << (dequeue_time / iterations) << " μs\n";
    std::cout << "  Operations per second: " << dequeue_rate << "\n";
}

int main() {
    try {
        // Print welcome message
        axonvex::printWelcome();

        std::cout << "=== AxonVex ThreadSafeQueue Example ===\n\n";

        // Test 1: Basic functionality
        std::cout << "Test 1: Basic queue operations\n";
        std::cout << "-------------------------------\n";

        ThreadSafeQueue<int> basic_queue(64);

        // Test enqueue and dequeue
        std::cout << "Enqueuing numbers 1-10...\n";
        for (int i = 1; i <= 10; ++i) {
            bool success = basic_queue.enqueue(i);
            std::cout << "Enqueue " << i << ": " << (success ? "Success" : "Failed") << "\n";
        }

        std::cout << "Queue size: " << basic_queue.size() << "\n";
        std::cout << "Queue capacity: " << basic_queue.capacity() << "\n";

        std::cout << "\nDequeuing all elements...\n";
        while (!basic_queue.isEmpty()) {
            auto item = basic_queue.dequeue();
            if (item.has_value()) {
                std::cout << "Dequeued: " << item.value() << "\n";
            }
        }

        std::cout << "Queue size after dequeue: " << basic_queue.size() << "\n";
        std::cout << "\n";

        // Test 2: Multi-threaded producer-consumer
        std::cout << "Test 2: Multi-threaded producer-consumer\n";
        std::cout << "----------------------------------------\n";

        constexpr int num_producers = 3;
        constexpr int num_consumers = 2;
        constexpr int messages_per_producer = 200;
        constexpr int total_messages = num_producers * messages_per_producer;

        ThreadSafeQueue<TestMessage> mt_queue(1024);

        std::atomic<int> messages_produced{0};
        std::atomic<int> messages_consumed{0};
        std::atomic<bool> should_stop{false};

        std::cout << "Starting " << num_producers << " producers and " << num_consumers
                  << " consumers...\n";
        std::cout << "Total messages to process: " << total_messages << "\n";

        auto start_time = std::chrono::high_resolution_clock::now();

        // Start producers
        std::vector<std::future<void>> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.push_back(std::async(std::launch::async, producer, std::ref(mt_queue), i,
                                           messages_per_producer, std::ref(messages_produced)));
        }

        // Start consumers
        std::vector<std::future<void>> consumers;
        for (int i = 0; i < num_consumers; ++i) {
            consumers.push_back(std::async(std::launch::async, consumer, std::ref(mt_queue), i,
                                           total_messages, std::ref(messages_consumed),
                                           std::ref(should_stop)));
        }

        // Wait for all producers to finish
        for (auto& future : producers) {
            future.wait();
        }

        std::cout << "All producers finished. Produced: " << messages_produced.load()
                  << " messages\n";

        // Wait for consumers to finish all messages
        while (messages_consumed.load() < total_messages && !mt_queue.isEmpty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        should_stop.store(true);

        // Wait for all consumers to finish
        for (auto& future : consumers) {
            future.wait();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        std::cout << "All consumers finished. Consumed: " << messages_consumed.load()
                  << " messages\n";
        std::cout << "Total execution time: " << total_time << " ms\n";
        std::cout << "Throughput: " << (total_messages * 1000.0 / total_time)
                  << " messages/second\n";

        // Display queue statistics
        const auto& stats = mt_queue.getStatistics();
        std::cout << "\nQueue Statistics:\n";
        std::cout << "  Enqueue count: " << stats.getEnqueueCount() << "\n";
        std::cout << "  Dequeue count: " << stats.getDequeueCount() << "\n";
        std::cout << "  Enqueue failures: " << stats.getEnqueueFailures() << "\n";
        std::cout << "  Dequeue failures: " << stats.getDequeueFailures() << "\n";
        std::cout << "  Max size reached: " << stats.getMaxSizeReached() << "\n";
        std::cout << "\n";

        // Test 3: Performance benchmarks
        std::cout << "Test 3: Performance benchmarks\n";
        std::cout << "------------------------------\n";

        // Benchmark different data types
        ThreadSafeQueue<int> int_queue(10000);
        benchmarkQueue(int_queue, 5000, "int");

        ThreadSafeQueue<std::string> string_queue(10000);
        benchmarkQueue(string_queue, 5000, "std::string");

        std::cout << "\n=== Example completed successfully! ===\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
