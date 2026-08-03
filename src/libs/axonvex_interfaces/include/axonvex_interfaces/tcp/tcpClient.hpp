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
#include <sys/time.h>
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
        // shutdown() wakes a blocked recv() immediately without invalidating the
        // fd, so it is safe to call while recvLoop is inside recv(). close() is
        // not: the number can be reused by the next socket()/open() and the
        // blocked call would then read a stranger's fd. So shutdown here, close
        // only after the join (C24). SO_RCVTIMEO is the backstop if the peer
        // state makes shutdown a no-op.
        //
        // Deliberately NOT under sockMutex_. send() holds that mutex across a
        // blocking ::send(), so taking it here would let a stalled peer (TCP
        // zero window, no SO_SNDTIMEO) gate the wakeup: stop() would block on
        // the mutex, never reach the shutdown, and never start the join — with
        // no bound at all, let alone RECV_TIMEOUT_MS. It is safe unlocked
        // because sock_ has no concurrent writer at this point: it is assigned
        // in connectSocket() before the reader thread starts, and cleared below
        // only after the join.
        if (sock_ >= 0)
            ::shutdown(sock_, SHUT_RDWR);
#endif
        if (worker_.joinable())
            worker_.join();
#if defined(AXONVEX_PLATFORM_LINUX)
        std::lock_guard<std::mutex> lock(sockMutex_);
        if (sock_ >= 0) {
            ::close(sock_);
            sock_ = -1;
        }
#endif
    }

    bool isRunning() const override {
        return running_.load();
    }

    bool send(const std::vector<uint8_t>& data) override {
#if defined(AXONVEX_PLATFORM_LINUX)
        // Held across both sends so the fd cannot be closed mid-frame and so two
        // concurrent senders cannot interleave a payload with another's
        // delimiter. Ceiling: a send that blocks on a full socket buffer blocks
        // every other sender and delays stop()'s close.
        std::lock_guard<std::mutex> lock(sockMutex_);
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
        stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
        stats_.bytesSent.fetch_add(static_cast<uint64_t>(total), std::memory_order_relaxed);
        return true;
#else
        stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
        stats_.bytesSent.fetch_add(data.size(), std::memory_order_relaxed);
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

    /// Setup only: host_/port_ are read unguarded by connectSocket(), so this
    /// must complete before start() and must not run concurrently with itself.
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
        return stats_.snapshot();
    }

  private:
    static constexpr const char* defaultKey() {
        return "default";
    }

    /// Upper bound on how long stop() waits for the receive thread to notice it.
    static constexpr int RECV_TIMEOUT_MS = 100;

#if defined(AXONVEX_PLATFORM_LINUX)
    bool connectSocket() {
        // Create socket
        sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0)
            return false;

        // Bounded blocking so recvLoop can observe running_ == false and exit on
        // its own, which is what lets stop() join before closing the fd. Ceiling:
        // stop() may take up to RECV_TIMEOUT_MS if shutdown() does not wake it.
        timeval tv{};
        tv.tv_sec = RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
        ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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
        // sock_ is stable for the whole loop: it is assigned in connectSocket()
        // before this thread is created and only cleared in stop() after the
        // join, so both edges are ordered by the thread handoff itself.
        const int fd = sock_;
        std::vector<uint8_t> buffer;
        buffer.reserve(4096);
        std::array<char, 1024> tmp{};
        while (running_.load()) {
            ssize_t n = ::recv(fd, tmp.data(), static_cast<int>(tmp.size()), 0);
            if (n == 0) {
                // A shutdown() from stop() also surfaces as EOF; only report it
                // as a peer-side close if we did not ask for the teardown.
                if (running_.load())
                    reportError("tcp: connection closed by peer");
                break;
            }
            if (n < 0) {
                // SO_RCVTIMEO expiry: no data, just re-check running_.
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                if (running_.load())
                    reportError(std::string("tcp: recv error: ") + strerror(errno));
                break;
            }
            // Append and scan for newline-delimited frames
            buffer.insert(buffer.end(), tmp.begin(), tmp.begin() + n);
            stats_.bytesReceived.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            // Extract frames
            size_t start = 0;
            for (size_t i = 0; i < buffer.size(); ++i) {
                if (buffer[i] == '\n') {
                    std::vector<uint8_t> frame(buffer.begin() + start, buffer.begin() + i);
                    stats_.messagesReceived.fetch_add(1, std::memory_order_relaxed);
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
    AtomicProtocolStatistics stats_{};

    /// Guards sock_ against being closed while send() is inside ::send().
    mutable std::mutex sockMutex_;

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
#endif
};

} // namespace axonvex::interfaces::tcp
