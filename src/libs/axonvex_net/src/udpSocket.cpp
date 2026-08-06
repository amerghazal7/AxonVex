#include <axonvex_net/udp/udpSocket.hpp>

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

#include <array>

namespace axonvex {
namespace interfaces {
namespace udp {

UdpSocket::UdpSocket(std::string bindAddress, uint16_t port)
    : bindAddress_(std::move(bindAddress)), port_(port) {}

UdpSocket::~UdpSocket() {
    UdpSocket::stop(); // qualified: no virtual dispatch during destruction
    if (worker_.joinable()) {
        // Only reachable when the destructor itself runs on the worker
        // thread, i.e. the object is being destroyed from its own callback.
        // That is not a supported lifecycle; detaching at least avoids
        // std::terminate here.
        worker_.detach();
    }
}

bool UdpSocket::start() {
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
    worker_ = std::thread([this]() {
        workerId_.store(std::this_thread::get_id(), std::memory_order_release);
        recvLoop();
        // Cleared by the worker itself on the way out. A thread::id is
        // reusable once its thread has exited, so leaving a stale id here
        // would let an unrelated future thread match it and wrongly skip
        // the join, stranding a joinable std::thread.
        workerId_.store(std::thread::id(), std::memory_order_release);
    });
    return true;
#else
    // No sockets on this platform. Reporting success and spinning a thread
    // that does nothing made every caller believe it had a live transport
    // (C24); fail honestly instead.
    reportError("udp: not implemented on this platform");
    return false;
#endif
}

void UdpSocket::stop() {
    running_.store(false);

    // Checked before lifecycleMutex_ and without touching worker_. Two
    // reasons. (1) Callbacks run ON this thread, and an external stop()
    // holds lifecycleMutex_ while waiting to join us — blocking here would
    // deadlock both. (2) start() move-assigns worker_ with no happens-before
    // edge to the child, so the child must not read the std::thread object;
    // the id is published by the child itself instead.
    if (workerId_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
        // Self-stop from a message or error callback. Joining would be a
        // self-join: system_error unwinding out of the thread entry, i.e.
        // std::terminate. The loop exits once this callback returns; the
        // join and close are left to the destructor or an external stop().
        return;
    }

    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    // Every step below is individually idempotent, so a repeat stop() (or
    // one completing a deferred self-stop) is harmless.
    // Join BEFORE closing: closing an fd that another thread is blocked in
    // recvfrom() on is a use-after-close — the number can be handed straight
    // back out by the next socket()/open() and the blocked call then reads a
    // stranger's fd. recvLoop notices running_ within RECV_TIMEOUT_MS via
    // SO_RCVTIMEO, so it exits on its own without the close (C24).
    if (worker_.joinable()) {
        worker_.join();
    }
    workerId_.store(std::thread::id(), std::memory_order_release);
#if defined(AXONVEX_PLATFORM_LINUX)
    std::lock_guard<std::mutex> lock(sockMutex_);
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
#endif
}

bool UdpSocket::isRunning() const {
    return running_.load();
}

bool UdpSocket::send(const std::vector<uint8_t>& data) {
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

void UdpSocket::setMessageCallback(MessageCallback* cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock); // it may unregister the previous handler
    if (defaultMsgCb_) {
        this->unregisterKeyedCallback(defaultKey(), defaultMsgCb_);
        defaultMsgCb_ = nullptr;
    }
    if (cb) {
        this->registerKeyedCallback(defaultKey(), cb);
        defaultMsgCb_ = cb;
    }
}

void UdpSocket::registerMessageHandler(const std::string& key, MessageCallback* cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    this->registerKeyedCallback(key, cb);
}

bool UdpSocket::unregisterMessageHandler(const std::string& key, MessageCallback* cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return this->unregisterKeyedCallback(key, cb);
}

size_t UdpSocket::unregisterAllMessageHandlersForKey(const std::string& key) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return this->unregisterAllCallbacksForKey(key);
}

void UdpSocket::setErrorCallback(ErrorCallback cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock); // it destroys the previous adapter
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

void UdpSocket::registerErrorHandler(const std::string& key, ErrorHandler* cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    errorKeyed_.registerKeyedCallback(key, cb);
}

bool UdpSocket::unregisterErrorHandler(const std::string& key, ErrorHandler* cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return errorKeyed_.unregisterKeyedCallback(key, cb);
}

size_t UdpSocket::unregisterAllErrorHandlersForKey(const std::string& key) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return errorKeyed_.unregisterAllCallbacksForKey(key);
}

bool UdpSocket::configure(const std::string& key, const std::string& value) {
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

axonvex::interfaces::ProtocolStatistics UdpSocket::getStatistics() const {
    return stats_.snapshot();
}

#if defined(AXONVEX_PLATFORM_LINUX)

bool UdpSocket::openAndBind() {
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

void UdpSocket::updateRemote() {
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

void UdpSocket::recvLoop() {
    // sock_ is stable for the whole loop: it is assigned in openAndBind()
    // before this thread is created and only cleared in stop() after the
    // join, so both edges are ordered by the thread handoff itself.
    const int fd = sock_;
    std::array<uint8_t, 2048> buf{};
    while (running_.load()) {
        sockaddr_storage peer{};
        socklen_t plen = sizeof(peer);
        ssize_t n =
            ::recvfrom(fd, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&peer), &plen);
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
        dispatchMessage(data);
    }
}

#endif // AXONVEX_PLATFORM_LINUX

void UdpSocket::dispatchMessage(const std::vector<uint8_t>& data) {
    std::vector<MessageCallback*> targets;
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        targets = this->snapshotCallbacksForKey(defaultKey());
        if (targets.empty()) {
            return;
        }
        dispatch_.begin();
    }
    detail::ScopedDispatch scope(dispatch_, cbMutex_);
    for (auto* cb : targets) {
        if (cb) {
            cb->callbackPerform(data);
        }
    }
}

void UdpSocket::reportError(const std::string& msg) {
    std::vector<ErrorHandler*> targets;
    {
        std::lock_guard<std::mutex> lock(cbMutex_);
        targets = errorKeyed_.snapshotCallbacksForKey(defaultKey());
        if (targets.empty()) {
            return;
        }
        dispatch_.begin();
    }
    detail::ScopedDispatch scope(dispatch_, cbMutex_);
    for (auto* handler : targets) {
        if (handler) {
            handler->callbackPerform(msg);
        }
    }
}

} // namespace udp
} // namespace interfaces
} // namespace axonvex
