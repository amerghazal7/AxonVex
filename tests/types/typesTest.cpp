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

// Test RingBuffer functionality
TEST_F(TypesModuleTest, RingBufferOperations) {
    RingBuffer<int> buffer(5);
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.capacity(), 5);
    
    // Fill buffer
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(buffer.write(i));
    }
    
    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.write(6));  // Should fail when full
    
    // Read from buffer
    int value;
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(buffer.read(value));
        EXPECT_EQ(value, i);
    }
    
    EXPECT_TRUE(buffer.empty());
    
    // Test statistics
    EXPECT_EQ(buffer.totalWrites(), 5);
    EXPECT_EQ(buffer.totalReads(), 5);
    EXPECT_EQ(buffer.overruns(), 1);  // One failed write
}

// Test BitSet functionality
TEST_F(TypesModuleTest, BitSetOperations) {
    BitSet bitset(64);
    
    EXPECT_EQ(bitset.size(), 64);
    EXPECT_TRUE(bitset.none());
    
    // Set some bits
    bitset.set(0);
    bitset.set(5);
    bitset.set(63);
    
    EXPECT_TRUE(bitset.get(0));
    EXPECT_TRUE(bitset.get(5));
    EXPECT_TRUE(bitset.get(63));
    EXPECT_FALSE(bitset.get(1));
    
    EXPECT_EQ(bitset.count(), 3);
    EXPECT_TRUE(bitset.any());
    EXPECT_FALSE(bitset.all());
    
    // Test logical operations
    BitSet other(64);
    other.set(0);
    other.set(10);
    
    auto intersection = bitset & other;
    EXPECT_EQ(intersection.count(), 1);  // Only bit 0 is common
    EXPECT_TRUE(intersection.get(0));
    
    auto union_set = bitset | other;
    EXPECT_EQ(union_set.count(), 4);  // 0, 5, 10, 63
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
    
    // Elements should come out in priority order (highest first)
    int value;
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 10);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 5);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 3);
    
    EXPECT_TRUE(pqueue.pop(value));
    EXPECT_EQ(value, 1);
    
    EXPECT_TRUE(pqueue.empty());
}

// Test ObjectPool functionality
TEST_F(TypesModuleTest, ObjectPoolOperations) {
    ObjectPool<std::vector<int>> pool(2, 10);  // Initial size 2, max 10
    
    EXPECT_EQ(pool.available(), 2);
    EXPECT_EQ(pool.inUse(), 0);
    
    {
        auto obj1 = pool.acquire();
        auto obj2 = pool.acquire();
        
        EXPECT_EQ(pool.available(), 0);
        EXPECT_EQ(pool.inUse(), 2);
        
        // Use the objects
        obj1->push_back(42);
        obj2->push_back(24);
        
        EXPECT_EQ(obj1->size(), 1);
        EXPECT_EQ(obj2->size(), 1);
    }  // Objects returned to pool here
    
    EXPECT_EQ(pool.available(), 2);
    EXPECT_EQ(pool.inUse(), 0);
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

// Test thread safety of collections
TEST_F(TypesModuleTest, ThreadSafetyTest) {
    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 1000;
    
    RingBuffer<int> buffer(NUM_THREADS * ITEMS_PER_THREAD);
    PriorityQueue<int> pqueue;
    
    // Writer threads
    std::vector<std::thread> writers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        writers.emplace_back([&buffer, &pqueue, t, ITEMS_PER_THREAD]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
                int value = t * ITEMS_PER_THREAD + i;
                buffer.write(value);
                pqueue.push(value);
            }
        });
    }
    
    // Wait for writers to complete
    for (auto& writer : writers) {
        writer.join();
    }
    
    // Verify results
    EXPECT_EQ(buffer.size(), NUM_THREADS * ITEMS_PER_THREAD);
    EXPECT_EQ(pqueue.size(), NUM_THREADS * ITEMS_PER_THREAD);
    
    // Read from buffer and verify no data loss
    int buffer_count = 0;
    int value;
    while (buffer.read(value)) {
        buffer_count++;
    }
    EXPECT_EQ(buffer_count, NUM_THREADS * ITEMS_PER_THREAD);
    
    // Read from priority queue and verify ordering
    int pqueue_count = 0;
    int last_value = INT_MAX;
    while (pqueue.pop(value)) {
        EXPECT_LE(value, last_value);  // Should be in descending order
        last_value = value;
        pqueue_count++;
    }
    EXPECT_EQ(pqueue_count, NUM_THREADS * ITEMS_PER_THREAD);
}