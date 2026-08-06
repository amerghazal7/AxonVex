// TelemetryBus contract tests (Phase 2, plan §8): bounded non-blocking publish,
// per-channel ordering + seq, egress-thread-only dispatch, framed JSON envelope,
// stop-drains, self-stop from a callback, multi-producer stress (TSan target).
#include <atomic>
#include <axonvex_visualization/telemetryBus.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>

using axonvex::visualization::TelemetryBus;
using axonvex::visualization::TelemetryFrame;

namespace {

// Poll an atomic-backed predicate against a steady_clock deadline (project rule:
// no sleep-based synchronization, no condition_variable::wait_for under TSan).
bool pollUntil(const std::function<bool()>& pred,
               std::chrono::milliseconds limit = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

class CollectingSub : public TelemetryBus::Subscriber {
  public:
    void callbackPerform(const TelemetryFrame frame) override {
        {
            std::lock_guard<std::mutex> g(mutex_);
            frames_.push_back(frame);
            threadIds_.insert(std::this_thread::get_id());
        }
        count_.fetch_add(1, std::memory_order_release);
    }

    size_t count() const {
        return count_.load(std::memory_order_acquire);
    }
    std::vector<TelemetryFrame> frames() const {
        std::lock_guard<std::mutex> g(mutex_);
        return frames_;
    }
    std::set<std::thread::id> threadIds() const {
        std::lock_guard<std::mutex> g(mutex_);
        return threadIds_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<TelemetryFrame> frames_;
    std::set<std::thread::id> threadIds_;
    std::atomic<size_t> count_{0};
};

TelemetryBus::Message bytes(std::initializer_list<uint8_t> b) {
    return TelemetryBus::Message(b);
}

// The lifecycle deadlock regressions these tests guard against fail as a HANG
// (join/destructor never returns) and gtest has no per-test timeout: without
// this, a regression wedges the whole suite instead of failing it. Aborting is
// loud and attributable. Yield-spin, not condition_variable::wait_for (bogus
// TSan races on GCC 11 libtsan) and not sleep (project rule).
class TestWatchdog {
  public:
    explicit TestWatchdog(std::chrono::seconds limit)
        : thread_([this, limit] {
              const auto deadline = std::chrono::steady_clock::now() + limit;
              while (!done_.load(std::memory_order_acquire)) {
                  if (std::chrono::steady_clock::now() >= deadline) {
                      std::fprintf(stderr, "TestWatchdog: lifecycle test wedged (deadlock "
                                           "regression?), aborting\n");
                      std::abort();
                  }
                  std::this_thread::yield();
              }
          }) {}
    ~TestWatchdog() {
        done_.store(true, std::memory_order_release);
        thread_.join();
    }

  private:
    std::atomic<bool> done_{false};
    std::thread thread_;
};

} // namespace

TEST(TelemetryBusTest, PublishRefusedWhenNotRunning) {
    TelemetryBus bus;
    EXPECT_FALSE(bus.publish("ch", bytes({1})));
    bus.start();
    EXPECT_TRUE(bus.publish("ch", bytes({1})));
    bus.stop();
    EXPECT_FALSE(bus.publish("ch", bytes({1})));
}

TEST(TelemetryBusTest, DeliversFrameWithEnvelopeContract) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("sensors/temp", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("sensors/temp", bytes({1, 2, 3})));
    ASSERT_TRUE(pollUntil([&] { return sub.count() == 1; }));
    bus.stop();

    const auto frames = sub.frames();
    ASSERT_EQ(frames.size(), 1u);
    const TelemetryFrame& f = frames[0];
    EXPECT_EQ(f.channel, "sensors/temp");
    EXPECT_EQ(f.seq, 0u);
    EXPECT_GT(f.timestampNs, 0u);
    EXPECT_EQ(f.payload, bytes({1, 2, 3}));

    // Self-describing JSON envelope: {channel, seq, timestamp, payload}.
    const nlohmann::json j = nlohmann::json::parse(f.json);
    EXPECT_EQ(j.at("channel").get<std::string>(), "sensors/temp");
    EXPECT_EQ(j.at("seq").get<uint64_t>(), 0u);
    EXPECT_EQ(j.at("timestamp").get<uint64_t>(), f.timestampNs);
    EXPECT_EQ(j.at("payload").get<std::vector<uint8_t>>(), f.payload);
}

TEST(TelemetryBusTest, AllCallbacksRunOnTheSingleEgressThread) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("*", &sub);
    bus.start();
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(bus.publish(i % 2 ? "a" : "b", bytes({static_cast<uint8_t>(i)})));
    }
    ASSERT_TRUE(pollUntil([&] { return sub.count() == 20; }));
    bus.stop();

    const auto ids = sub.threadIds();
    ASSERT_EQ(ids.size(), 1u) << "callbacks must be dispatched from exactly one egress thread";
    EXPECT_NE(*ids.begin(), std::this_thread::get_id());
}

TEST(TelemetryBusTest, PerChannelSeqIsMonotonicAndOrderPreserved) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("ch", &sub);
    bus.start();
    const int kFrames = 200;
    int accepted = 0;
    for (int i = 0; i < kFrames; ++i) {
        if (bus.publish("ch", bytes({static_cast<uint8_t>(i & 0xff)})))
            ++accepted;
    }
    ASSERT_TRUE(pollUntil([&] { return sub.count() == static_cast<size_t>(accepted); }));
    bus.stop();

    const auto frames = sub.frames();
    ASSERT_EQ(frames.size(), static_cast<size_t>(accepted));
    for (size_t i = 0; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].seq, i) << "per-channel seq must be gapless and monotonic";
        EXPECT_EQ(frames[i].payload[0], static_cast<uint8_t>(i & 0xff))
            << "single-producer per-channel publish order must be preserved";
    }
}

TEST(TelemetryBusTest, WildcardSubscriberReceivesAllChannels) {
    TelemetryBus bus;
    CollectingSub wildcard;
    CollectingSub only_a;
    bus.subscribe("*", &wildcard);
    bus.subscribe("a", &only_a);
    bus.start();
    ASSERT_TRUE(bus.publish("a", bytes({1})));
    ASSERT_TRUE(bus.publish("b", bytes({2})));
    ASSERT_TRUE(pollUntil([&] { return wildcard.count() == 2 && only_a.count() == 1; }));
    bus.stop();
    EXPECT_EQ(only_a.frames()[0].channel, "a");
}

TEST(TelemetryBusTest, FullQueueDropsNewestAndNeverBlocksPublisher) {
    class BlockingSub : public TelemetryBus::Subscriber {
      public:
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
        std::atomic<size_t> count{0};
        void callbackPerform(const TelemetryFrame) override {
            count.fetch_add(1);
            entered.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!release.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }
    };

    TelemetryBus bus(16); // minimum queue capacity
    BlockingSub sub;
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({0})));
    ASSERT_TRUE(pollUntil([&] { return sub.entered.load(); }));

    // Egress thread is parked inside the callback; fill well past capacity.
    const auto publishDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    size_t accepted = 1; // the frame currently being dispatched
    size_t rejected = 0;
    for (int i = 0; i < 64; ++i) {
        if (bus.publish("ch", bytes({static_cast<uint8_t>(i)})))
            ++accepted;
        else
            ++rejected;
    }
    EXPECT_LT(std::chrono::steady_clock::now(), publishDeadline)
        << "publish must never block on a full queue";
    EXPECT_GE(rejected, 1u);
    EXPECT_EQ(bus.droppedCount(), rejected);

    sub.release.store(true);
    bus.stop();
    EXPECT_EQ(sub.count.load(), accepted) << "every accepted frame is delivered, drops are honest";
}

TEST(TelemetryBusTest, StopDrainsAcceptedFramesThenExits) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("ch", &sub);
    bus.start();
    const int kFrames = 100;
    for (int i = 0; i < kFrames; ++i) {
        ASSERT_TRUE(bus.publish("ch", bytes({static_cast<uint8_t>(i)})));
    }
    bus.stop(); // must drain everything already accepted before returning
    EXPECT_EQ(sub.count(), static_cast<size_t>(kFrames));
    EXPECT_FALSE(bus.isRunning());
}

TEST(TelemetryBusTest, SelfStopFromCallbackDoesNotDeadlock) {
    class SelfStopSub : public TelemetryBus::Subscriber {
      public:
        explicit SelfStopSub(TelemetryBus& bus) : bus_(bus) {}
        std::atomic<bool> called{false};
        void callbackPerform(const TelemetryFrame) override {
            bus_.stop(); // re-entrant stop on the egress thread: must defer, not join self
            called.store(true);
        }

      private:
        TelemetryBus& bus_;
    };

    TestWatchdog watchdog(std::chrono::seconds(30));
    TelemetryBus bus;
    SelfStopSub sub(bus);
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({1})));
    ASSERT_TRUE(pollUntil([&] { return sub.called.load(); }));
    ASSERT_TRUE(pollUntil([&] { return !bus.isRunning(); }));
    bus.stop(); // external stop reclaims the deferred join
    EXPECT_FALSE(bus.publish("ch", bytes({2})));
}

// C42 regression at the TelemetryBus layer (sibling of timingController's test
// of the same name, fixed in 3a8de96): after a deferred self-stop, an external
// start() must join the leftover worker BEFORE flipping running_. Flipping
// first lets a worker still inside its final callback re-enter its loop
// (running_ reads true again) and never exit — start() then blocks forever in
// join() holding lifecycleMutex_, deadlocking every later lifecycle call.
TEST(TelemetryBusTest, StartAfterDeferredSelfStopReclaimsHandleAndRunsAgain) {
    class ParkAfterSelfStopSub : public TelemetryBus::Subscriber {
      public:
        explicit ParkAfterSelfStopSub(TelemetryBus& bus) : bus_(bus) {}
        std::atomic<bool> stopped{false};
        std::atomic<bool> release{false};
        void callbackPerform(const TelemetryFrame) override {
            bus_.stop(); // deferred self-stop: running_ false, worker parked here
            stopped.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!release.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }

      private:
        TelemetryBus& bus_;
    };

    TestWatchdog watchdog(std::chrono::seconds(30));
    TelemetryBus bus;
    ParkAfterSelfStopSub sub(bus);
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({1})));
    ASSERT_TRUE(pollUntil([&] { return sub.stopped.load(); }));

    // Worker is parked INSIDE its final callback with running_ already false —
    // the exact window a supervisor's `if (!bus.isRunning()) bus.start();` hits.
    std::atomic<bool> startReturned{false};
    std::atomic<bool> startResult{false};
    std::thread restarter([&] {
        startResult.store(bus.start());
        startReturned.store(true);
    });

    // Smoking gun of the regression: running_ observable as true while the
    // leftover worker is still inside its callback (the join must come first).
    EXPECT_FALSE(pollUntil([&] { return bus.isRunning(); }, std::chrono::milliseconds(200)))
        << "start() flipped running_ before joining the leftover worker (C42 shape)";

    sub.release.store(true);
    ASSERT_TRUE(pollUntil([&] { return startReturned.load(); }, std::chrono::seconds(10)))
        << "start() never returned: the leftover worker re-entered its loop";
    restarter.join();
    EXPECT_TRUE(startResult.load());
    EXPECT_TRUE(bus.isRunning());

    // The fresh worker actually dispatches.
    bus.unsubscribe("ch", &sub);
    CollectingSub fresh;
    bus.subscribe("ch", &fresh);
    ASSERT_TRUE(bus.publish("ch", bytes({2})));
    ASSERT_TRUE(pollUntil([&] { return fresh.count() == 1; }));
    bus.stop();
}

// C42 sibling, second failure mode: a subscriber calls stop() then start() ON
// the egress thread. start() must refuse outright — reaching worker_.join()
// there is a self-join (pthread EDEADLK -> std::system_error out of the thread
// -> std::terminate).
TEST(TelemetryBusTest, SelfRestartFromCallbackIsRefusedNotTerminate) {
    class StopStartSub : public TelemetryBus::Subscriber {
      public:
        explicit StopStartSub(TelemetryBus& bus) : bus_(bus) {}
        std::atomic<bool> called{false};
        std::atomic<bool> startAccepted{true};
        void callbackPerform(const TelemetryFrame) override {
            bus_.stop();
            startAccepted.store(bus_.start()); // must refuse, not terminate
            called.store(true);
        }

      private:
        TelemetryBus& bus_;
    };

    TestWatchdog watchdog(std::chrono::seconds(30));
    TelemetryBus bus;
    StopStartSub sub(bus);
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({1})));
    ASSERT_TRUE(pollUntil([&] { return sub.called.load(); }));
    EXPECT_FALSE(sub.startAccepted.load()) << "self-restart from the egress thread must be refused";
    ASSERT_TRUE(pollUntil([&] { return !bus.isRunning(); }));

    bus.stop();               // external stop reclaims the deferred join
    EXPECT_TRUE(bus.start()); // external restart is allowed afterwards
    bus.stop();
}

TEST(TelemetryBusTest, UnsubscribeStopsDelivery) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({1})));
    ASSERT_TRUE(pollUntil([&] { return sub.count() == 1; }));
    EXPECT_TRUE(bus.unsubscribe("ch", &sub));
    ASSERT_TRUE(bus.publish("ch", bytes({2})));
    bus.stop(); // drains: if still subscribed the second frame would arrive
    EXPECT_EQ(sub.count(), 1u);
}

TEST(TelemetryBusTest, RestartAfterStopWorks) {
    TelemetryBus bus;
    CollectingSub sub;
    bus.subscribe("ch", &sub);
    bus.start();
    ASSERT_TRUE(bus.publish("ch", bytes({1})));
    bus.stop();
    ASSERT_TRUE(bus.start());
    ASSERT_TRUE(bus.publish("ch", bytes({2})));
    bus.stop();
    EXPECT_EQ(sub.count(), 2u);
}

TEST(TelemetryBusTest, DestructorStopsAndDrainsWithoutExplicitStop) {
    CollectingSub sub;
    {
        TelemetryBus bus;
        bus.subscribe("ch", &sub);
        bus.start();
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(bus.publish("ch", bytes({static_cast<uint8_t>(i)})));
        }
        // no stop(): destructor must stop, drain, and join (ASan/TSan verify)
    }
    EXPECT_EQ(sub.count(), 10u);
}

// Lock-free-claim proof (project rule 8): multi-producer stress that must run
// TSan-clean. One producer per channel; verifies per-channel FIFO order,
// gapless per-channel seq, and accepted == delivered accounting.
TEST(TelemetryBusTest, MultiProducerStressPreservesPerChannelOrder) {
    constexpr int kProducers = 4;
    constexpr int kFramesPerProducer = 3000;

    TelemetryBus bus(1024);
    CollectingSub subs[kProducers];
    for (int p = 0; p < kProducers; ++p) {
        bus.subscribe("ch" + std::to_string(p), &subs[p]);
    }
    bus.start();

    std::atomic<size_t> accepted[kProducers];
    for (auto& a : accepted)
        a.store(0);

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            const std::string channel = "ch" + std::to_string(p);
            for (uint32_t i = 0; i < kFramesPerProducer; ++i) {
                TelemetryBus::Message payload{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8),
                                              static_cast<uint8_t>(i >> 16),
                                              static_cast<uint8_t>(i >> 24)};
                if (bus.publish(channel, std::move(payload)))
                    accepted[p].fetch_add(1);
            }
        });
    }
    for (auto& t : producers)
        t.join();
    bus.stop(); // drains all accepted frames

    for (int p = 0; p < kProducers; ++p) {
        const auto frames = subs[p].frames();
        ASSERT_EQ(frames.size(), accepted[p].load()) << "producer " << p;
        uint32_t last = 0;
        bool first = true;
        for (size_t i = 0; i < frames.size(); ++i) {
            EXPECT_EQ(frames[i].seq, i);
            ASSERT_EQ(frames[i].payload.size(), 4u);
            const uint32_t value = static_cast<uint32_t>(frames[i].payload[0]) |
                                   (static_cast<uint32_t>(frames[i].payload[1]) << 8) |
                                   (static_cast<uint32_t>(frames[i].payload[2]) << 16) |
                                   (static_cast<uint32_t>(frames[i].payload[3]) << 24);
            if (!first) {
                EXPECT_GT(value, last) << "per-producer per-channel FIFO violated at " << i;
            }
            last = value;
            first = false;
        }
    }
}
