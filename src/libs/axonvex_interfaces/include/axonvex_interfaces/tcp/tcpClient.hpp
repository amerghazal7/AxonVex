#pragma once

#include <array>
#include <atomic>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace axonvex::interfaces::tcp {

class TcpClient : public axonvex::interfaces::ProtocolInterface {
  public:
    TcpClient(std::string host = "127.0.0.1", uint16_t port = 9000)
        : host_(std::move(host)), port_(port) {}

    bool start() override {
        if (running_.load())
            return true;
#if defined(AXONVEX_PLATFORM_LINUX)
        if (!connectSocket()) {
            reportError("tcp: failed to connect to " + host_ + ":" + std::to_string(port_));
            return false;
        }
        running_.store(true);
        worker_ = std::thread([this]() { recvLoop(); });
        return true;
#else
        running_.store(true);
        // Fallback simulation loop (no real networking)
        worker_ = std::thread([this]() {
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        return true;
#endif
    }

    void stop() override {
        if (!running_.load())
            return;
        running_.store(false);
#if defined(AXONVEX_PLATFORM_LINUX)
        // Wake recv thread and close socket
        if (sock_ >= 0) {
            ::shutdown(sock_, SHUT_RDWR);
            ::close(sock_);
            sock_ = -1;
        }
#endif
        if (worker_.joinable())
            worker_.join();
    }

    bool isRunning() const override {
        return running_.load();
    }

    bool send(const std::vector<uint8_t>& data) override {
#if defined(AXONVEX_PLATFORM_LINUX)
        if (sock_ < 0)
            return false;
        // Send data plus a newline as frame delimiter
        ssize_t total = 0;
        if (!data.empty()) {
            ssize_t n = ::send(sock_, data.data(), data.size(), MSG_NOSIGNAL);
            if (n < 0) {
                reportError("tcp: send failed: " + std::string(strerror(errno)));
                return false;
            }
            total += n;
        }
        const char nl = '\n';
        if (::send(sock_, &nl, 1, MSG_NOSIGNAL) < 0) {
            reportError("tcp: send delimiter failed: " + std::string(strerror(errno)));
            return false;
        }
        total += 1;
        stats_.messagesSent++;
        stats_.bytesSent += static_cast<uint64_t>(total);
        return true;
#else
        stats_.messagesSent++;
        stats_.bytesSent += data.size();
        std::lock_guard<std::mutex> lock(cbMutex_);
        this->callCallbacksByKey(defaultKey(), data);
        return true;
#endif
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
        if (key == "host") {
            host_ = value;
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
        return stats_;
    }

  private:
    static constexpr const char* defaultKey() {
        return "default";
    }

#if defined(AXONVEX_PLATFORM_LINUX)
    bool connectSocket() {
        // Create socket
        sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0)
            return false;

        // Resolve host
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            ::close(sock_);
            sock_ = -1;
            return false;
        }
        if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(sock_);
            sock_ = -1;
            return false;
        }
        return true;
    }

    void recvLoop() {
        std::vector<uint8_t> buffer;
        buffer.reserve(4096);
        std::array<char, 1024> tmp{};
        while (running_.load()) {
            ssize_t n = ::recv(sock_, tmp.data(), static_cast<int>(tmp.size()), 0);
            if (n == 0) {
                reportError("tcp: connection closed by peer");
                break;
            }
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                reportError(std::string("tcp: recv error: ") + strerror(errno));
                break;
            }
            // Append and scan for newline-delimited frames
            buffer.insert(buffer.end(), tmp.begin(), tmp.begin() + n);
            stats_.bytesReceived += static_cast<uint64_t>(n);
            // Extract frames
            size_t start = 0;
            for (size_t i = 0; i < buffer.size(); ++i) {
                if (buffer[i] == '\n') {
                    std::vector<uint8_t> frame(buffer.begin() + start, buffer.begin() + i);
                    stats_.messagesReceived++;
                    {
                        std::lock_guard<std::mutex> lock(cbMutex_);
                        this->callCallbacksByKey(defaultKey(), frame);
                    }
                    start = i + 1;
                }
            }
            if (start > 0) {
                buffer.erase(buffer.begin(), buffer.begin() + start);
            }
        }
    }

    void reportError(const std::string& msg) {
        std::lock_guard<std::mutex> lock(cbMutex_);
        errorKeyed_.callCallbacksByKey(defaultKey(), msg);
    }
#endif

    std::string host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    MessageCallback* defaultMsgCb_{nullptr};

    axonvex::core::CallerKeyed<std::string, std::string> errorKeyed_;
    std::unique_ptr<ErrorHandler> errorAdapter_;

    mutable std::mutex cbMutex_;
    ProtocolStatistics stats_{};

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
#endif
};

} // namespace axonvex::interfaces::tcp
