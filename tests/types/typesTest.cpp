#include <gtest/gtest.h>
#include <axonvex/types/types.hpp>
#include <thread>
#include <vector>
#include <chrono>
#include <climits>  // For INT_MAX

using namespace axonvex::types;

class TypesModuleTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test UUID functionality
TEST_F(TypesModuleTest, UUIDGeneration) {
    auto uuid1 = UUID::generate();
    auto uuid2 = UUID::generate();
    
    EXPECT_NE(uuid1, uuid2);
    EXPECT_FALSE(uuid1.isNull());
    EXPECT_FALSE(uuid2.isNull());
    
    std::string uuid1_str = uuid1.toString();
    EXPECT_EQ(uuid1_str.length(), 36);  // Standard UUID string format
    
    UUID uuid3(uuid1_str);
    EXPECT_EQ(uuid1, uuid3);
}

// Test TypedID functionality
TEST_F(TypesModuleTest, TypedIDFunctionality) {
    ProcessingUnitID id1(123);
    ProcessingUnitID id2(456);
    ProcessingUnitID id3(123);
    
    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1, id3);
    EXPECT_TRUE(id1.isValid());
    EXPECT_FALSE(ProcessingUnitID::invalid().isValid());
}

// Test PrecisionFloat functionality
TEST_F(TypesModuleTest, PrecisionFloatOperations) {
    PrecisionFloat a(1.0, 1e-10);
    PrecisionFloat b(2.0, 1e-10);
    
    auto sum = a + b;
    EXPECT_TRUE(sum.equals(3.0));
    
    auto product = a * b;
    EXPECT_TRUE(product.equals(2.0));
    
    // Test precision comparison
    PrecisionFloat c(1.0000000001, 1e-8);
    EXPECT_TRUE(a.equals(c));  // Should be equal within epsilon
}

// Test RingBuffer functionality (now using CircularBuffer)
TEST_F(TypesModuleTest, RingBufferOperations) {
    collections::RingBuffer<int> buffer(8);  // Now uses CircularBuffer with power-of-2 optimization
    
    EXPECT_TRUE(buffer.isEmpty());
    // CircularBuffer optimizes capacity to next power of 2, so 8 becomes 16
    EXPECT_GE(buffer.capacity(), 8);  // Should be >= requested capacity
    
    // Fill buffer partially (leave room for the optimization)
    size_t actual_capacity = buffer.capacity();
    size_t fill_count = std::min(size_t(7), actual_capacity - 1);
    
    for (size_t i = 1; i <= fill_count; ++i) {
        EXPECT_TRUE(buffer.write(static_cast<int>(i)));
    }
    
    EXPECT_FALSE(buffer.isEmpty());
    
    // Read from buffer using actual CircularBuffer API
    for (size_t i = 1; i <= fill_count; ++i) {
        auto value_opt = buffer.read();  // Returns std::optional<T>
        EXPECT_TRUE(value_opt.has_value());
        EXPECT_EQ(value_opt.value(), static_cast<int>(i));
    }
    
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.size(), 0);
}

// Test BitSet functionality - DISABLED until BitSet is implemented
TEST_F(TypesModuleTest, DISABLED_BitSetOperations) {
    // BitSet not implemented yet
}

// Test PriorityQueue functionality
TEST_F(TypesModuleTest, PriorityQueueOperations) {
    PriorityQueue<int> pqueue;
    
    EXPECT_TRUE(pqueue.empty());
    
    // Add elements
    pqueue.push(5);
    pqueue.push(1);
    pqueue.push(10);
    pqueue.push(3);
    
    EXPECT_EQ(pqueue.size(), 4);
    
    // Elements should come out in priority order (smallest first by default)
    int value;
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 3);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 5);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 10);
    
    EXPECT_TRUE(pqueue.empty());
}

// Test ObjectPool functionality (now using MemoryPool)
TEST_F(TypesModuleTest, ObjectPoolOperations) {
    collections::ObjectPool<std::vector<int>> pool(2);  // Pool size 2, but optimized to power-of-2
    
    // MemoryPool optimizes capacity to next power of 2 and has minimum size
    size_t actual_capacity = pool.getAvailable();
    EXPECT_GE(actual_capacity, 2);  // Should be >= requested capacity
    EXPECT_EQ(pool.getUsage(), 0);
    
    {
        // Use unified MemoryPool API: allocate() and deallocate()
        auto obj1 = pool.allocate();
        auto obj2 = pool.allocate();
        
        EXPECT_NE(obj1, nullptr);
        EXPECT_NE(obj2, nullptr);
        EXPECT_EQ(pool.getAvailable(), actual_capacity - 2);
        EXPECT_EQ(pool.getUsage(), 2);
        
        // Use the objects (they are default-constructed)
        obj1->push_back(42);
        obj2->push_back(24);
        
        EXPECT_EQ(obj1->size(), 1);
        EXPECT_EQ(obj2->size(), 1);
        
        // Return objects to pool using unified API
        EXPECT_TRUE(pool.deallocate(obj1));
        EXPECT_TRUE(pool.deallocate(obj2));
    }
    
    EXPECT_EQ(pool.getAvailable(), actual_capacity);
    EXPECT_EQ(pool.getUsage(), 0);
}

// Test Atomic functionality
TEST_F(TypesModuleTest, AtomicOperations) {
    Atomic<int> atomic_int(0);
    
    EXPECT_EQ(atomic_int.load(), 0);
    
    atomic_int.store(42);
    EXPECT_EQ(atomic_int.load(), 42);
    
    EXPECT_EQ(atomic_int.fetch_add(8), 42);
    EXPECT_EQ(atomic_int.load(), 50);
    
    EXPECT_EQ(atomic_int++, 50);
    EXPECT_EQ(atomic_int.load(), 51);
    
    int expected = 51;
    EXPECT_TRUE(atomic_int.compare_exchange_strong(expected, 100));
    EXPECT_EQ(atomic_int.load(), 100);
}

// Test thread safety of collections - UPDATED VERSION
TEST_F(TypesModuleTest, ThreadSafetyTest) {
    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 64;
    
    collections::RingBuffer<int> buffer(512);  // Now uses CircularBuffer with dynamic capacity
    
    // Test thread-safe CircularBuffer
    std::vector<std::thread> writers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        writers.emplace_back([&buffer, t, ITEMS_PER_THREAD]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
                int value = t * ITEMS_PER_THREAD + i;
                // Keep trying until we can write (buffer might be full temporarily)
                while (!buffer.write(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for writers to complete
    for (auto& writer : writers) {
        writer.join();
    }
    
    // Verify results
    EXPECT_EQ(buffer.size(), NUM_THREADS * ITEMS_PER_THREAD);
    
    // Read from buffer and verify no data loss
    int buffer_count = 0;
    while (auto value_opt = buffer.read()) {  // Use actual CircularBuffer API: std::optional<T> read()
        buffer_count++;
    }
    EXPECT_EQ(buffer_count, NUM_THREADS * ITEMS_PER_THREAD);
    
    // Test PriorityQueue separately (single-threaded)
    collections::PriorityQueue<int> pqueue;
    
    // Add items in single thread
    for (int t = 0; t < NUM_THREADS; ++t) {
        for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
            int value = t * ITEMS_PER_THREAD + i;
            pqueue.push(value);
        }
    }
    
    EXPECT_EQ(pqueue.size(), NUM_THREADS * ITEMS_PER_THREAD);
    
    // Read from priority queue and verify ordering (single-threaded)
    int pqueue_count = 0;
    int last_value = -1;
    int value;  // Declare value variable for the pop operation
    while (pqueue.pop(value)) {
        EXPECT_GE(value, last_value);  // Should be in ascending order (min-heap)
        last_value = value;
        pqueue_count++;
    }
    EXPECT_EQ(pqueue_count, NUM_THREADS * ITEMS_PER_THREAD);
}