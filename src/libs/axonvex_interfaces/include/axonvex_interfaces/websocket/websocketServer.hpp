#pragma once

#include <atomic>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace axonvex::interfaces::websocket {

// Minimal placeholder WebSocket server interface (no external deps yet)
class WebSocketServer : public axonvex::interfaces::ProtocolInterface {
  public:
    explicit WebSocketServer(std::string address = "127.0.0.1", uint16_t port = 8080)
        : address_(std::move(address)), port_(port) {}

    bool start() override {
        if (running_.load())
            return true;
        running_.store(true);
        // Placeholder: spawn a thread that simulates receive loop
        worker_ = std::thread([this]() {
            while (running_.load()) {
                // No-op receive loop placeholder
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        return true;
    }

    void stop() override {
        if (!running_.load())
            return;
        running_.store(false);
        if (worker_.joinable())
            worker_.join();
    }

    bool isRunning() const override {
        return running_.load();
    }

    bool send(const std::vector<uint8_t>& data) override {
        // Placeholder send: echo back through callback(s)
        stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
        stats_.bytesSent.fetch_add(data.size(), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(cbMutex_);
        this->callCallbacksByKey(defaultKey(), data);
        return true;
    }

    using ProtocolInterface::ErrorCallback;
    using ProtocolInterface::ErrorHandler;
    using ProtocolInterface::MessageCallback;

    void setMessageCallback(MessageCallback* cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        if (defaultMsgCb_) {
            this->unregisterKeyedCallback(defaultKey(), defaultMsgCb_);
            defaultMsgCb_ = nullptr;
        }
        if (cb) {
            this->registerKeyedCallback(defaultKey(), cb);
            defaultMsgCb_ = cb;
        }
    }

    void registerMessageHandler(const std::string& key, MessageCallback* cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        this->registerKeyedCallback(key, cb);
    }

    bool unregisterMessageHandler(const std::string& key, MessageCallback* cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        return this->unregisterKeyedCallback(key, cb);
    }

    size_t unregisterAllMessageHandlersForKey(const std::string& key) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        return this->unregisterAllCallbacksForKey(key);
    }

    void setErrorCallback(ErrorCallback cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        // Replace the convenience adapter
        errorAdapter_.reset();
        if (cb) {
            struct FnAdapter : public ErrorHandler {
                ErrorCallback fn;
                explicit FnAdapter(ErrorCallback f) : fn(std::move(f)) {}
                void callbackPerform(const std::string s) override {
                    if (fn)
                        fn(s);
                }
            };
            errorAdapter_ = std::make_unique<FnAdapter>(std::move(cb));
            this->registerErrorHandler(defaultKey(), errorAdapter_.get());
        }
    }

    void registerErrorHandler(const std::string& key, ErrorHandler* cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        errorKeyed_.registerKeyedCallback(key, cb);
    }

    bool unregisterErrorHandler(const std::string& key, ErrorHandler* cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        return errorKeyed_.unregisterKeyedCallback(key, cb);
    }

    size_t unregisterAllErrorHandlersForKey(const std::string& key) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        return errorKeyed_.unregisterAllCallbacksForKey(key);
    }

    bool configure(const std::string& key, const std::string& value) override {
        if (key == "address") {
            address_ = value;
            return true;
        }
        if (key == "port") {
            try {
                port_ = static_cast<uint16_t>(std::stoul(value));
                return true;
            } catch (...) { return false; }
        }
        return false;
    }

    ProtocolStatistics getStatistics() const override {
        return stats_.snapshot();
    }

    std::string url() const {
        return "ws://" + address_ + ":" + std::to_string(port_);
    }

  private:
    static constexpr const char* defaultKey() {
        return "default";
    }

    std::string address_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    MessageCallback* defaultMsgCb_{nullptr};

    // Error handlers are keyed separately from messages
    axonvex::core::CallerKeyed<std::string, std::string> errorKeyed_;
    std::unique_ptr<ErrorHandler> errorAdapter_;

    mutable std::mutex cbMutex_;
    AtomicProtocolStatistics stats_{};
};

} // namespace axonvex::interfaces::websocket
