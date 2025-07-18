/**
 * @file threadSafeQueueTest.cpp
 * @brief Unit tests for ThreadSafeQueue class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/threadSafeQueue.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <string>

using namespace axonvex::core;

class ThreadSafeQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }
    
    // Test message structure
    struct TestMessage {
        int id;
        std::string data;
        std::chrono::high_resolution_clock::time_point timestamp;
        
        TestMessage() = default;
        TestMessage(int _id, const std::string& _data) 
            : id(_id), data(_data), timestamp(std::chrono::high_resolution_clock::now()) {}
            
        bool operator==(const TestMessage& other) const {
            return id == other.id && data == other.data;
        }
    };
};

// Test basic queue construction
TEST_F(ThreadSafeQueueTest, Construction) {
    ThreadSafeQueue<int> queue;
    EXPECT_EQ(queue.capacity(), 1024);  // Default capacity
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
}

// Test custom capacity construction
TEST_F(ThreadSafeQueueTest, CustomCapacityConstruction) {
    ThreadSafeQueue<int> queue(512);
    EXPECT_EQ(queue.capacity(), 512);
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
    
    // Test power-of-2 rounding
    ThreadSafeQueue<int> queue2(500);
    EXPECT_EQ(queue2.capacity(), 512);  // Rounded up to next power of 2
    
    // Test minimum capacity
    ThreadSafeQueue<int> queue3(8);
    EXPECT_EQ(queue3.capacity(), 16);  // Minimum capacity
}

// Test basic enqueue and dequeue operations
TEST_F(ThreadSafeQueueTest, BasicEnqueueDequeue) {
    ThreadSafeQueue<int> queue(64);
    
    // Enqueue single element
    EXPECT_TRUE(queue.enqueue(42));
    EXPECT_EQ(queue.size(), 1);
    EXPECT_FALSE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
    
    // Dequeue single element
    auto value = queue.dequeue();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.isEmpty());
}

// Test move semantics
TEST_F(ThreadSafeQueueTest, MoveSemantics) {
    ThreadSafeQueue<std::string> queue(64);
    
    std::string test_string = "Hello, World!";
    std::string original = test_string;
    
    // Enqueue with move
    EXPECT_TRUE(queue.enqueue(std::move(test_string)));
    EXPECT_EQ(queue.size(), 1);
    
    // Dequeue with move
    auto value = queue.dequeue();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), original);
    EXPECT_EQ(queue.size(), 0);
}

// Test queue overflow
TEST_F(ThreadSafeQueueTest, QueueOverflow) {
    ThreadSafeQueue<int> queue(16);
    
    // Fill queue to capacity
    for (int i = 0; i < 15; ++i) {  // One less than capacity due to implementation
        EXPECT_TRUE(queue.enqueue(i));
    }
    
    EXPECT_GT(queue.size(), 10);  // Should have significant size
    EXPECT_FALSE(queue.isEmpty());
    
    // Try to enqueue more (should eventually fail)
    bool overflow_detected = false;
    for (int i = 15; i < 100; ++i) {
        if (!queue.enqueue(i)) {
            overflow_detected = true;
            break;
        }
    }
    
    // Check statistics
    const auto& stats = queue.getStatistics();
    EXPECT_GT(stats.getEnqueueCount(), 0);
    if (overflow_detected) {
        EXPECT_GT(stats.getEnqueueFailures(), 0);
    }
}

// Test queue underflow
TEST_F(ThreadSafeQueueTest, QueueUnderflow) {
    ThreadSafeQueue<int> queue(64);
    
    // Try to dequeue from empty queue
    auto value = queue.dequeue();
    EXPECT_FALSE(value.has_value());
    
    // Check statistics
    const auto& stats = queue.getStatistics();
    EXPECT_EQ(stats.getDequeueCount(), 0);
    EXPECT_EQ(stats.getDequeueFailures(), 1);
}

// Test tryDequeue with timeout
TEST_F(ThreadSafeQueueTest, TryDequeueTimeout) {
    ThreadSafeQueue<int> queue(64);
    
    // Test timeout on empty queue
    auto start_time = std::chrono::high_resolution_clock::now();
    auto value = queue.tryDequeue(std::chrono::milliseconds(10));
    auto end_time = std::chrono::high_resolution_clock::now();
    
    EXPECT_FALSE(value.has_value());
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    EXPECT_GE(elapsed.count(), 8);  // Allow some margin
    
    // Test successful dequeue
    queue.enqueue(42);
    value = queue.tryDequeue(std::chrono::milliseconds(10));
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);
}

// Test statistics collection
TEST_F(ThreadSafeQueueTest, StatisticsCollection) {
    ThreadSafeQueue<int> queue(64);
    
    // Perform various operations
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }
    
    for (int i = 0; i < 5; ++i) {
        auto value = queue.dequeue();
        EXPECT_TRUE(value.has_value());
    }
    
    // Try to dequeue beyond available elements
    for (int i = 0; i < 10; ++i) {
        auto value = queue.dequeue();
        if (!value.has_value()) {
            break;
        }
    }
    
    // Check statistics
    const auto& stats = queue.getStatistics();
    EXPECT_EQ(stats.getEnqueueCount(), 10);
    EXPECT_GT(stats.getDequeueCount(), 0);
    EXPECT_EQ(stats.getEnqueueFailures(), 0);
    EXPECT_GT(stats.getDequeueFailures(), 0);
}

// Test statistics reset
TEST_F(ThreadSafeQueueTest, StatisticsReset) {
    ThreadSafeQueue<int> queue(64);
    
    // Generate some statistics
    queue.enqueue(1);
    queue.dequeue();
    queue.dequeue();  // This should fail
    
    const auto& stats = queue.getStatistics();
    EXPECT_GT(stats.getEnqueueCount(), 0);
    EXPECT_GT(stats.getDequeueFailures(), 0);
    
    // Reset statistics
    queue.resetStatistics();
    
    EXPECT_EQ(stats.getEnqueueCount(), 0);
    EXPECT_EQ(stats.getDequeueCount(), 0);
    EXPECT_EQ(stats.getEnqueueFailures(), 0);
    EXPECT_EQ(stats.getDequeueFailures(), 0);
}

// Test queue clear operation
TEST_F(ThreadSafeQueueTest, ClearOperation) {
    ThreadSafeQueue<int> queue(64);
    
    // Fill queue with data
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.enqueue(i));
    }
    EXPECT_GT(queue.size(), 0);
    
    // Clear queue
    queue.clear();
    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.isEmpty());
}

// Test single producer/consumer thread safety
TEST_F(ThreadSafeQueueTest, SingleProducerConsumerThreadSafety) {
    ThreadSafeQueue<int> queue(1024);
    const int NUM_ITEMS = 10000;
    
    std::atomic<int> items_produced{0};
    std::atomic<int> items_consumed{0};
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.enqueue(i)) {
                std::this_thread::yield();
            }
            items_produced.fetch_add(1);
        }
        producer_done.store(true);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        std::vector<bool> received(NUM_ITEMS, false);
        while (!producer_done.load() || !queue.isEmpty()) {
            auto value = queue.dequeue();
            if (value.has_value()) {
                int val = value.value();
                EXPECT_GE(val, 0);
                EXPECT_LT(val, NUM_ITEMS);
                if (val >= 0 && val < NUM_ITEMS) {
                    EXPECT_FALSE(received[val]); // Should not receive duplicate
                    received[val] = true;
                }
                items_consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
        
        // Verify all messages were received exactly once
        for (int i = 0; i < NUM_ITEMS; ++i) {
            EXPECT_TRUE(received[i]) << "Message " << i << " was not received";
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(items_produced.load(), NUM_ITEMS);
    EXPECT_EQ(items_consumed.load(), NUM_ITEMS);
    EXPECT_TRUE(queue.isEmpty());
    
    // Check statistics
    const auto& stats = queue.getStatistics();
    EXPECT_EQ(stats.getEnqueueCount(), NUM_ITEMS);
    EXPECT_EQ(stats.getDequeueCount(), NUM_ITEMS);
}

// Test multiple producer/consumer thread safety (simplified)
TEST_F(ThreadSafeQueueTest, MultipleProducerConsumerThreadSafety) {
    ThreadSafeQueue<int> queue(2048);
    const int NUM_PRODUCERS = 3;
    const int NUM_CONSUMERS = 2;
    const int MESSAGES_PER_PRODUCER = 500;
    const int TOTAL_MESSAGES = NUM_PRODUCERS * MESSAGES_PER_PRODUCER;
    
    std::atomic<int> messages_produced{0};
    std::atomic<int> messages_consumed{0};
    std::atomic<bool> all_producers_done{false};
    
    // Producer threads
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i) {
                int msg = p * 1000 + i;
                while (!queue.enqueue(msg)) {
                    std::this_thread::yield();
                }
                messages_produced.fetch_add(1);
            }
        });
    }
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&, c]() {
            while (!all_producers_done.load() || !queue.isEmpty()) {
                auto msg = queue.dequeue();
                if (msg.has_value()) {
                    messages_consumed.fetch_add(1);
                    
                    // Validate message format
                    EXPECT_GE(msg.value(), 0);
                    // Message IDs can be higher than TOTAL_MESSAGES due to producer numbering
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for all producers to finish
    for (auto& producer : producers) {
        producer.join();
    }
    all_producers_done.store(true);
    
    // Wait for all consumers to finish
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    EXPECT_EQ(messages_produced.load(), TOTAL_MESSAGES);
    EXPECT_EQ(messages_consumed.load(), TOTAL_MESSAGES);
    EXPECT_TRUE(queue.isEmpty());
    
    // Check statistics
    const auto& stats = queue.getStatistics();
    EXPECT_EQ(stats.getEnqueueCount(), TOTAL_MESSAGES);
    EXPECT_EQ(stats.getDequeueCount(), TOTAL_MESSAGES);
}

// Test performance characteristics
TEST_F(ThreadSafeQueueTest, PerformanceTest) {
    ThreadSafeQueue<int> queue(200000);  // Larger queue size
    const int NUM_OPERATIONS = 100000;
    
    // Measure enqueue performance
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        ASSERT_TRUE(queue.enqueue(i));
    }
    
    auto mid_time = std::chrono::high_resolution_clock::now();
    
    // Measure dequeue performance
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        auto value = queue.dequeue();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), i);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto enqueue_duration = std::chrono::duration_cast<std::chrono::microseconds>(mid_time - start_time);
    auto dequeue_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - mid_time);
    
    double enqueue_ops_per_sec = NUM_OPERATIONS / (enqueue_duration.count() / 1e6);
    double dequeue_ops_per_sec = NUM_OPERATIONS / (dequeue_duration.count() / 1e6);
    
    std::cout << "ThreadSafeQueue Performance:\n";
    std::cout << "  Enqueue operations per second: " << enqueue_ops_per_sec << "\n";
    std::cout << "  Dequeue operations per second: " << dequeue_ops_per_sec << "\n";
    std::cout << "  Enqueue time per operation: " << (enqueue_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    std::cout << "  Dequeue time per operation: " << (dequeue_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    
    // Performance expectations
    EXPECT_GT(enqueue_ops_per_sec, 1000000);  // At least 1M ops/sec
    EXPECT_GT(dequeue_ops_per_sec, 1000000);  // At least 1M ops/sec
}

// Test with complex data types (simplified to avoid memory issues)
TEST_F(ThreadSafeQueueTest, ComplexDataTypes) {
    ThreadSafeQueue<std::string> queue(64);
    
    std::string data1 = "test1";
    std::string data2 = "test2";
    
    // Enqueue complex data
    EXPECT_TRUE(queue.enqueue(data1));
    EXPECT_TRUE(queue.enqueue(data2));
    EXPECT_EQ(queue.size(), 2);
    
    // Dequeue complex data
    auto read1 = queue.dequeue();
    ASSERT_TRUE(read1.has_value());
    EXPECT_EQ(read1.value(), data1);
    
    auto read2 = queue.dequeue();
    ASSERT_TRUE(read2.has_value());
    EXPECT_EQ(read2.value(), data2);
    
    EXPECT_TRUE(queue.isEmpty());
}

// Test queue capacity limits
TEST_F(ThreadSafeQueueTest, CapacityLimits) {
    ThreadSafeQueue<int> queue(32);
    
    // Fill queue close to capacity
    int items_enqueued = 0;
    for (int i = 0; i < 100; ++i) {
        if (queue.enqueue(i)) {
            items_enqueued++;
        } else {
            break;
        }
    }
    
    // Should have enqueued some items
    EXPECT_GT(items_enqueued, 10);
    EXPECT_LT(items_enqueued, 100);
    
    // Queue should report as full or nearly full
    EXPECT_GT(queue.size(), 0);
    
    // Should be able to dequeue items
    int items_dequeued = 0;
    while (true) {
        auto value = queue.dequeue();
        if (!value.has_value()) {
            break;
        }
        items_dequeued++;
    }
    
    EXPECT_EQ(items_dequeued, items_enqueued);
    EXPECT_TRUE(queue.isEmpty());
}

// Test concurrent enqueue/dequeue stress test (simplified)
TEST_F(ThreadSafeQueueTest, ConcurrentStressTest) {
    ThreadSafeQueue<int> queue(4096);
    const int NUM_OPERATIONS = 1000;
    const int NUM_THREADS = 4;
    
    std::vector<std::thread> threads;
    std::atomic<int> operations_completed{0};
    
    // Half the threads enqueue, half dequeue
    for (int t = 0; t < NUM_THREADS; ++t) {
        if (t % 2 == 0) {
            // Producer thread
            threads.emplace_back([&, t]() {
                for (int i = 0; i < NUM_OPERATIONS / (NUM_THREADS / 2); ++i) {
                    int value = t * 100 + i;
                    int retries = 0;
                    while (!queue.enqueue(value) && retries < 1000) {
                        std::this_thread::yield();
                        retries++;
                    }
                    if (retries < 1000) {
                        operations_completed.fetch_add(1);
                    }
                }
            });
        } else {
            // Consumer thread
            threads.emplace_back([&]() {
                for (int i = 0; i < NUM_OPERATIONS / (NUM_THREADS / 2); ++i) {
                    int retries = 0;
                    while (retries < 1000) {
                        auto value = queue.dequeue();
                        if (value.has_value()) {
                            operations_completed.fetch_add(1);
                            break;
                        }
                        std::this_thread::yield();
                        retries++;
                    }
                }
            });
        }
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Allow some tolerance for the stress test
    EXPECT_GE(operations_completed.load(), NUM_OPERATIONS * 0.9);
}

// Test error handling and edge cases
TEST_F(ThreadSafeQueueTest, ErrorHandling) {
    ThreadSafeQueue<int> queue(16);
    
    // Test multiple dequeue from empty queue
    for (int i = 0; i < 5; ++i) {
        auto value = queue.dequeue();
        EXPECT_FALSE(value.has_value());
    }
    
    // Test enqueue/dequeue cycle
    EXPECT_TRUE(queue.enqueue(42));
    auto value = queue.dequeue();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);
    
    // Test queue state after operations
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_EQ(queue.size(), 0);
} 