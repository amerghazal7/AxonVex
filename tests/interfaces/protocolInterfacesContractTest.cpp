#include <algorithm>
#include <array>
#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_interfaces/websocket/websocketServer.hpp>
#include <axonvex_net/tcp/tcpClient.hpp>
#include <axonvex_net/udp/udpSocket.hpp>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

class VectorCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    void callbackPerform(const std::vector<uint8_t> data) override {
        ++count;
        last = data;
    }

    std::atomic<int> count{0};
    std::vector<uint8_t> last;
};

class StringCallback final : public axonvex::core::Callback<std::string> {
  public:
    void callbackPerform(const std::string data) override {
        // `last` must be visible to a reader that has observed the count
        // increment (waitForCount-style polling), so it is written before
        // the seq_cst increment publishes it, not after.
        last = data;
        ++count;
    }

    // Snapshot for a reader that polled `count` to a target: the atomic
    // load below happens-after the write above via count's seq_cst store.
    std::string lastMessage() const {
        return last;
    }

    std::atomic<int> count{0};
    std::string last;
};

} // namespace

TEST(ProtocolInterfacesContractTest, WebSocketPlaceholderLifecycleAndCallback) {
    axonvex::interfaces::websocket::WebSocketServer ws("127.0.0.1", 9099);
    VectorCallback cb;

    ws.setMessageCallback(&cb);
    EXPECT_TRUE(ws.start());
    EXPECT_TRUE(ws.isRunning());

    const std::vector<uint8_t> payload{1, 2, 3, 4};
    EXPECT_TRUE(ws.send(payload));

    auto stats = ws.getStatistics();
    EXPECT_EQ(stats.messagesSent, 1);
    EXPECT_EQ(stats.bytesSent, payload.size());
    EXPECT_EQ(cb.count.load(), 1);
    EXPECT_EQ(cb.last, payload);

    ws.stop();
    EXPECT_FALSE(ws.isRunning());
}

TEST(ProtocolInterfacesContractTest, UdpLifecycleAndStatsContract) {
    axonvex::interfaces::udp::UdpSocket udp("127.0.0.1", 0);
    VectorCallback msgCb;
    StringCallback errCb;

    udp.setMessageCallback(&msgCb);
    udp.registerErrorHandler("default", &errCb);

    EXPECT_TRUE(udp.start());
    EXPECT_TRUE(udp.isRunning());

    // Without remote destination and no prior packet, send should fail on Linux.
    const std::vector<uint8_t> payload{9, 8, 7};
    const bool sent = udp.send(payload);
#if defined(AXONVEX_PLATFORM_LINUX)
    EXPECT_FALSE(sent);
#else
    EXPECT_TRUE(sent);
    EXPECT_EQ(msgCb.count.load(), 1);
#endif

    auto stats = udp.getStatistics();
    EXPECT_EQ(stats.messagesReceived, 0);

    udp.stop();
    EXPECT_FALSE(udp.isRunning());
}

#if defined(AXONVEX_PLATFORM_LINUX)
// C24 regression: both transports resolved addresses with inet_pton(AF_INET,...),
// which parses a numeric IPv4 literal and nothing else — every hostname failed
// and IPv6 was unreachable. Pre-fix all three of these binds returned false.
TEST(ProtocolInterfacesContractTest, UdpResolvesHostnamesAndIpv6) {
    {
        axonvex::interfaces::udp::UdpSocket byName("localhost", 39413);
        EXPECT_TRUE(byName.start()) << "hostname resolution should work";
        byName.stop();
    }
    {
        axonvex::interfaces::udp::UdpSocket byIpv4("127.0.0.1", 39414);
        EXPECT_TRUE(byIpv4.start()) << "IPv4 literals must keep working";
        byIpv4.stop();
    }
    {
        // Skipped rather than failed where the host has no IPv6 loopback.
        axonvex::interfaces::udp::UdpSocket byIpv6("::1", 39415);
        if (!byIpv6.start()) {
            GTEST_SKIP() << "no IPv6 loopback on this host";
        }
        byIpv6.stop();
    }
}

// C24: TcpClient has its own per-candidate socket/connect loop, so the resolver
// rewrite needs coverage there too and not just on the UDP side. A refused
// connection proves resolution succeeded and the loop ran — pre-fix "localhost"
// never got as far as connect(), because inet_pton rejected the name outright.
TEST(ProtocolInterfacesContractTest, TcpResolvesHostnameBeforeConnecting) {
    axonvex::interfaces::tcp::TcpClient byName("localhost", 65533);
    StringCallback errCb;
    byName.registerErrorHandler("default", &errCb);

    EXPECT_FALSE(byName.start()) << "nothing is listening on this port";
    EXPECT_FALSE(byName.isRunning());
    ASSERT_GT(errCb.count.load(), 0);
    // The failure must be the connect, not the name lookup.
    EXPECT_EQ(errCb.last.find("cannot resolve"), std::string::npos)
        << "hostname should resolve; got: " << errCb.last;
}

// C24: an unresolvable name must fail, and say so through the error channel —
// not fail silently or hang on the resolver.
TEST(ProtocolInterfacesContractTest, UdpUnresolvableHostFailsWithError) {
    axonvex::interfaces::udp::UdpSocket udp("invalid.invalid.", 39416);
    StringCallback errCb;
    udp.registerErrorHandler("default", &errCb);

    EXPECT_FALSE(udp.start());
    EXPECT_FALSE(udp.isRunning());
    EXPECT_GT(errCb.count.load(), 0) << "failure must be reported, not swallowed";
}

// C24 regression: stop() used to close the socket while the receive thread was
// blocked in recvfrom() on it, and send() read the fd with no guard at all.
// TSan reported a race on the descriptor itself; the real hazard is worse than a
// torn read — a closed fd number is immediately reusable, so the blocked call
// could end up reading a descriptor that now belongs to something else.
// stop() now joins before closing and send() holds sockMutex_ across sendto.
TEST(ProtocolInterfacesContractTest, UdpConcurrentSendAndStopIsRaceFree) {
    const uint16_t kPort = 39412;

    axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
    VectorCallback rxCb;
    rx.setMessageCallback(&rxCb);
    if (!rx.start()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    axonvex::interfaces::udp::UdpSocket tx("127.0.0.1", 0);
    ASSERT_TRUE(tx.configure("remote_host", "127.0.0.1"));
    ASSERT_TRUE(tx.configure("remote_port", std::to_string(kPort)));
    ASSERT_TRUE(tx.start());

    // Senders keep hammering the socket right up to (and past) the stop, so the
    // close in stop() always has in-flight sendto calls to collide with.
    std::atomic<bool> stopping{false};
    std::atomic<int> sendsAttempted{0};
    std::vector<std::thread> senders;
    for (int t = 0; t < 2; ++t) {
        senders.emplace_back([&tx, &stopping, &sendsAttempted]() {
            const std::vector<uint8_t> payload{1, 2, 3, 4, 5};
            while (!stopping.load(std::memory_order_acquire)) {
                tx.send(payload); // may fail once stop() has closed the fd
                sendsAttempted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Let real traffic flow so the receive thread is genuinely inside recvfrom.
    while (sendsAttempted.load(std::memory_order_relaxed) < 200) {
        std::this_thread::yield();
    }

    tx.stop();
    EXPECT_FALSE(tx.isRunning());

    stopping.store(true, std::memory_order_release);
    for (auto& s : senders) {
        s.join();
    }

    rx.stop();
    EXPECT_FALSE(rx.isRunning());

    // Sending after stop must fail cleanly rather than touch a closed fd.
    EXPECT_FALSE(tx.send(std::vector<uint8_t>{9}));

    // The receiver is on loopback but UDP may still drop; only assert that the
    // counters are self-consistent, never that a specific count arrived.
    const auto rxStats = rx.getStatistics();
    EXPECT_EQ(rxStats.messagesReceived > 0, rxStats.bytesReceived > 0);
}
#endif

#if defined(AXONVEX_PLATFORM_LINUX)
// C33 regression: stop() was check-then-act on running_, so two concurrent
// callers could both pass the guard and both call join() on the same thread —
// joining an already-joined thread is UB. Exactly one must do the teardown.
TEST(ProtocolInterfacesContractTest, ConcurrentStopIsSafe) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        axonvex::interfaces::udp::UdpSocket udp("127.0.0.1", 0);
        ASSERT_TRUE(udp.start());

        std::atomic<int> ready{0};
        std::vector<std::thread> stoppers;
        for (int t = 0; t < 4; ++t) {
            stoppers.emplace_back([&udp, &ready]() {
                ready.fetch_add(1);
                while (ready.load() < 4) {
                    std::this_thread::yield(); // widen the overlap
                }
                udp.stop();
            });
        }
        for (auto& s : stoppers) {
            s.join();
        }
        EXPECT_FALSE(udp.isRunning());
    }
}
#endif

#if defined(AXONVEX_PLATFORM_LINUX)
// C33 regression, second shape: message and error callbacks run ON the worker
// thread, so a callback that calls stop() used to make the worker join itself.
// std::thread::join() on the calling thread throws
// system_error("Resource deadlock avoided"), and from inside a callback that
// throw escapes recvLoop and the thread entry lambda — std::terminate, process
// gone. Pre-fix this test aborted the whole binary rather than failing.
class StopOnMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    axonvex::interfaces::udp::UdpSocket* target{nullptr};
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t>) override {
        ++count;
        if (target) {
            target->stop(); // the exact reentrant call that used to terminate
        }
    }
};

TEST(ProtocolInterfacesContractTest, StopFromCallbackDoesNotSelfJoin) {
    const uint16_t kPort = 39417;

    axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
    StopOnMessageCallback cb;
    cb.target = &rx;
    rx.setMessageCallback(&cb);
    if (!rx.start()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    axonvex::interfaces::udp::UdpSocket tx("127.0.0.1", 0);
    ASSERT_TRUE(tx.configure("remote_host", "127.0.0.1"));
    ASSERT_TRUE(tx.configure("remote_port", std::to_string(kPort)));
    ASSERT_TRUE(tx.start());

    const std::vector<uint8_t> payload{1, 2, 3};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (cb.count.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        tx.send(payload);
        std::this_thread::yield();
    }
    ASSERT_GT(cb.count.load(), 0) << "receiver never got a datagram";

    // The self-stop defers the join; an ordinary stop() from this thread must
    // still complete the teardown rather than skipping it.
    rx.stop();
    EXPECT_FALSE(rx.isRunning());

    tx.stop();
}
#endif

#if defined(AXONVEX_PLATFORM_LINUX)
// C34 regression: the transports dispatched user callbacks while holding
// cbMutex_, so a callback that called back into the transport's own
// registration API deadlocked on a non-recursive mutex — and because dispatch
// runs on the receive thread, that wedged the transport permanently.
//
// Run in a worker with a deadline: a regression here is a hang, not a failed
// assertion, and a hung test blocks CI forever. Polled rather than
// condition-variable based because GCC 11's libtsan does not intercept
// pthread_cond_clockwait and reports bogus races for wait_for.
namespace {
bool finishesWithin(std::function<void()> fn, std::chrono::milliseconds limit) {
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

class ReentrantRegistrationCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    axonvex::interfaces::udp::UdpSocket* target{nullptr};
    VectorCallback extra;
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t>) override {
        if (target) {
            // Both directions of re-entry: registering takes cbMutex_ outright,
            // unregistering additionally waits for dispatch to drain — which is
            // this very dispatch, so it must not wait on itself.
            target->registerMessageHandler("extra", &extra);
            target->unregisterMessageHandler("extra", &extra);
        }
        ++count;
    }
};
} // namespace

TEST(ProtocolInterfacesContractTest, CallbackMayReenterRegistrationApi) {
    const uint16_t kPort = 39418;

    const bool finished = finishesWithin(
        [kPort]() {
            axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
            ReentrantRegistrationCallback cb;
            cb.target = &rx;
            rx.setMessageCallback(&cb);
            if (!rx.start()) {
                return; // port busy; the outer EXPECT below still passes
            }

            axonvex::interfaces::udp::UdpSocket tx("127.0.0.1", 0);
            tx.configure("remote_host", "127.0.0.1");
            tx.configure("remote_port", std::to_string(kPort));
            tx.start();

            const std::vector<uint8_t> payload{7, 7, 7};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (cb.count.load() == 0 && std::chrono::steady_clock::now() < deadline) {
                tx.send(payload);
                std::this_thread::yield();
            }

            tx.stop();
            rx.stop();
        },
        std::chrono::milliseconds(10000));

    EXPECT_TRUE(finished) << "a callback re-entering the registration API deadlocked";
}
#endif

#if defined(AXONVEX_PLATFORM_LINUX)
// C34, second shape: one transport shares its dispatch barrier between the
// message path (receive thread) and the error path (reachable from send() on
// any caller thread), so two threads can be dispatching at once. A barrier that
// remembers only a single "current dispatcher" id then loses track of the
// thread that entered first — that thread fails its own self-check, waits for
// the in-flight count to drain, and the count can never reach zero because its
// own dispatch is the one it is stuck inside.
//
// This test forces exactly that interleaving: the caller thread enters an error
// dispatch FIRST, then waits inside its handler until the receive thread is
// also dispatching, and only then self-unregisters.
namespace {
class OrchestratingErrorHandler final : public axonvex::core::Callback<std::string> {
  public:
    axonvex::interfaces::udp::UdpSocket* target{nullptr};
    std::atomic<bool>* entered{nullptr};
    std::atomic<bool>* workerDispatching{nullptr};
    std::atomic<int> count{0};

    void callbackPerform(const std::string) override {
        if (count.fetch_add(1) != 0) {
            return; // only orchestrate on the first error
        }
        entered->store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!workerDispatching->load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        // Two threads are now inside a dispatch. Pre-fix this blocked forever.
        target->unregisterErrorHandler("default", this);
    }
};

class SlowMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    std::atomic<bool>* workerDispatching{nullptr};
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t>) override {
        ++count;
        workerDispatching->store(true, std::memory_order_release);
        // Stay in dispatch long enough to overlap the caller thread's.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
};
} // namespace

TEST(ProtocolInterfacesContractTest, ConcurrentDispatchFromTwoThreadsDoesNotDeadlock) {
    const uint16_t kPort = 39419;

    const bool finished = finishesWithin(
        [kPort]() {
            std::atomic<bool> entered{false};
            std::atomic<bool> workerDispatching{false};

            axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
            OrchestratingErrorHandler errCb;
            SlowMessageCallback msgCb;
            errCb.target = &rx;
            errCb.entered = &entered;
            errCb.workerDispatching = &workerDispatching;
            msgCb.workerDispatching = &workerDispatching;

            rx.registerErrorHandler("default", &errCb);
            rx.setMessageCallback(&msgCb);
            if (!rx.start()) {
                return;
            }

            // Feeds rx only once the caller thread is already inside its error
            // dispatch, so the two overlap in the order that used to deadlock.
            std::thread feeder([kPort, &entered]() {
                axonvex::interfaces::udp::UdpSocket tx("127.0.0.1", 0);
                tx.configure("remote_host", "127.0.0.1");
                tx.configure("remote_port", std::to_string(kPort));
                if (!tx.start()) {
                    return;
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while (!entered.load(std::memory_order_acquire) &&
                       std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::yield();
                }
                tx.send(std::vector<uint8_t>{4, 2});
                tx.stop();
            });

            // rx has no destination configured and has not received yet, so this
            // fails and drives reportError on THIS thread.
            rx.send(std::vector<uint8_t>{1});

            feeder.join();
            rx.stop();
        },
        std::chrono::milliseconds(15000));

    EXPECT_TRUE(finished) << "concurrent dispatch on two threads deadlocked the barrier";
}
#endif

#if defined(AXONVEX_PLATFORM_LINUX)
namespace {
// Minimal blocking loopback TCP server for framing tests: bind/listen on
// 127.0.0.1, accept exactly one client, then let the test read/write raw
// bytes on the accepted fd. Blocking is fine — every use sits inside a
// finishesWithin() deadline.
class LoopbackTcpServer {
  public:
    explicit LoopbackTcpServer(uint16_t port) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0)
            return;
        int yes = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listenFd_, 1) != 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }
    ~LoopbackTcpServer() {
        if (connFd_ >= 0)
            ::close(connFd_);
        if (listenFd_ >= 0)
            ::close(listenFd_);
    }
    bool valid() const {
        return listenFd_ >= 0;
    }
    bool acceptOne() {
        connFd_ = ::accept(listenFd_, nullptr, nullptr);
        return connFd_ >= 0;
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
    // Reads exactly n bytes or gives up on EOF/error.
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
};

// Frame a payload the way the C11 wire format specifies.
std::vector<uint8_t> framed(const std::vector<uint8_t>& payload) {
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

// Collects every dispatched message; poll `count` against a deadline.
class CollectingCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    std::mutex m;
    std::vector<std::vector<uint8_t>> messages;
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t> data) override {
        {
            std::lock_guard<std::mutex> lock(m);
            messages.push_back(data);
        }
        ++count;
    }
};

bool waitForCount(std::atomic<int>& counter, int target, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (counter.load() < target && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return counter.load() >= target;
}
} // namespace

// C11 regression: newline framing corrupted binary payloads — a 0x0A byte
// inside the data was indistinguishable from the frame boundary, so the
// receiver split the message. The wire format is now magic 0xAF 0x01 +
// u32 BE payload length + payload; this test pins BOTH directions:
// the exact bytes send() puts on the wire, and that a framed payload
// containing 0x0A dispatches intact.
TEST(ProtocolInterfacesContractTest, TcpBinaryPayloadWithNewlineRoundTripsIntact) {
    const uint16_t kPort = 39420;
    LoopbackTcpServer server(kPort);
    if (!server.valid()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    const std::vector<uint8_t> payload{0x01, 0x0A, 0x02, 0x0A, 0x0A, 0x03};

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", kPort);
            CollectingCallback rx;
            client.setMessageCallback(&rx);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // Direction 1: client -> wire. The exact framed bytes, nothing
            // more. A second, empty-payload frame is sent immediately after:
            // readExactly reads past frame 1's boundary, so a stray byte left
            // over from frame 1 (e.g. the old '\n' delimiter) corrupts frame
            // 2's magic in the comparison below instead of going unnoticed
            // past the end of a single-frame read. This also exercises
            // send()'s zero-length-payload skip branch on the send side.
            ASSERT_TRUE(client.send(payload));
            ASSERT_TRUE(client.send({}));
            std::vector<uint8_t> wire;
            ASSERT_TRUE(server.readExactly(6 + payload.size() + 6, wire));
            std::vector<uint8_t> expectedWire = framed(payload);
            const std::vector<uint8_t> secondFrame = framed({});
            expectedWire.insert(expectedWire.end(), secondFrame.begin(), secondFrame.end());
            EXPECT_EQ(wire, expectedWire)
                << "send() did not emit the C11 wire format, or left a stray "
                   "trailing byte that corrupted the following frame";

            // Direction 2: wire -> client. One dispatch, payload intact.
            ASSERT_TRUE(server.writeRaw(framed(payload)));
            ASSERT_TRUE(waitForCount(rx.count, 1, std::chrono::seconds(5)));
            {
                std::lock_guard<std::mutex> lock(rx.m);
                ASSERT_EQ(rx.messages.size(), 1u)
                    << "payload with embedded 0x0A was split into multiple frames";
                EXPECT_EQ(rx.messages[0], payload);
            }
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "framing round-trip wedged";
}

// Frames survive arbitrary TCP segmentation: header and payload dribbled
// byte-wise, then several complete frames in one write, incl. an empty frame.
TEST(ProtocolInterfacesContractTest, TcpFramesSurviveSplitAndCoalescedDelivery) {
    const uint16_t kPort = 39421;
    LoopbackTcpServer server(kPort);
    if (!server.valid()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    const std::vector<uint8_t> a{0xDE, 0xAD};
    const std::vector<uint8_t> b{}; // zero-length frame is legal
    const std::vector<uint8_t> c{0x0A, 0xBE, 0xEF};

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", kPort);
            CollectingCallback rx;
            client.setMessageCallback(&rx);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // Frame `a` one byte at a time (splits inside header AND payload).
            for (uint8_t byte : framed(a)) {
                ASSERT_TRUE(server.writeRaw({byte}));
            }
            // Frames `b` (empty) and `c` coalesced into one write.
            std::vector<uint8_t> burst = framed(b);
            const std::vector<uint8_t> fc = framed(c);
            burst.insert(burst.end(), fc.begin(), fc.end());
            ASSERT_TRUE(server.writeRaw(burst));

            ASSERT_TRUE(waitForCount(rx.count, 3, std::chrono::seconds(5)));
            {
                std::lock_guard<std::mutex> lock(rx.m);
                ASSERT_EQ(rx.messages.size(), 3u);
                EXPECT_EQ(rx.messages[0], a);
                EXPECT_EQ(rx.messages[1], b);
                EXPECT_EQ(rx.messages[2], c);
            }
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "split/coalesced framing wedged";
}

// A non-AxonVex peer (wrong magic — e.g. an old newline-framing peer) is a
// precise loud error, not a garbage dispatch and not a silent drop.
TEST(ProtocolInterfacesContractTest, TcpBadMagicIsLoudErrorAndNothingDispatches) {
    const uint16_t kPort = 39422;
    LoopbackTcpServer server(kPort);
    if (!server.valid()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", kPort);
            CollectingCallback rx;
            StringCallback err; // existing helper in this file: collects error strings
            client.setMessageCallback(&rx);
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            ASSERT_TRUE(server.writeRaw({'H', 'e', 'l', 'l', 'o', '\n'}));
            ASSERT_TRUE(waitForCount(err.count, 1, std::chrono::seconds(5)));
            EXPECT_EQ(rx.count.load(), 0) << "garbage bytes must never dispatch";
            EXPECT_NE(err.lastMessage().find("not an AxonVex frame"), std::string::npos);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "bad-magic handling wedged";
}

// A hostile/corrupt length field must not drive a giant allocation: valid
// magic + a length above kMaxFrameLength is a precise loud error.
TEST(ProtocolInterfacesContractTest, TcpOversizeLengthIsLoudErrorAndNothingDispatches) {
    const uint16_t kPort = 39423;
    LoopbackTcpServer server(kPort);
    if (!server.valid()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", kPort);
            CollectingCallback rx;
            StringCallback err;
            client.setMessageCallback(&rx);
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // 0xFFFFFFFF payload length: far above the 16 MiB limit.
            ASSERT_TRUE(server.writeRaw({0xAF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF}));
            ASSERT_TRUE(waitForCount(err.count, 1, std::chrono::seconds(5)));
            EXPECT_EQ(rx.count.load(), 0);
            EXPECT_NE(err.lastMessage().find("exceeds max"), std::string::npos);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "oversize-length handling wedged";
}

// send() refuses an over-limit payload before touching the socket.
TEST(ProtocolInterfacesContractTest, TcpSendRefusesOverLimitPayload) {
    const uint16_t kPort = 39424;
    LoopbackTcpServer server(kPort);
    if (!server.valid()) {
        GTEST_SKIP() << "port " << kPort << " unavailable on this host";
    }

    // If the size guard ever regresses, client.send(tooBig) below would try
    // to actually write 16 MiB+1 to a peer that never reads it: the kernel
    // send buffer fills and the blocking ::send() (no SO_SNDTIMEO) blocks
    // forever inside sockMutex_. Bound it like its four neighbors so that
    // regression is a fast failure, not a wedged ctest run.
    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", kPort);
            StringCallback err;
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // 16 MiB + 1 of zeros; allocation is fine, the send must refuse it.
            std::vector<uint8_t> tooBig(16u * 1024u * 1024u + 1u, 0);
            EXPECT_FALSE(client.send(tooBig));
            EXPECT_GE(err.count.load(), 1);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "over-limit send refusal wedged";
}
#endif

TEST(ProtocolInterfacesContractTest, TcpStartFailurePathAndConfigurationContract) {
    axonvex::interfaces::tcp::TcpClient tcp("127.0.0.1", 65534);
    StringCallback errCb;
    tcp.registerErrorHandler("default", &errCb);

    EXPECT_TRUE(tcp.configure("host", "127.0.0.1"));
    EXPECT_TRUE(tcp.configure("port", "65534"));
    EXPECT_FALSE(tcp.configure("invalid_key", "value"));

    // No local server expected on this high port during test.
    const bool started = tcp.start();
#if defined(AXONVEX_PLATFORM_LINUX)
    EXPECT_FALSE(started);
    EXPECT_FALSE(tcp.isRunning());
#else
    EXPECT_TRUE(started);
    EXPECT_TRUE(tcp.isRunning());
    tcp.stop();
#endif
}
