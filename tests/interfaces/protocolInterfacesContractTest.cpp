#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_interfaces/tcp/tcpClient.hpp>
#include <axonvex_interfaces/udp/udpSocket.hpp>
#include <axonvex_interfaces/websocket/websocketServer.hpp>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

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
        ++count;
        last = data;
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
