#include <gtest/gtest.h>
#include <axonvex/axonvex.hpp>
#include <atomic>

using axonvex::safety::Watchdog;
using axonvex::safety::WatchdogEvent;

class WDHandler : public axonvex::core::Callback<WatchdogEvent> {
public:
    std::atomic<int> count{0};
    void callbackPerform(const WatchdogEvent e) override {
        (void)e; count++;
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
