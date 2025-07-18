/**
 * @file memoryPoolTest.cpp
 * @brief Unit tests for MemoryPool class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/core/memoryPool.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <string>
#include <algorithm>

using namespace axonvex::core;

class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }
    
    // Test structures
    struct TestObject {
        int value;
        double data;
        std::string name;
        
        TestObject() : value(0), data(0.0), name("default") {}
        TestObject(int v, double d, const std::string& n) : value(v), data(d), name(n) {}
        
        bool operator==(const TestObject& other) const {
            return value == other.value && data == other.data && name == other.name;
        }
    };
    
    // Simple test structure for performance testing
    struct SimpleStruct {
        int id;
        double value;
        
        SimpleStruct() : id(0), value(0.0) {}
        SimpleStruct(int i, double v) : id(i), value(v) {}
    };
};

// Test basic pool construction
TEST_F(MemoryPoolTest, Construction) {
    MemoryPool<int> pool;
    EXPECT_EQ(pool.getCapacity(), 1024);  // Default capacity
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 1024);
    EXPECT_TRUE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 0.0);
}

// Test custom capacity construction
TEST_F(MemoryPoolTest, CustomCapacityConstruction) {
    MemoryPool<int> pool(512);
    EXPECT_EQ(pool.getCapacity(), 512);
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 512);
    EXPECT_TRUE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
    
    // Test power-of-2 rounding
    MemoryPool<int> pool2(500);
    EXPECT_EQ(pool2.getCapacity(), 512);  // Rounded up to next power of 2
    
    // Test minimum capacity
    MemoryPool<int> pool3(8);
    EXPECT_EQ(pool3.getCapacity(), 16);  // Minimum capacity
}

// Test basic allocation and deallocation
TEST_F(MemoryPoolTest, BasicAllocationDeallocation) {
    MemoryPool<int> pool(64);
    
    // Allocate single object
    int* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(pool.getUsage(), 1);
    EXPECT_EQ(pool.getAvailable(), 63);
    EXPECT_FALSE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
    EXPECT_GT(pool.getUtilization(), 0.0);
    
    // Deallocate single object
    EXPECT_TRUE(pool.deallocate(ptr));
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 64);
    EXPECT_TRUE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 0.0);
}

// Test object allocation with constructors
TEST_F(MemoryPoolTest, ObjectAllocation) {
    MemoryPool<TestObject> pool(64);
    
    // Allocate with default constructor
    TestObject* obj1 = pool.allocateObject();
    ASSERT_NE(obj1, nullptr);
    EXPECT_EQ(obj1->value, 0);
    EXPECT_EQ(obj1->name, "default");
    
    // Allocate with copy constructor
    TestObject test_obj(42, 3.14, "test");
    TestObject* obj2 = pool.allocateObject(test_obj);
    ASSERT_NE(obj2, nullptr);
    EXPECT_EQ(obj2->value, 42);
    EXPECT_DOUBLE_EQ(obj2->data, 3.14);
    EXPECT_EQ(obj2->name, "test");
    
    // Allocate with move constructor
    TestObject move_obj(99, 2.71, "move");
    TestObject* obj3 = pool.allocateObject(std::move(move_obj));
    ASSERT_NE(obj3, nullptr);
    EXPECT_EQ(obj3->value, 99);
    EXPECT_DOUBLE_EQ(obj3->data, 2.71);
    EXPECT_EQ(obj3->name, "move");
    
    // Deallocate objects
    EXPECT_TRUE(pool.deallocateObject(obj1));
    EXPECT_TRUE(pool.deallocateObject(obj2));
    EXPECT_TRUE(pool.deallocateObject(obj3));
    
    EXPECT_EQ(pool.getUsage(), 0);
}

// Test pool exhaustion
TEST_F(MemoryPoolTest, PoolExhaustion) {
    MemoryPool<int> pool(16);
    std::vector<int*> ptrs;
    
    // Allocate all objects
    for (int i = 0; i < 16; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    EXPECT_EQ(pool.getUsage(), 16);
    EXPECT_EQ(pool.getAvailable(), 0);
    EXPECT_FALSE(pool.isFull());
    EXPECT_TRUE(pool.isEmpty());
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 1.0);
    
    // Try to allocate more (should fail)
    int* ptr = pool.allocate();
    EXPECT_EQ(ptr, nullptr);
    
    // Check statistics
    const auto& stats = pool.getStatistics();
    EXPECT_EQ(stats.getAllocations(), 16);
    EXPECT_EQ(stats.getAllocationFailures(), 1);
    EXPECT_EQ(stats.getPoolExhausted(), 1);
    
    // Deallocate all objects
    for (int* p : ptrs) {
        EXPECT_TRUE(pool.deallocate(p));
    }
    
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 16);
    EXPECT_TRUE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
}

// Test invalid pointer deallocation
TEST_F(MemoryPoolTest, InvalidPointerDeallocation) {
    MemoryPool<int> pool(64);
    
    // Try to deallocate null pointer
    EXPECT_FALSE(pool.deallocate(nullptr));
    
    // Try to deallocate invalid pointer
    int external_var = 42;
    EXPECT_FALSE(pool.deallocate(&external_var));
    
    // Try to deallocate same pointer twice
    int* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(pool.deallocate(ptr));
    EXPECT_FALSE(pool.deallocate(ptr));  // Should fail second time
    
    // Check statistics
    const auto& stats = pool.getStatistics();
    EXPECT_EQ(stats.getDeallocationFailures(), 3);
}

// Test pool validation
TEST_F(MemoryPoolTest, PoolValidation) {
    MemoryPool<int> pool(64);
    
    // Empty pool should be valid
    EXPECT_TRUE(pool.validate());
    
    // Allocate some objects
    std::vector<int*> ptrs;
    for (int i = 0; i < 10; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    // Pool should still be valid
    EXPECT_TRUE(pool.validate());
    
    // Deallocate some objects
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(pool.deallocate(ptrs[i]));
    }
    
    // Pool should still be valid
    EXPECT_TRUE(pool.validate());
    
    // Deallocate remaining objects
    for (int i = 5; i < 10; ++i) {
        EXPECT_TRUE(pool.deallocate(ptrs[i]));
    }
    
    // Empty pool should be valid
    EXPECT_TRUE(pool.validate());
}

// Test statistics collection
TEST_F(MemoryPoolTest, StatisticsCollection) {
    MemoryPool<int> pool(64);
    
    // Perform various operations
    std::vector<int*> ptrs;
    for (int i = 0; i < 20; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    // Deallocate some
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(pool.deallocate(ptrs[i]));
    }
    
    // Try to allocate beyond capacity
    std::vector<int*> more_ptrs;
    for (int i = 0; i < 60; ++i) {
        int* ptr = pool.allocate();
        if (ptr != nullptr) {
            more_ptrs.push_back(ptr);
        }
    }
    
    // Check statistics
    const auto& stats = pool.getStatistics();
    EXPECT_GT(stats.getAllocations(), 0);
    EXPECT_EQ(stats.getDeallocations(), 10);
    EXPECT_GE(stats.getAllocationFailures(), 0);  // May have failures if pool is full
    EXPECT_EQ(stats.getDeallocationFailures(), 0);
    EXPECT_GT(stats.getPeakUsage(), 0);
    EXPECT_GT(stats.getCurrentUsage(), 0);
    
    // Clean up
    for (int i = 10; i < 20; ++i) {
        pool.deallocate(ptrs[i]);
    }
    for (int* ptr : more_ptrs) {
        pool.deallocate(ptr);
    }
}

// Test statistics reset
TEST_F(MemoryPoolTest, StatisticsReset) {
    MemoryPool<int> pool(64);
    
    // Generate some statistics
    int* ptr1 = pool.allocate();
    int* ptr2 = pool.allocate();
    pool.deallocate(ptr1);
    pool.deallocate(nullptr);  // This should fail
    
    const auto& stats = pool.getStatistics();
    EXPECT_GT(stats.getAllocations(), 0);
    EXPECT_GT(stats.getDeallocations(), 0);
    EXPECT_GT(stats.getDeallocationFailures(), 0);
    
    // Reset statistics
    pool.resetStatistics();
    
    EXPECT_EQ(stats.getAllocations(), 0);
    EXPECT_EQ(stats.getDeallocations(), 0);
    EXPECT_EQ(stats.getAllocationFailures(), 0);
    EXPECT_EQ(stats.getDeallocationFailures(), 0);
    
    // Clean up
    pool.deallocate(ptr2);
}

// Test clear operation
TEST_F(MemoryPoolTest, ClearOperation) {
    MemoryPool<int> pool(64);
    
    // Allocate some objects
    std::vector<int*> ptrs;
    for (int i = 0; i < 20; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    EXPECT_EQ(pool.getUsage(), 20);
    EXPECT_EQ(pool.getAvailable(), 44);
    
    // Clear pool
    pool.clear();
    
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 64);
    EXPECT_TRUE(pool.isFull());
    EXPECT_FALSE(pool.isEmpty());
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 0.0);
}

// Test thread safety
TEST_F(MemoryPoolTest, ThreadSafety) {
    MemoryPool<SimpleStruct> pool(2048);
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 500;
    
    std::atomic<int> successful_allocations{0};
    std::atomic<int> successful_deallocations{0};
    std::atomic<bool> start_flag{false};
    
    std::vector<std::thread> threads;
    
    // Create threads
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            // Wait for start signal
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            
            std::vector<SimpleStruct*> local_ptrs;
            
            // Allocate objects
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                SimpleStruct obj(t * 1000 + i, 3.14 * (t * 1000 + i));
                SimpleStruct* ptr = pool.allocateObject(obj);
                if (ptr != nullptr) {
                    local_ptrs.push_back(ptr);
                    successful_allocations.fetch_add(1);
                }
            }
            
            // Deallocate objects
            for (SimpleStruct* ptr : local_ptrs) {
                if (pool.deallocateObject(ptr)) {
                    successful_deallocations.fetch_add(1);
                }
            }
        });
    }
    
    // Start all threads
    start_flag.store(true);
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Check results
    EXPECT_EQ(successful_allocations.load(), successful_deallocations.load());
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_TRUE(pool.validate());
    
    // Check statistics
    const auto& stats = pool.getStatistics();
    EXPECT_EQ(stats.getAllocations(), stats.getDeallocations());
    EXPECT_EQ(stats.getAllocationFailures(), 0);
    EXPECT_EQ(stats.getDeallocationFailures(), 0);
}

// Test performance
TEST_F(MemoryPoolTest, PerformanceTest) {
    MemoryPool<SimpleStruct> pool(100000);  // Large pool
    const int NUM_OPERATIONS = 50000;
    
    // Measure allocation performance
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<SimpleStruct*> ptrs;
    ptrs.reserve(NUM_OPERATIONS);
    
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        SimpleStruct obj(i, 3.14 * i);
        SimpleStruct* ptr = pool.allocateObject(obj);
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    auto mid_time = std::chrono::high_resolution_clock::now();
    
    // Measure deallocation performance
    for (SimpleStruct* ptr : ptrs) {
        ASSERT_TRUE(pool.deallocateObject(ptr));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto allocation_duration = std::chrono::duration_cast<std::chrono::microseconds>(mid_time - start_time);
    auto deallocation_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - mid_time);
    
    double allocation_ops_per_sec = NUM_OPERATIONS / (allocation_duration.count() / 1e6);
    double deallocation_ops_per_sec = NUM_OPERATIONS / (deallocation_duration.count() / 1e6);
    
    std::cout << "MemoryPool Performance:\n";
    std::cout << "  Allocation operations per second: " << allocation_ops_per_sec << "\n";
    std::cout << "  Deallocation operations per second: " << deallocation_ops_per_sec << "\n";
    std::cout << "  Allocation time per operation: " << (allocation_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    std::cout << "  Deallocation time per operation: " << (deallocation_duration.count() / static_cast<double>(NUM_OPERATIONS)) << " μs\n";
    
    // Performance expectations (targeting <20ns per operation = 50M+ ops/sec)
    EXPECT_GT(allocation_ops_per_sec, 10000000);   // At least 10M ops/sec
    EXPECT_GT(deallocation_ops_per_sec, 10000000); // At least 10M ops/sec
    
    // Check statistics
    const auto& stats = pool.getStatistics();
    EXPECT_EQ(stats.getAllocations(), NUM_OPERATIONS);
    EXPECT_EQ(stats.getDeallocations(), NUM_OPERATIONS);
    EXPECT_EQ(stats.getAllocationFailures(), 0);
    EXPECT_EQ(stats.getDeallocationFailures(), 0);
}

// Test complex data types
TEST_F(MemoryPoolTest, ComplexDataTypes) {
    MemoryPool<TestObject> pool(64);
    
    // Allocate complex objects
    TestObject test1(42, 3.14, "test1");
    TestObject test2(99, 2.71, "test2");
    TestObject* obj1 = pool.allocateObject(test1);
    TestObject* obj2 = pool.allocateObject(test2);
    
    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    
    // Verify object content
    EXPECT_EQ(obj1->value, 42);
    EXPECT_DOUBLE_EQ(obj1->data, 3.14);
    EXPECT_EQ(obj1->name, "test1");
    
    EXPECT_EQ(obj2->value, 99);
    EXPECT_DOUBLE_EQ(obj2->data, 2.71);
    EXPECT_EQ(obj2->name, "test2");
    
    // Deallocate objects
    EXPECT_TRUE(pool.deallocateObject(obj1));
    EXPECT_TRUE(pool.deallocateObject(obj2));
    
    EXPECT_EQ(pool.getUsage(), 0);
}

// Test pool utilization calculations
TEST_F(MemoryPoolTest, UtilizationCalculations) {
    MemoryPool<int> pool(100);
    
    // Pool capacity is rounded to next power of 2 (128)
    size_t actual_capacity = pool.getCapacity();
    EXPECT_EQ(actual_capacity, 128);
    
    // Initially empty
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 0.0);
    
    // Allocate 25 objects
    std::vector<int*> ptrs;
    for (int i = 0; i < 25; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 25.0 / actual_capacity);
    
    // Allocate 50 more objects
    for (int i = 0; i < 50; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }
    
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 75.0 / actual_capacity);
    
    // Deallocate 25 objects
    for (int i = 0; i < 25; ++i) {
        EXPECT_TRUE(pool.deallocate(ptrs[i]));
    }
    
    EXPECT_DOUBLE_EQ(pool.getUtilization(), 50.0 / actual_capacity);
    
    // Clean up
    for (int i = 25; i < 75; ++i) {
        pool.deallocate(ptrs[i]);
    }
}

// Test error handling
TEST_F(MemoryPoolTest, ErrorHandling) {
    MemoryPool<int> pool(16);
    
    // Test multiple invalid deallocations
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(pool.deallocate(nullptr));
    }
    
    // Test allocation/deallocation cycle
    int* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(pool.deallocate(ptr));
    
    // Test double deallocation
    EXPECT_FALSE(pool.deallocate(ptr));
    
    // Test pool state after operations
    EXPECT_EQ(pool.getUsage(), 0);
    EXPECT_EQ(pool.getAvailable(), 16);
    EXPECT_TRUE(pool.validate());
    
    // Check error statistics
    const auto& stats = pool.getStatistics();
    EXPECT_EQ(stats.getDeallocationFailures(), 6);  // 5 null + 1 double deallocation
} 