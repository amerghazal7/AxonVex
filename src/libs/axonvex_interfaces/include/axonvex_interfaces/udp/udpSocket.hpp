#pragma once

#include <array>
#include <atomic>
#include <axonvex_interfaces/detail/addressResolver.hpp>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
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

    /// Without this, destroying a still-running socket runs ~std::thread on a
    /// joinable thread, which calls std::terminate.
    ~UdpSocket() override {
        UdpSocket::stop(); // qualified: no virtual dispatch during destruction
        if (worker_.joinable()) {
            // Only reachable when the destructor itself runs on the worker
            // thread, i.e. the object is being destroyed from its own callback.
            // That is not a supported lifecycle; detaching at least avoids
            // std::terminate here.
            worker_.detach();
        }
    }

    bool start() override {
        // Serialises the whole lifecycle. `running_` alone was check-then-act:
        // two concurrent stop() calls could both pass the guard and both reach
        // worker_.join(), and joining an already-joined thread is UB (C33).
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
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
        // No sockets on this platform. Reporting success and spinning a thread
        // that does nothing made every caller believe it had a live transport
        // (C24); fail honestly instead.
        reportError("udp: not implemented on this platform");
        return false;
#endif
    }

    void stop() override {
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        // No early return on running_: after a deferred self-stop (below) the
        // flag is already false while the thread and fd still need reclaiming.
        // Every step below is individually idempotent instead.
        running_.store(false);
        // Join BEFORE closing: closing an fd that another thread is blocked in
        // recvfrom() on is a use-after-close — the number can be handed straight
        // back out by the next socket()/open() and the blocked call then reads a
        // stranger's fd. recvLoop notices running_ within RECV_TIMEOUT_MS via
        // SO_RCVTIMEO, so it exits on its own without the close (C24).
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                // stop() was called from a message or error callback, and those
                // run on the worker thread itself. Joining here is a self-join:
                // it throws system_error("Resource deadlock avoided"), and from
                // inside a callback that escapes recvLoop and the thread entry
                // lambda, so std::terminate takes the whole process down.
                //
                // running_ is already false, so the loop exits as soon as this
                // callback returns. The join and the close are deferred to the
                // destructor or to a later stop() from another thread, both of
                // which will find the worker already finished.
                return;
            }
            worker_.join();
        }
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
        detail::ResolvedAddress dest;
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            if (remoteSet_) {
                dest = remoteAddr_;
            } else if (lastPeerSet_) {
                dest = lastPeer_;
            }
        }
        if (dest.length == 0) {
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
            n = ::sendto(sock_, data.data(), data.size(), MSG_NOSIGNAL, dest.addr(), dest.length);
        }
        if (n < 0) {
            reportError(std::string("udp: sendto failed: ") + strerror(errno));
            return false;
        }
        stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
        stats_.bytesSent.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        return true;
#else
        // Looping the payload straight back to the local callbacks is not a
        // send; it counted bytes that never left the process (C24).
        (void)data;
        reportError("udp: not implemented on this platform");
        return false;
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
        std::vector<detail::ResolvedAddress> candidates;
        std::string resolveError;
        if (!detail::resolveAddresses(bindAddress_, port_, SOCK_DGRAM, /*passive=*/true, candidates,
                                      resolveError)) {
            reportError("udp: " + resolveError);
            return false;
        }

        // A host with both an A and an AAAA record yields both; keep the first
        // that actually binds rather than assuming the family.
        for (const auto& candidate : candidates) {
            int fd = ::socket(candidate.family, SOCK_DGRAM, candidate.protocol);
            if (fd < 0) {
                continue;
            }
            int on = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

            // Bounded blocking so recvLoop can observe running_ == false and exit
            // on its own. This is what lets stop() join before closing the fd; the
            // ceiling is that stop() may take up to RECV_TIMEOUT_MS to return.
            timeval tv{};
            tv.tv_sec = RECV_TIMEOUT_MS / 1000;
            tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            if (::bind(fd, candidate.addr(), candidate.length) == 0) {
                sock_ = fd;
                sockFamily_ = candidate.family;
                updateRemote();
                return true;
            }
            ::close(fd);
        }
        reportError("udp: no resolved address for " + bindAddress_ + ":" + std::to_string(port_) +
                    " could be bound");
        return false;
    }

    /// Called twice on purpose when remote_host is configured before start():
    /// once from configure(), when sockFamily_ is still AF_UNSPEC and the family
    /// preference cannot be applied, and again from openAndBind() once the
    /// socket's real family is known, which overwrites that first guess.
    void updateRemote() {
        std::vector<detail::ResolvedAddress> candidates;
        std::string resolveError;
        bool resolved = false;
        detail::ResolvedAddress chosen;

        if (!remoteHost_.empty() && remotePort_ != 0 &&
            detail::resolveAddresses(remoteHost_, remotePort_, SOCK_DGRAM, /*passive=*/false,
                                     candidates, resolveError)) {
            // The destination must match the bound socket's family — an IPv6
            // address cannot be sent from an IPv4 socket. Before the bind
            // (sockFamily_ == AF_UNSPEC) any candidate will do.
            for (const auto& candidate : candidates) {
                if (sockFamily_ == AF_UNSPEC || candidate.family == sockFamily_) {
                    chosen = candidate;
                    resolved = true;
                    break;
                }
            }
            if (!resolved) {
                reportError("udp: " + remoteHost_ +
                            " resolved, but to no address in the bound "
                            "socket's family");
            }
        } else if (!remoteHost_.empty() && remotePort_ != 0) {
            reportError("udp: " + resolveError);
        }

        std::lock_guard<std::mutex> lock(peerMutex_);
        remoteSet_ = resolved;
        if (resolved) {
            remoteAddr_ = chosen;
        }
    }

    void recvLoop() {
        // sock_ is stable for the whole loop: it is assigned in openAndBind()
        // before this thread is created and only cleared in stop() after the
        // join, so both edges are ordered by the thread handoff itself.
        const int fd = sock_;
        std::array<uint8_t, 2048> buf{};
        while (running_.load()) {
            sockaddr_storage peer{};
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
                lastPeer_.storage = peer;
                lastPeer_.length = plen;
                lastPeer_.family = peer.ss_family;
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

#endif

    /// Outside the platform guard: the non-Linux build needs it to report that
    /// it cannot do the operation at all.
    void reportError(const std::string& msg) {
        std::lock_guard<std::mutex> lock(cbMutex_);
        errorKeyed_.callCallbacksByKey(defaultKey(), msg);
    }

    std::string bindAddress_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    /// Serialises start()/stop() so only one caller ever tears the thread down.
    mutable std::mutex lifecycleMutex_;

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
    /// Guards the resolved peer/remote addresses. It exists for the steady-state
    /// race — recvLoop() writes lastPeer_ on every datagram while send() reads
    /// it — NOT to make configure() a runtime-reconfiguration API. configure()
    /// is setup-only (see its comment); it resolves through getaddrinfo, which
    /// blocks on DNS for as long as the system resolver takes.
    mutable std::mutex peerMutex_;

#if defined(AXONVEX_PLATFORM_LINUX)
    int sock_{-1};
    /// Family the socket was actually bound with; destinations must match it.
    int sockFamily_{AF_UNSPEC};
    detail::ResolvedAddress remoteAddr_{};
    bool remoteSet_{false};
    detail::ResolvedAddress lastPeer_{};
    bool lastPeerSet_{false};
#endif
};

} // namespace axonvex::interfaces::udp
