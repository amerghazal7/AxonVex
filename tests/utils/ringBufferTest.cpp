#include <atomic>
#include <axonvex_core/utils/utils.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using axonvex::utils::containers::RingBuffer;

TEST(RingBufferTest, PushPopBasic) {
    RingBuffer<int> rb(16);
    EXPECT_TRUE(rb.write(1));
    EXPECT_TRUE(rb.write(2));
    EXPECT_TRUE(rb.write(3));
    EXPECT_TRUE(rb.write(4));

    auto a = rb.read();
    auto b = rb.read();
    auto c = rb.read();
    auto d = rb.read();
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(b.has_value());
    EXPECT_TRUE(c.has_value());
    EXPECT_TRUE(d.has_value());
    EXPECT_EQ(a.value(), 1);
    EXPECT_EQ(b.value(), 2);
    EXPECT_EQ(c.value(), 3);
    EXPECT_EQ(d.value(), 4);
    EXPECT_TRUE(rb.isEmpty());
}

namespace {
struct LifetimeCounter {
    static std::atomic<int> constructions;
    static std::atomic<int> destructions;
    LifetimeCounter() {
        constructions.fetch_add(1);
    }
    LifetimeCounter(const LifetimeCounter&) {
        constructions.fetch_add(1);
    }
    LifetimeCounter& operator=(const LifetimeCounter&) = default;
    ~LifetimeCounter() {
        destructions.fetch_add(1);
    }
};
std::atomic<int> LifetimeCounter::constructions{0};
std::atomic<int> LifetimeCounter::destructions{0};
} // namespace

// C21 regression: the ctor placement-new'd over elements make_unique<T[]> had
// already value-initialized -- every slot was constructed twice and destroyed
// once, leaking the first object's resources for non-trivial T. RingBuffer's
// MIN_CAPACITY is 16, so that is the smallest capacity this test can request.
TEST(RingBufferTest, ConstructionAndDestructionAreBalanced) {
    LifetimeCounter::constructions.store(0);
    LifetimeCounter::destructions.store(0);
    { RingBuffer<LifetimeCounter> buffer(RingBuffer<LifetimeCounter>::MIN_CAPACITY); }
    EXPECT_EQ(LifetimeCounter::constructions.load(), LifetimeCounter::destructions.load());
}

// C21 regression: RingBuffer is documented as SPSC lock-free (project rule 8
// requires a TSan-clean stress test backing that claim). One producer writes
// a strictly increasing sequence, one consumer reads and checks both value
// integrity and FIFO order; deadline-guarded rather than a bare sleep so a
// reintroduced correctness bug fails fast instead of hanging the suite.
TEST(RingBufferTest, SpscStressPreservesValueIntegrityAndOrder) {
    RingBuffer<uint64_t> rb(1024);
    constexpr uint64_t kItems = 200000;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> mismatches{0};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < kItems; ++i) {
            while (!rb.write(i)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return;
                }
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&]() {
        uint64_t expected = 0;
        while (expected < kItems) {
            auto item = rb.read();
            if (!item.has_value()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return;
                }
                std::this_thread::yield();
                continue;
            }
            if (item.value() != expected) {
                mismatches.fetch_add(1, std::memory_order_relaxed);
            }
            ++expected;
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), kItems) << "producer hit the deadline before finishing";
    EXPECT_EQ(consumed.load(), kItems) << "consumer hit the deadline before finishing";
    EXPECT_EQ(mismatches.load(), 0u) << "SPSC ring buffer delivered an out-of-order/corrupt value";
}
