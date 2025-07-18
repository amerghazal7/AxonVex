/**
 * @file circularBufferTest.cpp
 * @brief Unit tests for CircularBuffer class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/circularBuffer.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>

using namespace axonvex::core;

class CircularBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }
    
    // Helper function to create test data
    std::vector<int> createTestData(size_t count) {
        std::vector<int> data;
        data.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            data.push_back(static_cast<int>(i));
        }
        return data;
    }
};

// Test basic buffer construction
TEST_F(CircularBufferTest, Construction) {
    CircularBuffer<int> buffer;
    EXPECT_EQ(buffer.capacity(), 1024);  // Default capacity
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.available(), 1023);  // capacity - 1
}

// Test custom capacity construction
TEST_F(CircularBufferTest, CustomCapacityConstruction) {
    CircularBuffer<int> buffer(512);
    EXPECT_EQ(buffer.capacity(), 512);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    
    // Test power-of-2 rounding
    CircularBuffer<int> buffer2(500);
    EXPECT_EQ(buffer2.capacity(), 512);  // Rounded up to next power of 2
    
    // Test minimum capacity
    CircularBuffer<int> buffer3(8);
    EXPECT_EQ(buffer3.capacity(), 16);  // Minimum capacity
}

// Test basic write and read operations
TEST_F(CircularBufferTest, BasicWriteRead) {
    CircularBuffer<int> buffer(64);
    
    // Write single element
    EXPECT_TRUE(buffer.write(42));
    EXPECT_EQ(buffer.size(), 1);
    EXPECT_FALSE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    
    // Read single element
    auto value = buffer.read();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_TRUE(buffer.isEmpty());
}

// Test move semantics
TEST_F(CircularBufferTest, MoveSemantics) {
    CircularBuffer<std::string> buffer(64);
    
    std::string test_string = "Hello, World!";
    std::string original = test_string;
    
    // Write with move
    EXPECT_TRUE(buffer.write(std::move(test_string)));
    EXPECT_EQ(buffer.size(), 1);
    
    // Read with move
    auto value = buffer.read();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), original);
    EXPECT_EQ(buffer.size(), 0);
}

// Test buffer overflow
TEST_F(CircularBufferTest, BufferOverflow) {
    CircularBuffer<int> buffer(16);
    
    // Fill buffer to capacity - 1
    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    
    EXPECT_EQ(buffer.size(), 15);
    EXPECT_FALSE(buffer.isEmpty());
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.available(), 0);
    
    // Try to write one more (should fail)
    EXPECT_FALSE(buffer.write(15));
    EXPECT_EQ(buffer.size(), 15);
    
    // Check statistics
    const auto& stats = buffer.getStatistics();
    EXPECT_EQ(stats.getWriteCount(), 15);
    EXPECT_EQ(stats.getWriteFailures(), 1);
    EXPECT_EQ(stats.getOverruns(), 1);
}

// Test buffer underflow
TEST_F(CircularBufferTest, BufferUnderflow) {
    CircularBuffer<int> buffer(64);
    
    // Try to read from empty buffer
    auto value = buffer.read();
    EXPECT_FALSE(value.has_value());
    
    // Check statistics
    const auto& stats = buffer.getStatistics();
    EXPECT_EQ(stats.getReadCount(), 0);
    EXPECT_EQ(stats.getReadFailures(), 1);
    EXPECT_EQ(stats.getUnderruns(), 1);
}

// Test peek functionality
TEST_F(CircularBufferTest, PeekOperation) {
    CircularBuffer<int> buffer(64);
    
    // Write some data
    EXPECT_TRUE(buffer.write(100));
    EXPECT_TRUE(buffer.write(200));
    EXPECT_EQ(buffer.size(), 2);
    
    // Peek at first element
    auto peeked = buffer.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked.value(), 100);
    EXPECT_EQ(buffer.size(), 2);  // Size unchanged
    
    // Read first element
    auto read_value = buffer.read();
    ASSERT_TRUE(read_value.has_value());
    EXPECT_EQ(read_value.value(), 100);
    EXPECT_EQ(buffer.size(), 1);
    
    // Peek at second element
    peeked = buffer.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked.value(), 200);
    EXPECT_EQ(buffer.size(), 1);  // Size unchanged
}

// Test writeMany and readMany operations
TEST_F(CircularBufferTest, BulkOperations) {
    CircularBuffer<int> buffer(64);
    
    // Prepare test data
    std::vector<int> write_data = createTestData(10);
    
    // Write multiple elements
    size_t written = buffer.writeMany(write_data.data(), write_data.size());
    EXPECT_EQ(written, 10);
    EXPECT_EQ(buffer.size(), 10);
    
    // Read multiple elements
    std::vector<int> read_data(10);
    size_t read_count = buffer.readMany(read_data.data(), read_data.size());
    EXPECT_EQ(read_count, 10);
    EXPECT_EQ(buffer.size(), 0);
    
    // Verify data integrity
    EXPECT_EQ(read_data, write_data);
}

// Test partial bulk operations
TEST_F(CircularBufferTest, PartialBulkOperations) {
    CircularBuffer<int> buffer(16);
    
    // Fill buffer almost to capacity
    for (int i = 0; i < 14; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    
    // Try to write more than available space
    std::vector<int> write_data = createTestData(10);
    size_t written = buffer.writeMany(write_data.data(), write_data.size());
    EXPECT_EQ(written, 1);  // Only 1 slot available
    EXPECT_TRUE(buffer.isFull());
    
    // Read some data
    std::vector<int> read_data(5);
    size_t read_count = buffer.readMany(read_data.data(), read_data.size());
    EXPECT_EQ(read_count, 5);
    EXPECT_EQ(buffer.size(), 10);
}

// Test buffer utilization
TEST_F(CircularBufferTest, UtilizationCalculation) {
    CircularBuffer<int> buffer(64);
    
    // Empty buffer
    EXPECT_DOUBLE_EQ(buffer.getUtilization(), 0.0);
    
    // Half full buffer
    for (int i = 0; i < 32; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    EXPECT_DOUBLE_EQ(buffer.getUtilization(), 0.5);
    
    // Nearly full buffer
    for (int i = 32; i < 63; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    EXPECT_DOUBLE_EQ(buffer.getUtilization(), 63.0/64.0);
}

// Test statistics collection
TEST_F(CircularBufferTest, StatisticsCollection) {
    CircularBuffer<int> buffer(64);
    
    // Perform various operations
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    
    for (int i = 0; i < 5; ++i) {
        auto value = buffer.read();
        EXPECT_TRUE(value.has_value());
    }
    
    // Try to read from buffer with remaining elements
    for (int i = 0; i < 3; ++i) {
        auto value = buffer.read();
        EXPECT_TRUE(value.has_value());
    }
    
    // Try to read beyond available elements
    auto value = buffer.read();
    EXPECT_TRUE(value.has_value());  // Should get the last element
    
    value = buffer.read();
    EXPECT_TRUE(value.has_value());  // Should get the 10th element
    
    value = buffer.read();
    EXPECT_FALSE(value.has_value());  // Should fail
    
    // Check statistics
    const auto& stats = buffer.getStatistics();
    EXPECT_EQ(stats.getWriteCount(), 10);
    EXPECT_EQ(stats.getReadCount(), 10);
    EXPECT_EQ(stats.getWriteFailures(), 0);
    EXPECT_EQ(stats.getReadFailures(), 1);
    EXPECT_EQ(stats.getUnderruns(), 1);
}

// Test statistics reset
TEST_F(CircularBufferTest, StatisticsReset) {
    CircularBuffer<int> buffer(64);
    
    // Generate some statistics
    buffer.write(1);
    buffer.read();
    buffer.read();  // This should fail
    
    const auto& stats = buffer.getStatistics();
    EXPECT_GT(stats.getWriteCount(), 0);
    EXPECT_GT(stats.getReadFailures(), 0);
    
    // Reset statistics
    buffer.resetStatistics();
    
    EXPECT_EQ(stats.getWriteCount(), 0);
    EXPECT_EQ(stats.getReadCount(), 0);
    EXPECT_EQ(stats.getWriteFailures(), 0);
    EXPECT_EQ(stats.getReadFailures(), 0);
    EXPECT_EQ(stats.getOverruns(), 0);
    EXPECT_EQ(stats.getUnderruns(), 0);
}

// Test buffer clear operation
TEST_F(CircularBufferTest, ClearOperation) {
    CircularBuffer<int> buffer(64);
    
    // Fill buffer with data
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    EXPECT_EQ(buffer.size(), 10);
    
    // Clear buffer
    buffer.clear();
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.available(), 63);  // capacity - 1
}

// Test thread safety with single producer/consumer
TEST_F(CircularBufferTest, SingleProducerConsumerThreadSafety) {
    CircularBuffer<int> buffer(1024);
    const int NUM_ITEMS = 10000;
    
    std::atomic<int> items_written{0};
    std::atomic<int> items_read{0};
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!buffer.write(i)) {
                std::this_thread::yield();
            }
            items_written.fetch_add(1);
        }
        producer_done.store(true);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int expected_value = 0;
        while (!producer_done.load() || !buffer.isEmpty()) {
            auto value = buffer.read();
            if (value.has_value()) {
                EXPECT_EQ(value.value(), expected_value);
                expected_value++;
                items_read.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(items_written.load(), NUM_ITEMS);
    EXPECT_EQ(items_read.load(), NUM_ITEMS);
    EXPECT_TRUE(buffer.isEmpty());
    
    // Check statistics
    const auto& stats = buffer.getStatistics();
    EXPECT_EQ(stats.getWriteCount(), NUM_ITEMS);
    EXPECT_EQ(stats.getReadCount(), NUM_ITEMS);
}

// Performance test
TEST_F(CircularBufferTest, PerformanceTest) {
    CircularBuffer<int> buffer(8192);
    const int NUM_OPERATIONS = 4096;  // Use less than buffer capacity for pure performance test
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Write operations
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        ASSERT_TRUE(buffer.write(i));
    }
    
    auto mid_time = std::chrono::high_resolution_clock::now();
    
    // Read operations
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        auto value = buffer.read();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), i);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto write_duration = std::chrono::duration_cast<std::chrono::microseconds>(mid_time - start_time);
    auto read_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - mid_time);
    
    double write_ops_per_sec = NUM_OPERATIONS / (write_duration.count() / 1e6);
    double read_ops_per_sec = NUM_OPERATIONS / (read_duration.count() / 1e6);
    
    std::cout << "CircularBuffer Performance:\n";
    std::cout << "  Write operations per second: " << write_ops_per_sec << "\n";
    std::cout << "  Read operations per second: " << read_ops_per_sec << "\n";
    std::cout << "  Write time per operation: " << (write_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    std::cout << "  Read time per operation: " << (read_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    
    // Performance expectations (should be very fast)
    EXPECT_GT(write_ops_per_sec, 10000000);  // At least 10M ops/sec
    EXPECT_GT(read_ops_per_sec, 10000000);   // At least 10M ops/sec
}

// Test with complex data types
TEST_F(CircularBufferTest, ComplexDataTypes) {
    struct ComplexData {
        int id;
        std::string name;
        std::vector<double> values;
        
        bool operator==(const ComplexData& other) const {
            return id == other.id && name == other.name && values == other.values;
        }
    };
    
    CircularBuffer<ComplexData> buffer(64);
    
    ComplexData data1{1, "test1", {1.0, 2.0, 3.0}};
    ComplexData data2{2, "test2", {4.0, 5.0, 6.0}};
    
    // Write complex data
    EXPECT_TRUE(buffer.write(data1));
    EXPECT_TRUE(buffer.write(data2));
    EXPECT_EQ(buffer.size(), 2);
    
    // Read complex data
    auto read1 = buffer.read();
    ASSERT_TRUE(read1.has_value());
    EXPECT_EQ(read1.value(), data1);
    
    auto read2 = buffer.read();
    ASSERT_TRUE(read2.has_value());
    EXPECT_EQ(read2.value(), data2);
    
    EXPECT_TRUE(buffer.isEmpty());
}

// Test error handling
TEST_F(CircularBufferTest, ErrorHandling) {
    CircularBuffer<int> buffer(16);
    
    // Test writeMany with null pointer
    size_t written = buffer.writeMany(nullptr, 10);
    EXPECT_EQ(written, 0);
    
    // Test readMany with null pointer
    size_t read_count = buffer.readMany(nullptr, 10);
    EXPECT_EQ(read_count, 0);
    
    // Test writeMany with zero count
    std::vector<int> data = {1, 2, 3};
    written = buffer.writeMany(data.data(), 0);
    EXPECT_EQ(written, 0);
    
    // Test readMany with zero count
    read_count = buffer.readMany(data.data(), 0);
    EXPECT_EQ(read_count, 0);
} 