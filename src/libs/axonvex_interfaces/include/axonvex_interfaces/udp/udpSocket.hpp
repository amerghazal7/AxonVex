#pragma once

#include <array>
#include <atomic>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace axonvex::interfaces::udp {

class UdpSocket : public axonvex::interfaces::ProtocolInterface {
  public:
    UdpSocket(std::string bindAddress = "0.0.0.0", uint16_t port = 9001)
        : bindAddress_(std::move(bindAddress)), port_(port) {}

    bool start() override {
        if (running_.load())
            return true;
#if defined(AXONVEX_PLATFORM_LINUX)
        if (!openAndBind()) {
            reportError("udp: failed to bind to " + bindAddress_ + ":" + std::to_string(port_));
            return false;
        }
        running_.store(true);
        worker_ = std::thread([this]() { recvLoop(); });
        return true;
#else
        running_.store(true);
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
        // Join BEFORE closing: closing an fd that another thread is blocked in
        // recvfrom() on is a use-after-close — the number can be handed straight
        // back out by the next socket()/open() and the blocked call then reads a
        // stranger's fd. recvLoop notices running_ within RECV_TIMEOUT_MS via
        // SO_RCVTIMEO, so it exits on its own without the close (C24).
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
        sockaddr_in dest{};
        socklen_t dlen = sizeof(dest);
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            if (remoteSet_) {
                dest = remoteAddr_;
            } else if (lastPeerSet_) {
                dest = lastPeer_;
            } else {
                dlen = 0;
            }
        }
        if (dlen == 0) {
            reportError("udp: no destination (configure remote_host/remote_port or receive a "
                        "packet first)");
            return false;
        }
        // sendto runs under sockMutex_ so the fd cannot be closed out from under
        // it. A UDP sendto does not block short of a full socket buffer, so the
        // hold is bounded; if that ever changes, hand out a dup()'d fd instead.
        ssize_t n = -1;
        {
            std::lock_guard<std::mutex> lock(sockMutex_);
            if (sock_ < 0)
                return false;
            n = ::sendto(sock_, data.data(), data.size(), MSG_NOSIGNAL,
                         reinterpret_cast<sockaddr*>(&dest), dlen);
        }
        if (n < 0) {
            reportError(std::string("udp: sendto failed: ") + strerror(errno));
            return false;
        }
        stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
        stats_.bytesSent.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
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

    /// Setup only: bindAddress_/port_/remoteHost_/remotePort_ are read unguarded
    /// by openAndBind()/updateRemote(), so this must complete before start() and
    /// must not run concurrently with itself. (The resolved address it produces
    /// *is* guarded — peerMutex_ covers remoteAddr_/remoteSet_.)
    bool configure(const std::string& key, const std::string& value) override {
        if (key == "bind") {
            bindAddress_ = value;
            return true;
        }
        if (key == "port") {
            try {
                port_ = static_cast<uint16_t>(std::stoul(value));
                return true;
            } catch (...) { return false; }
        }
        if (key == "remote_host") {
            remoteHost_ = value;
#if defined(AXONVEX_PLATFORM_LINUX)
            updateRemote();
#endif
            return true;
        }
        if (key == "remote_port") {
            try {
                remotePort_ = static_cast<uint16_t>(std::stoul(value));
#if defined(AXONVEX_PLATFORM_LINUX)
                updateRemote();
#endif
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
    bool openAndBind() {
        sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0)
            return false;
        int on = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        // Bounded blocking so recvLoop can observe running_ == false and exit
        // on its own. This is what lets stop() join before closing the fd; the
        // ceiling is that stop() may take up to RECV_TIMEOUT_MS to return.
        timeval tv{};
        tv.tv_sec = RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
        ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, bindAddress_.c_str(), &addr.sin_addr) <= 0) {
            ::close(sock_);
            sock_ = -1;
            return false;
        }
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(sock_);
            sock_ = -1;
            return false;
        }
        updateRemote();
        return true;
    }

    void updateRemote() {
        std::lock_guard<std::mutex> lock(peerMutex_);
        if (remoteHost_.empty() || remotePort_ == 0) {
            remoteSet_ = false;
            return;
        }
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(remotePort_);
        if (::inet_pton(AF_INET, remoteHost_.c_str(), &dest.sin_addr) <= 0) {
            remoteSet_ = false;
            return;
        }
        remoteAddr_ = dest;
        remoteSet_ = true;
    }

    void recvLoop() {
        // sock_ is stable for the whole loop: it is assigned in openAndBind()
        // before this thread is created and only cleared in stop() after the
        // join, so both edges are ordered by the thread handoff itself.
        const int fd = sock_;
        std::array<uint8_t, 2048> buf{};
        while (running_.load()) {
            sockaddr_in peer{};
            socklen_t plen = sizeof(peer);
            ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0,
                                   reinterpret_cast<sockaddr*>(&peer), &plen);
            if (n < 0) {
                // SO_RCVTIMEO expiry: no packet, just re-check running_.
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                reportError(std::string("udp: recvfrom error: ") + strerror(errno));
                break;
            }
            if (n == 0)
                continue;
            {
                std::lock_guard<std::mutex> lock(peerMutex_);
                lastPeer_ = peer;
                lastPeerSet_ = true;
            }
            stats_.bytesReceived.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            stats_.messagesReceived.fetch_add(1, std::memory_order_relaxed);
            std::vector<uint8_t> data(buf.begin(), buf.begin() + n);
            {
                std::lock_guard<std::mutex> lock(cbMutex_);
                this->callCallbacksByKey(defaultKey(), data);
            }
        }
    }

    void reportError(const std::string& msg) {
        std::lock_guard<std::mutex> lock(cbMutex_);
        errorKeyed_.callCallbacksByKey(defaultKey(), msg);
    }
#endif

    std::string bindAddress_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    MessageCallback* defaultMsgCb_{nullptr};

    axonvex::core::CallerKeyed<std::string, std::string> errorKeyed_;
    std::unique_ptr<ErrorHandler> errorAdapter_;

    mutable std::mutex cbMutex_;
    AtomicProtocolStatistics stats_{};

    // Remote destination configuration
    std::string remoteHost_;
    uint16_t remotePort_{0};

    /// Guards sock_ against being closed while send() is inside sendto().
    mutable std::mutex sockMutex_;
    /// Guards the peer/remote address state shared by send(), recvLoop() and
    /// configure()->updateRemote().
    mutable std::mutex peerMutex_;

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
    sockaddr_in remoteAddr_{};
    bool remoteSet_{false};
    sockaddr_in lastPeer_{};
    bool lastPeerSet_{false};
#endif
};

} // namespace axonvex::interfaces::udp
