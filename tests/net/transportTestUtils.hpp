#pragma once

// Shared helpers for transport (axonvex_net) tests: an ephemeral-port
// loopback TCP server, C11 frame encoding, and deadline-polling waits
// (no sleep-based synchronization; condition_variable::wait_for is avoided
// because GCC 11's libtsan does not intercept pthread_cond_clockwait and
// reports bogus races).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace testnet {

#if defined(AXONVEX_PLATFORM_LINUX)
/// Minimal blocking loopback TCP server for transport tests: binds 127.0.0.1
/// on an EPHEMERAL port (bind to port 0, read the real port back with
/// getsockname), accepts one client at a time, then lets the test read/write
/// raw bytes on the accepted fd. Ephemeral binding means tests never collide
/// on a hardcoded port, so there is no skip-if-unavailable path. Blocking is
/// fine — every use sits inside a finishesWithin() deadline.
class LoopbackTcpServer {
  public:
    LoopbackTcpServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0)
            return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0; // ephemeral: the kernel picks a free port
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sockaddr_in bound{};
        socklen_t blen = sizeof(bound);
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listenFd_, 1) != 0 ||
            ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&bound), &blen) != 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            return;
        }
        port_ = ntohs(bound.sin_port);
    }
    ~LoopbackTcpServer() {
        closeConnection();
        if (listenFd_ >= 0)
            ::close(listenFd_);
    }
    LoopbackTcpServer(const LoopbackTcpServer&) = delete;
    LoopbackTcpServer& operator=(const LoopbackTcpServer&) = delete;

    /// False only if socket()/bind()/listen() failed outright (resource
    /// exhaustion) — never because a specific port was taken.
    bool valid() const {
        return listenFd_ >= 0;
    }
    uint16_t port() const {
        return port_;
    }

    /// Accepts the next client, dropping any previous connection first, so a
    /// test can exercise a client reconnect against the same server.
    bool acceptOne() {
        closeConnection();
        connFd_ = ::accept(listenFd_, nullptr, nullptr);
        return connFd_ >= 0;
    }

    /// Orderly close (FIN): the peer sees EOF.
    void closeConnection() {
        if (connFd_ >= 0) {
            ::close(connFd_);
            connFd_ = -1;
        }
    }

    /// Abortive close (RST via SO_LINGER 0): the peer's next send/recv fails
    /// with a hard error instead of a clean EOF.
    void abortConnection() {
        if (connFd_ >= 0) {
            linger lin{};
            lin.l_onoff = 1;
            lin.l_linger = 0;
            ::setsockopt(connFd_, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            ::close(connFd_);
            connFd_ = -1;
        }
    }

    bool writeRaw(const std::vector<uint8_t>& bytes) {
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t n = ::send(connFd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
            if (n <= 0)
                return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }

    /// Reads exactly n bytes or gives up on EOF/error.
    bool readExactly(size_t n, std::vector<uint8_t>& out) {
        out.clear();
        out.reserve(n);
        std::array<uint8_t, 512> tmp{};
        while (out.size() < n) {
            ssize_t got = ::recv(connFd_, tmp.data(), std::min(tmp.size(), n - out.size()), 0);
            if (got <= 0)
                return false;
            out.insert(out.end(), tmp.begin(), tmp.begin() + got);
        }
        return true;
    }

  private:
    int listenFd_{-1};
    int connFd_{-1};
    uint16_t port_{0};
};
#endif // AXONVEX_PLATFORM_LINUX

/// Frame a payload the way the C11 wire format specifies:
/// magic 0xAF 0x01 + u32 BE payload length + payload.
inline std::vector<uint8_t> framed(const std::vector<uint8_t>& payload) {
    const uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> out{0xAF,
                             0x01,
                             static_cast<uint8_t>((len >> 24) & 0xFF),
                             static_cast<uint8_t>((len >> 16) & 0xFF),
                             static_cast<uint8_t>((len >> 8) & 0xFF),
                             static_cast<uint8_t>(len & 0xFF)};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Polls @p pred against a steady_clock deadline. No sleeps beyond yield.
template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return pred();
}

inline bool waitForCount(std::atomic<int>& counter, int target, std::chrono::milliseconds limit) {
    return waitUntil([&counter, target]() { return counter.load() >= target; }, limit);
}

/// Runs @p fn on a worker with a deadline: a lifecycle regression is usually a
/// hang, not a failed assertion, and a hung test blocks CI forever.
inline bool finishesWithin(std::function<void()> fn, std::chrono::milliseconds limit) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([fn, done]() {
        fn();
        done->store(true);
    });
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done->load()) {
        worker.detach(); // wedged; leak it rather than block the suite
        return false;
    }
    worker.join();
    return true;
}

} // namespace testnet
