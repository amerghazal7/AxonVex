#pragma once

#include <chrono>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include "../primitives/primitives.hpp"

namespace axonvex::types::time {

/**
 * @brief High-precision timestamp
 */
class Timestamp {
private:
    std::chrono::high_resolution_clock::time_point time_point_;
    
public:
    Timestamp() : time_point_(std::chrono::high_resolution_clock::now()) {}
    
    explicit Timestamp(std::chrono::high_resolution_clock::time_point tp) 
        : time_point_(tp) {}
    
    static Timestamp now() {
        return Timestamp(std::chrono::high_resolution_clock::now());
    }
    
    static Timestamp from_nanoseconds(int64_t ns) {
        auto duration = std::chrono::nanoseconds(ns);
        auto epoch = std::chrono::high_resolution_clock::time_point{};
        return Timestamp(epoch + duration);
    }
    
    static Timestamp from_microseconds(int64_t us) {
        auto duration = std::chrono::microseconds(us);
        auto epoch = std::chrono::high_resolution_clock::time_point{};
        return Timestamp(epoch + duration);
    }
    
    static Timestamp from_milliseconds(int64_t ms) {
        auto duration = std::chrono::milliseconds(ms);
        auto epoch = std::chrono::high_resolution_clock::time_point{};
        return Timestamp(epoch + duration);
    }
    
    int64_t nanoseconds_since_epoch() const {
        auto duration = time_point_.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    }
    
    int64_t microseconds_since_epoch() const {
        auto duration = time_point_.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }
    
    int64_t milliseconds_since_epoch() const {
        auto duration = time_point_.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
    
    double seconds_since_epoch() const {
        auto duration = time_point_.time_since_epoch();
        return std::chrono::duration<double>(duration).count();
    }
    
    Timestamp operator+(const std::chrono::nanoseconds& duration) const {
        return Timestamp(time_point_ + duration);
    }
    
    Timestamp operator-(const std::chrono::nanoseconds& duration) const {
        return Timestamp(time_point_ - duration);
    }
    
    std::chrono::nanoseconds operator-(const Timestamp& other) const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            time_point_ - other.time_point_);
    }
    
    bool operator<(const Timestamp& other) const {
        return time_point_ < other.time_point_;
    }
    
    bool operator<=(const Timestamp& other) const {
        return time_point_ <= other.time_point_;
    }
    
    bool operator>(const Timestamp& other) const {
        return time_point_ > other.time_point_;
    }
    
    bool operator>=(const Timestamp& other) const {
        return time_point_ >= other.time_point_;
    }
    
    bool operator==(const Timestamp& other) const {
        return time_point_ == other.time_point_;
    }
    
    bool operator!=(const Timestamp& other) const {
        return time_point_ != other.time_point_;
    }
    
    const std::chrono::high_resolution_clock::time_point& get_time_point() const {
        return time_point_;
    }
};

/**
 * @brief Duration wrapper with various time units
 */
class Duration {
private:
    std::chrono::nanoseconds duration_;
    
public:
    Duration() : duration_(0) {}
    
    explicit Duration(std::chrono::nanoseconds ns) : duration_(ns) {}
    
    static Duration nanoseconds(int64_t ns) {
        return Duration(std::chrono::nanoseconds(ns));
    }
    
    static Duration microseconds(int64_t us) {
        return Duration(std::chrono::microseconds(us));
    }
    
    static Duration milliseconds(int64_t ms) {
        return Duration(std::chrono::milliseconds(ms));
    }
    
    static Duration seconds(double s) {
        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(s)));
    }
    
    static Duration minutes(double m) {
        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double, std::ratio<60>>(m)));
    }
    
    static Duration hours(double h) {
        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double, std::ratio<3600>>(h)));
    }
    
    int64_t as_nanoseconds() const {
        return duration_.count();
    }
    
    int64_t as_microseconds() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(duration_).count();
    }
    
    int64_t as_milliseconds() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration_).count();
    }
    
    double as_seconds() const {
        return std::chrono::duration<double>(duration_).count();
    }
    
    double as_minutes() const {
        return std::chrono::duration<double, std::ratio<60>>(duration_).count();
    }
    
    double as_hours() const {
        return std::chrono::duration<double, std::ratio<3600>>(duration_).count();
    }
    
    Duration operator+(const Duration& other) const {
        return Duration(duration_ + other.duration_);
    }
    
    Duration operator-(const Duration& other) const {
        return Duration(duration_ - other.duration_);
    }
    
    Duration operator*(double factor) const {
        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            duration_ * factor));
    }
    
    Duration operator/(double divisor) const {
        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            duration_ / divisor));
    }
    
    bool operator<(const Duration& other) const {
        return duration_ < other.duration_;
    }
    
    bool operator<=(const Duration& other) const {
        return duration_ <= other.duration_;
    }
    
    bool operator>(const Duration& other) const {
        return duration_ > other.duration_;
    }
    
    bool operator>=(const Duration& other) const {
        return duration_ >= other.duration_;
    }
    
    bool operator==(const Duration& other) const {
        return duration_ == other.duration_;
    }
    
    bool operator!=(const Duration& other) const {
        return duration_ != other.duration_;
    }
    
    const std::chrono::nanoseconds& get_duration() const {
        return duration_;
    }
};

/**
 * @brief High-precision timer for performance measurement
 */
class Timer {
private:
    Timestamp start_time_;
    bool running_;
    Duration accumulated_;
    
public:
    Timer() : running_(false), accumulated_(Duration::nanoseconds(0)) {}
    
    void start() {
        if (!running_) {
            start_time_ = Timestamp::now();
            running_ = true;
        }
    }
    
    void stop() {
        if (running_) {
            auto now = Timestamp::now();
            auto elapsed = now - start_time_;
            accumulated_ = accumulated_ + Duration(elapsed);
            running_ = false;
        }
    }
    
    void reset() {
        running_ = false;
        accumulated_ = Duration::nanoseconds(0);
    }
    
    Duration elapsed() const {
        if (running_) {
            auto now = Timestamp::now();
            auto current_elapsed = now - start_time_;
            return accumulated_ + Duration(current_elapsed);
        }
        return accumulated_;
    }
    
    bool is_running() const {
        return running_;
    }
    
    // Convenience methods
    double elapsed_seconds() const {
        return elapsed().as_seconds();
    }
    
    int64_t elapsed_milliseconds() const {
        return elapsed().as_milliseconds();
    }
    
    int64_t elapsed_microseconds() const {
        return elapsed().as_microseconds();
    }
    
    int64_t elapsed_nanoseconds() const {
        return elapsed().as_nanoseconds();
    }
};

/**
 * @brief RAII timer for automatic measurement
 */
class ScopedTimer {
private:
    Timer& timer_;
    
public:
    explicit ScopedTimer(Timer& timer) : timer_(timer) {
        timer_.start();
    }
    
    ~ScopedTimer() {
        timer_.stop();
    }
    
    // Non-copyable, non-movable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;
};

/**
 * @brief Rate limiter for controlling execution frequency
 */
class RateLimiter {
private:
    Duration interval_;
    Timestamp last_execution_;
    
public:
    explicit RateLimiter(Duration interval) 
        : interval_(interval), last_execution_(Timestamp::now()) {}
    
    explicit RateLimiter(double frequency_hz) 
        : interval_(Duration::seconds(1.0 / frequency_hz)), 
          last_execution_(Timestamp::now()) {}
    
    bool can_execute() {
        auto now = Timestamp::now();
        auto elapsed = now - last_execution_;
        return Duration(elapsed) >= interval_;
    }
    
    void execute() {
        last_execution_ = Timestamp::now();
    }
    
    bool try_execute() {
        if (can_execute()) {
            execute();
            return true;
        }
        return false;
    }
    
    Duration time_until_next() const {
        auto now = Timestamp::now();
        auto elapsed = now - last_execution_;
        auto remaining = interval_ - Duration(elapsed);
        return remaining > Duration::nanoseconds(0) ? remaining : Duration::nanoseconds(0);
    }
    
    void set_frequency(double frequency_hz) {
        interval_ = Duration::seconds(1.0 / frequency_hz);
    }
    
    void set_interval(Duration interval) {
        interval_ = interval;
    }
    
    double get_frequency() const {
        return 1.0 / interval_.as_seconds();
    }
    
    Duration get_interval() const {
        return interval_;
    }
};

/**
 * @brief Deadline for timeout operations
 */
class Deadline {
private:
    Timestamp deadline_;
    
public:
    explicit Deadline(Duration timeout) 
        : deadline_(Timestamp::now() + timeout.get_duration()) {}
    
    explicit Deadline(Timestamp absolute_time) : deadline_(absolute_time) {}
    
    bool expired() const {
        return Timestamp::now() > deadline_;
    }
    
    Duration remaining() const {
        auto now = Timestamp::now();
        if (now >= deadline_) {
            return Duration::nanoseconds(0);
        }
        return Duration(deadline_ - now);
    }
    
    bool wait_for(Duration max_wait) const {
        auto remaining_time = remaining();
        auto wait_time = remaining_time < max_wait ? remaining_time : max_wait;
        
        if (wait_time > Duration::nanoseconds(0)) {
            std::this_thread::sleep_for(wait_time.get_duration());
        }
        
        return !expired();
    }
    
    Timestamp get_deadline() const {
        return deadline_;
    }
};

/**
 * @brief Time-based event scheduler
 */
class TimeEvent {
public:
    using Callback = std::function<void()>;
    
private:
    Timestamp execute_time_;
    Callback callback_;
    bool recurring_;
    Duration interval_;
    uint64_t id_;
    static std::atomic<uint64_t> next_id_;
    
public:
    TimeEvent(Timestamp execute_time, Callback callback, 
              bool recurring = false, Duration interval = Duration::nanoseconds(0))
        : execute_time_(execute_time), callback_(std::move(callback)),
          recurring_(recurring), interval_(interval), id_(next_id_++) {}
    
    void execute() {
        if (callback_) {
            callback_();
        }
        
        if (recurring_) {
            execute_time_ = execute_time_ + interval_.get_duration();
        }
    }
    
    bool should_execute(const Timestamp& now) const {
        return now >= execute_time_;
    }
    
    bool is_recurring() const { return recurring_; }
    Timestamp get_execute_time() const { return execute_time_; }
    uint64_t get_id() const { return id_; }
    
    bool operator<(const TimeEvent& other) const {
        // For priority queue (min-heap), we want events with earlier times to have higher priority
        return execute_time_ > other.execute_time_;
    }
};

std::atomic<uint64_t> TimeEvent::next_id_{1};

/**
 * @brief Event scheduler for time-based callbacks
 */
class EventScheduler {
private:
    std::priority_queue<TimeEvent> events_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    
public:
    EventScheduler() : running_(false) {}
    
    ~EventScheduler() {
        stop();
    }
    
    void start() {
        if (running_.exchange(true)) {
            return; // Already running
        }
        
        worker_thread_ = std::thread([this]() {
            while (running_) {
                std::unique_lock<std::mutex> lock(mutex_);
                
                if (events_.empty()) {
                    cv_.wait(lock, [this]() { return !events_.empty() || !running_; });
                    continue;
                }
                
                auto now = Timestamp::now();
                auto next_event = events_.top();
                
                if (next_event.should_execute(now)) {
                    events_.pop();
                    lock.unlock();
                    
                    next_event.execute();
                    
                    if (next_event.is_recurring()) {
                        lock.lock();
                        events_.push(next_event);
                    }
                } else {
                    auto wait_time = next_event.get_execute_time() - now;
                    cv_.wait_for(lock, wait_time.get_duration());
                }
            }
        });
    }
    
    void stop() {
        if (!running_.exchange(false)) {
            return; // Already stopped
        }
        
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    uint64_t schedule_at(Timestamp execute_time, TimeEvent::Callback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        TimeEvent event(execute_time, std::move(callback));
        uint64_t id = event.get_id();
        events_.push(std::move(event));
        cv_.notify_one();
        return id;
    }
    
    uint64_t schedule_after(Duration delay, TimeEvent::Callback callback) {
        auto execute_time = Timestamp::now() + delay.get_duration();
        return schedule_at(execute_time, std::move(callback));
    }
    
    uint64_t schedule_recurring(Duration interval, TimeEvent::Callback callback) {
        auto execute_time = Timestamp::now() + interval.get_duration();
        std::lock_guard<std::mutex> lock(mutex_);
        TimeEvent event(execute_time, std::move(callback), true, interval);
        uint64_t id = event.get_id();
        events_.push(std::move(event));
        cv_.notify_one();
        return id;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_ = std::priority_queue<TimeEvent>{};
    }
    
    size_t pending_events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }
    
    bool is_running() const {
        return running_;
    }
};

/**
 * @brief Performance profiler for measuring execution times
 */
class Profiler {
private:
    struct ProfileEntry {
        std::string name;
        Duration total_time;
        uint64_t call_count;
        Duration min_time;
        Duration max_time;
        
        ProfileEntry(const std::string& n) 
            : name(n), total_time(Duration::nanoseconds(0)), call_count(0),
              min_time(Duration::nanoseconds(std::numeric_limits<int64_t>::max())),
              max_time(Duration::nanoseconds(0)) {}
    };
    
    std::unordered_map<std::string, ProfileEntry> entries_;
    std::mutex mutex_;
    
public:
    void record(const std::string& name, Duration duration) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            it = entries_.emplace(name, ProfileEntry(name)).first;
        }
        
        ProfileEntry& entry = it->second;
        entry.total_time = entry.total_time + duration;
        entry.call_count++;
        
        if (duration < entry.min_time) {
            entry.min_time = duration;
        }
        if (duration > entry.max_time) {
            entry.max_time = duration;
        }
    }
    
    Duration get_total_time(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(name);
        return it != entries_.end() ? it->second.total_time : Duration::nanoseconds(0);
    }
    
    Duration get_average_time(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(name);
        if (it != entries_.end() && it->second.call_count > 0) {
            return Duration::nanoseconds(it->second.total_time.as_nanoseconds() / it->second.call_count);
        }
        return Duration::nanoseconds(0);
    }
    
    uint64_t get_call_count(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(name);
        return it != entries_.end() ? it->second.call_count : 0;
    }
    
    Duration get_min_time(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(name);
        return it != entries_.end() ? it->second.min_time : Duration::nanoseconds(0);
    }
    
    Duration get_max_time(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(name);
        return it != entries_.end() ? it->second.max_time : Duration::nanoseconds(0);
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }
    
    std::vector<std::string> get_profile_names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(entries_.size());
        
        for (const auto& entry : entries_) {
            names.push_back(entry.first);
        }
        
        return names;
    }
};

/**
 * @brief RAII profiler for automatic measurement
 */
class ScopedProfiler {
private:
    Profiler& profiler_;
    std::string name_;
    Timer timer_;
    
public:
    ScopedProfiler(Profiler& profiler, const std::string& name) 
        : profiler_(profiler), name_(name) {
        timer_.start();
    }
    
    ~ScopedProfiler() {
        timer_.stop();
        profiler_.record(name_, timer_.elapsed());
    }
    
    // Non-copyable, non-movable
    ScopedProfiler(const ScopedProfiler&) = delete;
    ScopedProfiler& operator=(const ScopedProfiler&) = delete;
    ScopedProfiler(ScopedProfiler&&) = delete;
    ScopedProfiler& operator=(ScopedProfiler&&) = delete;
};

// Utility macros for easy profiling
#define PROFILE_SCOPE(profiler, name) \
    axonvex::types::time::ScopedProfiler _prof(profiler, name)

#define PROFILE_FUNCTION(profiler) \
    axonvex::types::time::ScopedProfiler _prof(profiler, __FUNCTION__)

} // namespace axonvex::types::time