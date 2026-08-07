#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_safety/watchdog.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>

using axonvex::safety::Watchdog;
using axonvex::safety::WatchdogEvent;

class WDHandler : public axonvex::core::Callback<WatchdogEvent> {
  public:
    std::atomic<int> count{0};
    void callbackPerform(const WatchdogEvent e) override {
        (void)e;
        count++;
    }
};

TEST(WatchdogTest, TriggersOnTimeout) {
    Watchdog wd(std::chrono::milliseconds(50));
    WDHandler h;
    wd.registerHandler(&h);
    ASSERT_TRUE(wd.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    wd.stop();
    EXPECT_GE(h.count.load(), 1);
}

TEST(WatchdogTest, TickPreventsTimeout) {
    Watchdog wd(std::chrono::milliseconds(100));
    WDHandler h;
    wd.registerHandler(&h);
    ASSERT_TRUE(wd.start());
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        wd.tick();
    }
    wd.stop();
    EXPECT_EQ(h.count.load(), 0);
}

// C26: a timeout handler that throws must not unwind out of the watchdog's
// thread entry — that is std::terminate, and the watchdog is the component that
// most needs to survive misbehaving user code. The watchdog must also keep
// arming afterwards: one bad handler does not disarm it for the rest of the run.
namespace {
class ThrowingWDHandler final : public axonvex::core::Callback<axonvex::safety::WatchdogEvent> {
  public:
    std::atomic<int> count{0};
    void callbackPerform(const axonvex::safety::WatchdogEvent) override {
        ++count;
        throw std::runtime_error("handler blew up");
    }
};

// C33's shape, third occurrence: handlers run ON the worker thread, so a
// handler calling stop() used to make that thread join itself.
class StoppingWDHandler final : public axonvex::core::Callback<axonvex::safety::WatchdogEvent> {
  public:
    axonvex::safety::Watchdog* target{nullptr};
    std::atomic<int> count{0};
    void callbackPerform(const axonvex::safety::WatchdogEvent) override {
        ++count;
        if (target) {
            target->stop();
        }
    }
};
} // namespace

TEST(WatchdogTest, ThrowingHandlerDoesNotKillTheWatchdog) {
    Watchdog wd(std::chrono::milliseconds(30));
    ThrowingWDHandler bad;
    WDHandler good;
    wd.registerHandler(&bad);
    wd.registerHandler(&good);
    ASSERT_TRUE(wd.start());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (bad.count.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    wd.stop();

    // Fired more than once: the loop survived the first throw and re-armed.
    EXPECT_GE(bad.count.load(), 2);
    EXPECT_GE(wd.getStatistics().timeouts, 2u);
}

TEST(WatchdogTest, HandlerMayStopTheWatchdog) {
    Watchdog wd(std::chrono::milliseconds(30));
    StoppingWDHandler h;
    h.target = &wd;
    wd.registerHandler(&h);
    ASSERT_TRUE(wd.start());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (h.count.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(h.count.load(), 0);

    // The self-stop defers the join; an external stop() must still complete it.
    wd.stop();
}

// C36: removing the running_ check-then-act (needed to make a deferred
// self-stop reachable) left nothing serialising two external stop() calls —
// both clear the self-check and both can reach join() on the same std::thread,
// which is UB. A lifecycle mutex now serialises the join path.
TEST(WatchdogTest, ConcurrentStopIsSafe) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        Watchdog wd(std::chrono::milliseconds(20));
        WDHandler h;
        wd.registerHandler(&h);
        ASSERT_TRUE(wd.start());

        std::atomic<int> ready{0};
        std::vector<std::thread> stoppers;
        for (int t = 0; t < 4; ++t) {
            stoppers.emplace_back([&wd, &ready]() {
                ready.fetch_add(1);
                while (ready.load() < 4) {
                    std::this_thread::yield(); // widen the overlap
                }
                wd.stop();
            });
        }
        for (auto& s : stoppers) {
            s.join();
        }
    }
}
