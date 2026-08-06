#include <axonvex_net/detail/addressResolver.hpp>
#include <axonvex_net/tcp/tcpClient.hpp>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <array>

namespace axonvex {
namespace interfaces {
namespace tcp {

#if defined(AXONVEX_PLATFORM_LINUX)
namespace {
/// Error-path only: format one byte as "0xNN" for framing diagnostics.
std::string toHexByte(uint8_t b) {
    static const char* digits = "0123456789ABCDEF";
    std::string s = "0x??";
    s[2] = digits[(b >> 4) & 0xF];
    s[3] = digits[b & 0xF];
    return s;
}
} // namespace
#endif

TcpClient::TcpClient(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}

TcpClient::~TcpClient() {
    TcpClient::stop(); // qualified: no virtual dispatch during destruction
    if (worker_.joinable()) {
        // Only reachable when the destructor itself runs on the worker
        // thread, i.e. the object is being destroyed from its own callback.
        // That is not a supported lifecycle; detaching at least avoids
        // std::terminate here.
        worker_.detach();
    }
}

bool TcpClient::start() {
    // Serialises the whole lifecycle. `running_` alone was check-then-act:
    // two concurrent stop() calls could both pass the guard and both reach
    // worker_.join(), and joining an already-joined thread is UB (C33).
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (running_.load())
        return true;
#if defined(AXONVEX_PLATFORM_LINUX)
    if (!connectSocket()) {
        reportError("tcp: failed to connect to " + host_ + ":" + std::to_string(port_));
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
    reportError("tcp: not implemented on this platform");
    return false;
#endif
}

void TcpClient::stop() {
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

bool TcpClient::isRunning() const {
    return running_.load();
}

bool TcpClient::send(const std::vector<uint8_t>& data) {
#if defined(AXONVEX_PLATFORM_LINUX)
    if (data.size() > kMaxFrameLength) {
        reportError("tcp: refusing to send " + std::to_string(data.size()) + "-byte frame (max " +
                    std::to_string(kMaxFrameLength) + ")");
        return false;
    }
    // Held across the whole frame so the fd cannot be closed mid-frame and
    // so two concurrent senders cannot interleave header and payload
    // bytes. Ceiling: a send that blocks on a full socket buffer blocks
    // every other sender and delays stop()'s close.
    std::lock_guard<std::mutex> lock(sockMutex_);
    if (sock_ < 0)
        return false;
    const uint32_t len = static_cast<uint32_t>(data.size());
    const uint8_t header[kFrameHeaderSize] = {kFrameMagic0,
                                              kFrameMagic1,
                                              static_cast<uint8_t>((len >> 24) & 0xFFu),
                                              static_cast<uint8_t>((len >> 16) & 0xFFu),
                                              static_cast<uint8_t>((len >> 8) & 0xFFu),
                                              static_cast<uint8_t>(len & 0xFFu)};
    ssize_t sent = 0;
    ssize_t total = 0;
    if (!sendAll(header, sizeof(header), sent)) {
        reportError("tcp: send failed after " + std::to_string(sent) + " of " +
                    std::to_string(sizeof(header)) +
                    " header bytes: " + std::string(strerror(errno)));
        return false;
    }
    total += sent;
    if (!data.empty()) {
        // Ceiling: if the header above went out whole but this payload
        // write then fails partway, the peer is left waiting for the
        // rest of a frame that will never arrive — there is no resync
        // point on a TCP stream, so a later send() on this connection
        // writes its next header right into that gap. Reachable today
        // only via a hard socket error mid-payload, which means the
        // connection is already broken: the peer's own recvLoop sees
        // that as EOF/error and drops the incomplete trailing frame
        // without dispatching it (no garbage delivered), but the desync
        // itself is not resolved here. Full fix (e.g. closing this
        // socket outright on partial-payload failure) is deferred to
        // the §6 axonvex_net transport work; the receive side documents
        // its equivalent case at the framing-violation break below.
        if (!sendAll(data.data(), data.size(), sent)) {
            reportError("tcp: send failed after " + std::to_string(sent) + " of " +
                        std::to_string(data.size()) +
                        " payload bytes: " + std::string(strerror(errno)));
            return false;
        }
        total += sent;
    }
    stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
    stats_.bytesSent.fetch_add(static_cast<uint64_t>(total), std::memory_order_relaxed);
    return true;
#else
    // Looping the payload straight back to the local callbacks is not a
    // send; it counted bytes that never left the process (C24).
    (void)data;
    reportError("tcp: not implemented on this platform");
    return false;
#endif
}

void TcpClient::setMessageCallback(MessageCallback* cb) {
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

void TcpClient::registerMessageHandler(const std::string& key, MessageCallback* cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    this->registerKeyedCallback(key, cb);
}

bool TcpClient::unregisterMessageHandler(const std::string& key, MessageCallback* cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return this->unregisterKeyedCallback(key, cb);
}

size_t TcpClient::unregisterAllMessageHandlersForKey(const std::string& key) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return this->unregisterAllCallbacksForKey(key);
}

void TcpClient::setErrorCallback(ErrorCallback cb) {
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

void TcpClient::registerErrorHandler(const std::string& key, ErrorHandler* cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    errorKeyed_.registerKeyedCallback(key, cb);
}

bool TcpClient::unregisterErrorHandler(const std::string& key, ErrorHandler* cb) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return errorKeyed_.unregisterKeyedCallback(key, cb);
}

size_t TcpClient::unregisterAllErrorHandlersForKey(const std::string& key) {
    std::unique_lock<std::mutex> lock(cbMutex_);
    dispatch_.waitQuiescent(lock);
    return errorKeyed_.unregisterAllCallbacksForKey(key);
}

bool TcpClient::configure(const std::string& key, const std::string& value) {
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

axonvex::interfaces::ProtocolStatistics TcpClient::getStatistics() const {
    return stats_.snapshot();
}

#if defined(AXONVEX_PLATFORM_LINUX)

bool TcpClient::connectSocket() {
    std::vector<detail::ResolvedAddress> candidates;
    std::string resolveError;
    if (!detail::resolveAddresses(host_, port_, SOCK_STREAM, /*passive=*/false, candidates,
                                  resolveError)) {
        reportError("tcp: " + resolveError);
        return false;
    }

    // A host with both an A and an AAAA record yields both, and only one may
    // be reachable; keep the first that connects.
    for (const auto& candidate : candidates) {
        int fd = ::socket(candidate.family, SOCK_STREAM, candidate.protocol);
        if (fd < 0) {
            continue;
        }

        // Bounded blocking so recvLoop can observe running_ == false and exit on
        // its own, which is what lets stop() join before closing the fd. Ceiling:
        // stop() may take up to RECV_TIMEOUT_MS if shutdown() does not wake it.
        timeval tv{};
        tv.tv_sec = RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (::connect(fd, candidate.addr(), candidate.length) == 0) {
            sock_ = fd;
            return true;
        }
        ::close(fd);
    }
    return false;
}

bool TcpClient::sendAll(const uint8_t* bytes, size_t length, ssize_t& sentOut) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = ::send(sock_, bytes + offset, length - offset, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            sentOut = static_cast<ssize_t>(offset);
            return false;
        }
        if (n == 0) {
            break; // peer will not take more
        }
        offset += static_cast<size_t>(n);
    }
    sentOut = static_cast<ssize_t>(offset);
    return offset == length;
}

void TcpClient::recvLoop() {
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
        // Append, then extract complete frames: 6-byte header (magic +
        // u32 BE payload length) followed by the payload. A framing
        // violation is fatal for the connection — after bad bytes there
        // is no resync point on a TCP stream, and carrying on would
        // dispatch garbage.
        buffer.insert(buffer.end(), tmp.begin(), tmp.begin() + n);
        stats_.bytesReceived.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        size_t start = 0;
        bool framingViolation = false;
        while (buffer.size() - start >= kFrameHeaderSize) {
            if (!running_.load()) {
                // A callback invoked by dispatchMessage() below (e.g. the
                // frame just before this one) may have called stop() on
                // this thread (C33's supported reentrant-stop case). Do
                // not drain every remaining buffered frame after that —
                // stop() means stop, not "finish the batch".
                break;
            }
            if (buffer[start] != kFrameMagic0 || buffer[start + 1] != kFrameMagic1) {
                reportError("tcp: not an AxonVex frame (magic " + toHexByte(buffer[start]) + " " +
                            toHexByte(buffer[start + 1]) + ") — peer speaks a different protocol");
                framingViolation = true;
                break;
            }
            const uint32_t len = (static_cast<uint32_t>(buffer[start + 2]) << 24) |
                                 (static_cast<uint32_t>(buffer[start + 3]) << 16) |
                                 (static_cast<uint32_t>(buffer[start + 4]) << 8) |
                                 static_cast<uint32_t>(buffer[start + 5]);
            if (len > kMaxFrameLength) {
                reportError("tcp: frame length " + std::to_string(len) + " exceeds max " +
                            std::to_string(kMaxFrameLength));
                framingViolation = true;
                break;
            }
            if (buffer.size() - start < kFrameHeaderSize + len) {
                break; // incomplete frame — wait for more bytes
            }
            std::vector<uint8_t> frame(buffer.begin() + start + kFrameHeaderSize,
                                       buffer.begin() + start + kFrameHeaderSize + len);
            stats_.messagesReceived.fetch_add(1, std::memory_order_relaxed);
            dispatchMessage(frame);
            start += kFrameHeaderSize + len;
        }
        if (start > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + start);
        }
        if (framingViolation) {
            // Ceiling: this only exits the receive loop, nothing more.
            // running_ is deliberately left true, the socket is not
            // closed, isRunning() keeps reporting true, and send() keeps
            // returning true — the connection is half-open until the
            // owner calls stop(). Do NOT "fix" by storing
            // running_ = false here: start() would then run
            // worker_ = std::thread(...) over this still-joinable
            // handle, which is std::terminate. The real fix (mark
            // not-running without racing a concurrent start(), refuse
            // send() on a dead connection, have start() reap a
            // joinable-but-dead worker) is deferred to the §6
            // axonvex_net transport work.
            break; // connection is unusable; terminate the receive loop
        }
    }
}

#endif // AXONVEX_PLATFORM_LINUX

void TcpClient::dispatchMessage(const std::vector<uint8_t>& data) {
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

void TcpClient::reportError(const std::string& msg) {
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

} // namespace tcp
} // namespace interfaces
} // namespace axonvex
