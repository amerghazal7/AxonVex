#include <atomic>
#include <axonvex_core/utils/containers/threadSafeQueue.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using axonvex::utils::containers::ThreadSafeQueue;

/**
 * Soak / scalability gate: many enqueue/dequeue cycles must complete within a
 * generous wall-clock bound (CI-friendly; not a micro-benchmark).
 */
TEST(SoakScalabilityTest, ThreadSafeQueueHighVolumeWithinTimeBudget) {
    constexpr int kIterations = 200000;
    constexpr int kMaxSeconds = 120;

    ThreadSafeQueue<int> queue(4096);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        ASSERT_TRUE(queue.enqueue(i));
        auto v = queue.dequeue();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }

    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
    EXPECT_LT(elapsed.count(), kMaxSeconds);
}

TEST(SoakScalabilityTest, ThreadSafeQueueMultiProducerConsumerRoughBalance) {
    constexpr int kPerThread = 50000;
    // Must hold bursts while the main thread drains; two producers may push up to 2*kPerThread
    // items before all are consumed.
    ThreadSafeQueue<int> queue(262144);

    std::atomic<int> sum{0};

    auto worker = [&]() {
        for (int i = 0; i < kPerThread; ++i) {
            ASSERT_TRUE(queue.enqueue(1));
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);

    int drained = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (drained < 2 * kPerThread) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "Drain stalled (possible lost items or deadlock)";
        auto v = queue.tryDequeue(std::chrono::milliseconds(50));
        if (v.has_value()) {
            sum += *v;
            ++drained;
        }
    }

    t1.join();
    t2.join();

    EXPECT_EQ(sum.load(), 2 * kPerThread);
}
