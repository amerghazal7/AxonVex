#pragma once

#include <axonvex_core/callback.hpp>
#include <axonvex_core/callerKeyed.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::adapters {

enum class AdapterState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error,
    ShuttingDown
};

inline std::string adapterStateToString(AdapterState state) {
    switch (state) {
        case AdapterState::Disconnected:  return "Disconnected";
        case AdapterState::Connecting:    return "Connecting";
        case AdapterState::Connected:     return "Connected";
        case AdapterState::Reconnecting:  return "Reconnecting";
        case AdapterState::Error:         return "Error";
        case AdapterState::ShuttingDown:  return "ShuttingDown";
    }
    return "Unknown";
}

struct AdapterStatistics {
    uint64_t messagesReceived{0};
    uint64_t messagesSent{0};
    uint64_t bytesReceived{0};
    uint64_t bytesSent{0};
    uint64_t errorsCount{0};
    uint64_t reconnectCount{0};
    std::chrono::steady_clock::time_point connectedSince;

    double uptimeSeconds() const {
        if (connectedSince == std::chrono::steady_clock::time_point{}) return 0.0;
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - connectedSince)
            .count();
    }

    void reset() {
        messagesReceived = messagesSent = bytesReceived = bytesSent =
            errorsCount = reconnectCount = 0;
        connectedSince = {};
    }
};

struct AdapterMessage {
    std::string topic;
    std::vector<uint8_t> payload;
    std::chrono::steady_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
};

/**
 * @brief Abstract interface that all protocol adapters must implement
 *
 * Lifecycle: initialize() -> start() -> [publish/subscribe] -> stop() -> shutdown()
 */
class AdapterInterface {
  public:
    virtual ~AdapterInterface() = default;

    virtual std::string name() const = 0;
    virtual std::string protocolId() const = 0;

    virtual bool initialize() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual void shutdown() = 0;

    virtual AdapterState state() const = 0;
    virtual bool isConnected() const = 0;

    virtual bool publish(const std::string& topic,
                         const std::vector<uint8_t>& payload) = 0;

    using MessageCallback = axonvex::core::Callback<AdapterMessage>;
    virtual void subscribe(const std::string& topic, MessageCallback* cb) = 0;
    virtual bool unsubscribe(const std::string& topic, MessageCallback* cb) = 0;

    using ErrorCallback = axonvex::core::Callback<std::string>;
    virtual void registerErrorHandler(const std::string& key, ErrorCallback* cb) = 0;
    virtual bool unregisterErrorHandler(const std::string& key, ErrorCallback* cb) = 0;

    virtual bool configure(const std::string& key, const std::string& value) = 0;

    virtual AdapterStatistics statistics() const = 0;
    virtual bool healthCheck() const = 0;
};

// =====================================================================
// AdapterBase — common state machine, subscription routing, stats
// =====================================================================

/**
 * @brief Reusable base implementing the adapter contract.
 *
 * Concrete adapters (in plugin libs like axonvex_ros2) extend this and
 * only provide: name(), protocolId(), configure(), and optionally
 * override onStart/onStop/onShutdown/enrichMessage for real transport.
 */
class AdapterBase : public AdapterInterface {
  public:
    ~AdapterBase() override {
        if (state_.load() != AdapterState::Disconnected &&
            state_.load() != AdapterState::ShuttingDown) {
            stop();
            shutdown();
        }
    }

    bool initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.load() != AdapterState::Disconnected) {
            reportError("initialize() called in invalid state: " +
                        adapterStateToString(state_.load()));
            return false;
        }
        stats_.reset();
        initialized_ = true;
        return true;
    }

    bool start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            reportError("start() called before initialize()");
            return false;
        }
        auto current = state_.load();
        if (current == AdapterState::Connected) return true;
        if (current != AdapterState::Disconnected) {
            reportError("start() called in invalid state: " +
                        adapterStateToString(current));
            return false;
        }
        state_.store(AdapterState::Connecting);
        if (!onStart()) {
            state_.store(AdapterState::Error);
            stats_.errorsCount++;
            return false;
        }
        state_.store(AdapterState::Connected);
        stats_.connectedSince = std::chrono::steady_clock::now();
        return true;
    }

    bool stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto current = state_.load();
        if (current == AdapterState::Disconnected ||
            current == AdapterState::ShuttingDown) {
            return true;
        }
        state_.store(AdapterState::ShuttingDown);
        onStop();
        state_.store(AdapterState::Disconnected);
        return true;
    }

    void shutdown() override {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.store(AdapterState::Disconnected);
        initialized_ = false;
        topicSubscribers_.clear();
        onShutdown();
    }

    AdapterState state() const override { return state_.load(); }
    bool isConnected() const override {
        return state_.load() == AdapterState::Connected;
    }

    bool publish(const std::string& topic,
                 const std::vector<uint8_t>& payload) override {
        if (!isConnected()) {
            reportError("publish() called while not connected");
            stats_.errorsCount++;
            return false;
        }
        stats_.messagesSent++;
        stats_.bytesSent += payload.size();
        deliverLocally(topic, payload);
        return true;
    }

    void subscribe(const std::string& topic, MessageCallback* cb) override {
        std::lock_guard<std::mutex> lock(mutex_);
        topicSubscribers_[topic].push_back(cb);
    }

    bool unsubscribe(const std::string& topic, MessageCallback* cb) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topicSubscribers_.find(topic);
        if (it == topicSubscribers_.end()) return false;
        auto& vec = it->second;
        auto pos = std::find(vec.begin(), vec.end(), cb);
        if (pos == vec.end()) return false;
        vec.erase(pos);
        if (vec.empty()) topicSubscribers_.erase(it);
        return true;
    }

    void registerErrorHandler(const std::string& key, ErrorCallback* cb) override {
        errorBus_.registerKeyedCallback(key, cb);
    }

    bool unregisterErrorHandler(const std::string& key, ErrorCallback* cb) override {
        return errorBus_.unregisterKeyedCallback(key, cb);
    }

    AdapterStatistics statistics() const override { return stats_; }
    bool healthCheck() const override { return isConnected(); }

    size_t subscriberCount(const std::string& topic) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topicSubscribers_.find(topic);
        return it != topicSubscribers_.end() ? it->second.size() : 0;
    }

  protected:
    virtual bool onStart() { return true; }
    virtual void onStop() {}
    virtual void onShutdown() {}
    virtual void enrichMessage(AdapterMessage& /*msg*/) {}

    void reportError(const std::string& message) {
        stats_.errorsCount++;
        errorBus_.callCallbacksByKey("default", message);
    }

    void deliverLocally(const std::string& topic,
                        const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topicSubscribers_.find(topic);
        if (it == topicSubscribers_.end()) return;

        AdapterMessage msg;
        msg.topic = topic;
        msg.payload = payload;
        msg.timestamp = std::chrono::steady_clock::now();
        enrichMessage(msg);

        for (auto* cb : it->second) {
            cb->callbackPerform(msg);
        }
        stats_.messagesReceived += it->second.size();
        stats_.bytesReceived += payload.size() * it->second.size();
    }

    std::atomic<AdapterState> state_{AdapterState::Disconnected};
    bool initialized_{false};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<MessageCallback*>> topicSubscribers_;
    axonvex::core::CallerKeyed<std::string, std::string> errorBus_;
    mutable AdapterStatistics stats_;
};

// =====================================================================
// MockAdapter — for unit testing the adapter contract
// =====================================================================

class MockAdapter final : public AdapterBase {
  public:
    std::string name() const override { return "MockAdapter"; }
    std::string protocolId() const override { return "mock"; }
    bool configure(const std::string& /*key*/, const std::string& /*value*/) override {
        return false;
    }
};

} // namespace axonvex::adapters
