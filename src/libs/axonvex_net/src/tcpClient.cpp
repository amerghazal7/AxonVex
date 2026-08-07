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
#include <system_error>

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
    // Checked before lifecycleMutex_, same as in stop(): callbacks run ON the
    // worker thread, and a start() below may have to reap (join) a dead
    // worker — from a callback that join would be a self-join
    // (std::terminate), and merely waiting on lifecycleMutex_ can deadlock
    // against an external stop() that holds it while joining us. A
    // callback-driven restart is therefore refused, loudly.
    if (workerId_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
        reportError("tcp: start() from inside a transport callback is not supported");
        return false;
    }

    // Failure messages are built under lifecycleMutex_ but dispatched only
    // after it is released (the C34/C12 rule): an error handler reacting to a
    // connect failure by calling start() or stop() — both supported — would
    // otherwise self-deadlock on the non-recursive lifecycleMutex_.
    std::vector<std::string> startErrors;
    bool started = false;
    {
        // Serialises the whole lifecycle. `running_` alone was check-then-act:
        // two concurrent stop() calls could both pass the guard and both reach
        // worker_.join(), and joining an already-joined thread is UB (C33).
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        if (running_.load() && connectionAlive_.load(std::memory_order_acquire))
            return true; // already running on a live connection
#if defined(AXONVEX_PLATFORM_LINUX)
        // Reap a dead-but-unjoined worker (framing violation, peer EOF, hard
        // recv/send error, or a deferred self-stop) before reassigning worker_:
        // move-assigning over a joinable std::thread is std::terminate. The join
        // is bounded — with running_ or connectionAlive_ false, recvLoop exits
        // within RECV_TIMEOUT_MS via SO_RCVTIMEO.
        if (worker_.joinable()) {
            running_.store(false);
            worker_.join();
            workerId_.store(std::thread::id(), std::memory_order_release);
        }
        running_.store(false);
        {
            // The dead connection's fd survives until here (not closed by
            // recvLoop) so late send() calls fail on the connectionAlive_ flag,
            // never on a recycled fd number.
            std::lock_guard<std::mutex> lock(sockMutex_);
            if (sock_ >= 0) {
                ::close(sock_);
                sock_ = -1;
            }
        }
        if (!connectSocket(startErrors)) {
            startErrors.push_back("tcp: failed to connect to " + host_ + ":" +
                                  std::to_string(port_));
        } else {
            connectionAlive_.store(true, std::memory_order_release);
            running_.store(true);
            worker_ = std::thread([this]() {
                workerId_.store(std::this_thread::get_id(), std::memory_order_release);
                recvLoop();
                // Terminal exits inside recvLoop already cleared connectionAlive_;
                // this covers the ordinary stop() path so "stopped" and "dead" are
                // the same state for send()/isRunning().
                connectionAlive_.store(false, std::memory_order_release);
                // Cleared by the worker itself on the way out. A thread::id is
                // reusable once its thread has exited, so leaving a stale id here
                // would let an unrelated future thread match it and wrongly skip
                // the join, stranding a joinable std::thread.
                workerId_.store(std::thread::id(), std::memory_order_release);
            });
            started = true;
        }
#else
        // No sockets on this platform. Reporting success and spinning a thread
        // that does nothing made every caller believe it had a live transport
        // (C24); fail honestly instead.
        startErrors.push_back("tcp: not implemented on this platform");
#endif
    }
    for (const auto& msg : startErrors) {
        reportError(msg);
    }
    return started;
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
    // Both flags: running_ is the caller's intent (start()..stop()),
    // connectionAlive_ is the stream's actual state. A connection that died
    // under us (framing violation, peer EOF, hard error) must not keep
    // reporting itself as running — that half-open lie was the 2a bug.
    return running_.load() && connectionAlive_.load(std::memory_order_acquire);
}

bool TcpClient::send(const std::vector<uint8_t>& data) {
#if defined(AXONVEX_PLATFORM_LINUX)
    // Refuse before touching the socket: a dead connection — recvLoop exited
    // (framing violation, peer EOF, recv error), a previous send poisoned the
    // stream, stop() ran, or start() never did — must fail loudly. A silent
    // false was indistinguishable from a transient hiccup (2a).
    if (!connectionAlive_.load(std::memory_order_acquire)) {
        // Ceiling: an error handler that reacts to this report by calling
        // send() again recurses reportError -> handler -> send unboundedly
        // (the connection stays dead). Handlers must not retry send()
        // synchronously; upgrade path is a per-thread reentry guard if a
        // real consumer needs one.
        reportError("tcp: send refused — connection is not open (start() (re)connects)");
        return false;
    }
    if (data.size() > kMaxFrameLength) {
        reportError("tcp: refusing to send " + std::to_string(data.size()) + "-byte frame (max " +
                    std::to_string(kMaxFrameLength) + ")");
        return false;
    }
    // Error text is built under sockMutex_ but dispatched only after it is
    // released: user callbacks never run under a transport lock (the C34/C12
    // rule) — an error handler calling stop() would otherwise self-deadlock
    // on sockMutex_'s close block.
    std::string sendError;
    {
        // Held across the whole frame so the fd cannot be closed mid-frame and
        // so two concurrent senders cannot interleave header and payload
        // bytes. Ceiling: a send that blocks on a full socket buffer blocks
        // every other sender and delays stop()'s close.
        std::lock_guard<std::mutex> lock(sockMutex_);
        if (sock_ < 0) {
            // Belt only (connectionAlive_ implies an open fd) but reachable:
            // stop() can close the fd between the connectionAlive_ load above
            // and this lock. The contract is loud refusal, so report it too —
            // a silent false here contradicted "send() refuses loudly".
            sendError = "tcp: send refused — connection is not open (start() (re)connects)";
        } else {
            const uint32_t len = static_cast<uint32_t>(data.size());
            const uint8_t header[kFrameHeaderSize] = {kFrameMagic0,
                                                      kFrameMagic1,
                                                      static_cast<uint8_t>((len >> 24) & 0xFFu),
                                                      static_cast<uint8_t>((len >> 16) & 0xFFu),
                                                      static_cast<uint8_t>((len >> 8) & 0xFFu),
                                                      static_cast<uint8_t>(len & 0xFFu)};
            ssize_t sent = 0;
            if (!sendAll(header, sizeof(header), sent)) {
                poisonConnectionLocked();
                sendError = "tcp: send failed after " + std::to_string(sent) + " of " +
                            std::to_string(sizeof(header)) +
                            " header bytes: " + std::generic_category().message(errno);
            } else if (!data.empty() && !sendAll(data.data(), data.size(), sent)) {
                // A partial frame write (header out, payload cut short — or a
                // truncated header) leaves the peer waiting for bytes that will
                // never arrive, and a TCP stream has no resync point: a later
                // send() would write its next header straight into that gap.
                // Poisoning the connection (2b) is what keeps that garbage off
                // the wire — every later send() refuses until start() reconnects.
                poisonConnectionLocked();
                sendError = "tcp: send failed after " + std::to_string(sent) + " of " +
                            std::to_string(data.size()) +
                            " payload bytes: " + std::generic_category().message(errno);
            } else {
                stats_.messagesSent.fetch_add(1, std::memory_order_relaxed);
                stats_.bytesSent.fetch_add(static_cast<uint64_t>(kFrameHeaderSize + data.size()),
                                           std::memory_order_relaxed);
            }
        }
    }
    if (!sendError.empty()) {
        reportError(sendError);
        return false;
    }
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
    if (errorAdapter_) {
        // Unregister before destroying: reset() alone would leave a dangling
        // handler pointer in errorKeyed_ for the next reportError to call.
        errorKeyed_.unregisterKeyedCallback(defaultKey(), errorAdapter_.get());
        errorAdapter_.reset();
    }
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
        // Direct, not this->registerErrorHandler(): the public override locks
        // cbMutex_, which this function already holds — calling it here was a
        // guaranteed self-deadlock on the non-recursive mutex.
        errorKeyed_.registerKeyedCallback(defaultKey(), errorAdapter_.get());
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

bool TcpClient::connectSocket(std::vector<std::string>& errorsOut) {
    std::vector<detail::ResolvedAddress> candidates;
    std::string resolveError;
    if (!detail::resolveAddresses(host_, port_, SOCK_STREAM, /*passive=*/false, candidates,
                                  resolveError)) {
        // Collected, not dispatched: the caller (start()) holds lifecycleMutex_
        // here and dispatches after releasing it (C34/C12 rule).
        errorsOut.push_back("tcp: " + resolveError);
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
            // Under sockMutex_: a send() that loaded connectionAlive_ == true
            // just before the previous connection died can still be heading
            // for its sockMutex_ block while start() is already in here
            // reconnecting; without the lock this write would race that read.
            std::lock_guard<std::mutex> lock(sockMutex_);
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

void TcpClient::poisonConnectionLocked() {
    connectionAlive_.store(false, std::memory_order_release);
    // shutdown(), not close(): recvLoop may be blocked in recv() on this fd,
    // and a closed fd number is immediately reusable (same rationale as
    // stop()). This wakes the loop; its condition sees connectionAlive_
    // false, so it exits and the worker becomes reapable by start().
    if (sock_ >= 0)
        ::shutdown(sock_, SHUT_RDWR);
}

void TcpClient::recvLoop() {
    // sock_ is stable for the whole loop: it is assigned in connectSocket()
    // before this thread is created and only cleared in start()/stop() after
    // the join, so both edges are ordered by the thread handoff itself.
    const int fd = sock_;
    std::vector<uint8_t> buffer;
    buffer.reserve(4096);
    std::array<char, 1024> tmp{};
    // connectionAlive_ is in the condition so a send-side poison (2b) ends
    // the loop within RECV_TIMEOUT_MS even if the poison's shutdown() lost a
    // race with this loop re-entering recv().
    while (running_.load() && connectionAlive_.load(std::memory_order_acquire)) {
        ssize_t n = ::recv(fd, tmp.data(), static_cast<int>(tmp.size()), 0);
        if (n == 0) {
            // A shutdown() from stop() or a send-side poison also surfaces as
            // EOF; only report a peer-side close if we did not initiate the
            // teardown ourselves.
            const bool selfInitiated =
                !running_.load() || !connectionAlive_.load(std::memory_order_acquire);
            // Dead before the report, so an error handler that immediately
            // retries send() is already refused (2a).
            connectionAlive_.store(false, std::memory_order_release);
            if (!selfInitiated)
                reportError("tcp: connection closed by peer");
            break;
        }
        if (n < 0) {
            // SO_RCVTIMEO expiry: no data, just re-check the loop condition.
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            const bool selfInitiated =
                !running_.load() || !connectionAlive_.load(std::memory_order_acquire);
            // strerror(errno) is not required to be thread-safe by POSIX (it
            // may format into a shared static buffer); generic_category's
            // message() is the reentrant equivalent (libstdc++ uses
            // strerror_r internally) — this path runs on a per-connection
            // recv thread and a concurrent error on another connection's
            // thread must not race this read (same defect class as C37).
            const std::string reason = std::generic_category().message(errno);
            connectionAlive_.store(false, std::memory_order_release);
            if (!selfInitiated)
                reportError("tcp: recv error: " + reason);
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
        // Re-checked before every frame: a callback invoked by
        // dispatchMessage() below (e.g. for the previous frame) may have
        // called stop() on this thread (C33's supported reentrant-stop
        // case) or poisoned the connection via a failed send(). Do not
        // drain every remaining buffered frame after that — stop() means
        // stop, not "finish the batch".
        while (running_.load() && connectionAlive_.load(std::memory_order_acquire)) {
            const size_t avail = buffer.size() - start;
            if (avail == 0)
                break;
            // Magic is validated as soon as its bytes arrive, not only once
            // a full 6-byte header is buffered: a wrong-protocol peer that
            // sends fewer than kFrameHeaderSize bytes and then waits used to
            // go undiagnosed until EOF.
            if (buffer[start] != kFrameMagic0 ||
                (avail >= 2 && buffer[start + 1] != kFrameMagic1)) {
                // Dead before the report: the connection is unusable from
                // this instant, so send() must already refuse when the error
                // handler (or any other thread) observes the violation (2a).
                connectionAlive_.store(false, std::memory_order_release);
                reportError("tcp: not an AxonVex frame (magic " + toHexByte(buffer[start]) + " " +
                            (avail >= 2 ? toHexByte(buffer[start + 1]) : std::string("??")) +
                            ") — peer speaks a different protocol");
                framingViolation = true;
                break;
            }
            if (avail < kFrameHeaderSize) {
                break; // incomplete header — wait for more bytes
            }
            const uint32_t len = (static_cast<uint32_t>(buffer[start + 2]) << 24) |
                                 (static_cast<uint32_t>(buffer[start + 3]) << 16) |
                                 (static_cast<uint32_t>(buffer[start + 4]) << 8) |
                                 static_cast<uint32_t>(buffer[start + 5]);
            if (len > kMaxFrameLength) {
                connectionAlive_.store(false, std::memory_order_release);
                reportError("tcp: frame length " + std::to_string(len) + " exceeds max " +
                            std::to_string(kMaxFrameLength));
                framingViolation = true;
                break;
            }
            if (avail < kFrameHeaderSize + len) {
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
            // The connection is dead (2a): connectionAlive_ is already false,
            // so isRunning() reports false, send() refuses, and the next
            // start() reaps this worker and reconnects. running_ is
            // deliberately left true — clearing it here would let a
            // concurrent start() believe there is nothing to reap while this
            // thread's handle is still joinable and move-assign over it,
            // which is std::terminate. The socket is left open for
            // start()/stop() to close after the join, so late send() calls
            // fail on the flag, never on a recycled fd number.
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
    // Recursion ceiling: send()'s loud refusal on a dead connection reports
    // through here, so a handler that reacts by calling send() again
    // re-enters reportError() from inside this very dispatch — unbounded,
    // that is reportError -> handler -> send -> reportError forever, i.e. a
    // stack overflow (the old silent `return false` broke the cycle by
    // construction). Per-thread, not per-object: while a TCP error report is
    // dispatching on this thread, a nested TCP error report on the same
    // thread is dropped instead of re-entering the handlers. The nested
    // send()/etc. call still runs and still returns its own failure to its
    // caller — only the re-dispatch is skipped.
    static thread_local bool inDispatch = false;
    if (inDispatch) {
        return;
    }
    inDispatch = true;
    struct DispatchGuard {
        bool& flag;
        ~DispatchGuard() {
            flag = false;
        }
    } guard{inDispatch};

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
