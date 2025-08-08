#pragma once

#include <axonvex/core/caller.hpp>
#include <axonvex/core/callback.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace axonvex::safety {

struct WatchdogEvent {
    std::string reason;
};

class Watchdog {
  public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;
    using Callback = axonvex::core::Callback<WatchdogEvent>;

    explicit Watchdog(Duration period = Duration(1000)) : period_(period) {}

    ~Watchdog() { stop(); }

    bool start() {
        if (running_.exchange(true)) return true;
        nextDeadline_ = Clock::now() + period_;
        worker_ = std::thread([this]() { run(); });
        return true;
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    void tick() {
        std::lock_guard<std::mutex> lock(mtx_);
        nextDeadline_ = Clock::now() + period_;
    }

    void setPeriod(Duration d) {
        std::lock_guard<std::mutex> lock(mtx_);
        period_ = d;
        nextDeadline_ = Clock::now() + period_;
    }

    void registerHandler(Callback* cb) { caller_.registerCallback(cb); }
    bool unregisterHandler(Callback* cb) { return caller_.unregisterCallback(cb); }

    struct Statistics { uint64_t timeouts{0}; };
    Statistics getStatistics() const { return stats_; }

  private:
    void run() {
        while (running_.load()) {
            auto now = Clock::now();
            bool timeout = false;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (now >= nextDeadline_) {
                    timeout = true;
                    nextDeadline_ = now + period_;
                }
            }
            if (timeout) {
                stats_.timeouts++;
                caller_.callCallbacks({"watchdog timeout"});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    Duration period_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    mutable std::mutex mtx_;
    Clock::time_point nextDeadline_{};

    axonvex::core::Caller<WatchdogEvent> caller_;
    Statistics stats_{};
};

} // namespace axonvex::safety
