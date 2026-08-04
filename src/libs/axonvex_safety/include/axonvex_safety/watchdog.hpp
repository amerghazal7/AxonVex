#pragma once

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_core/caller.hpp>
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

    ~Watchdog() {
        stop();
    }

    bool start() {
        if (running_.exchange(true))
            return true;
        nextDeadline_ = Clock::now() + period_;
        worker_ = std::thread([this]() {
            workerId_.store(std::this_thread::get_id(), std::memory_order_release);
            run();
        });
        return true;
    }

    void stop() {
        // No early return on running_: after a deferred self-stop the flag is
        // already false while the thread still needs reclaiming.
        running_.store(false);

        // Checked before anything else touches worker_. A handler runs ON the
        // worker, so self-detection must not read the std::thread object —
        // start() move-assigns it, and the child has no happens-before edge to
        // that write (TSan caught exactly this). The id is published by the
        // child itself, so reading it here is safe from either side.
        if (workerId_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            // Self-stop from a timeout handler. Joining would be a self-join:
            // system_error unwinding out of the thread entry, i.e.
            // std::terminate (C33's shape). The loop exits once this handler
            // returns; the join is left to the destructor or an external stop().
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }
        workerId_.store(std::thread::id(), std::memory_order_release);
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

    void registerHandler(Callback* cb) {
        caller_.registerCallback(cb);
    }
    bool unregisterHandler(Callback* cb) {
        return caller_.unregisterCallback(cb);
    }

    /// Plain snapshot handed to callers; the live counter behind it is atomic
    /// because the worker increments it while any thread may read.
    struct Statistics {
        uint64_t timeouts{0};
    };
    Statistics getStatistics() const {
        Statistics out;
        out.timeouts = timeouts_.load(std::memory_order_relaxed);
        return out;
    }

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
                timeouts_.fetch_add(1, std::memory_order_relaxed);
                // A throwing handler must not unwind out of this thread entry:
                // that is std::terminate, and the watchdog is precisely the
                // component that must survive misbehaving user code (C26). The
                // loop continues — one bad handler does not disarm the
                // watchdog for the rest of the run.
                try {
                    caller_.callCallbacks({"watchdog timeout"});
                } catch (...) {}
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
    std::atomic<uint64_t> timeouts_{0};
    /// Published by the worker itself; see stop().
    std::atomic<std::thread::id> workerId_{std::thread::id()};
};

} // namespace axonvex::safety
