#include <atomic>
#include <axonvex/core/callback.hpp>
#include <axonvex/interfaces/tcp/tcpClient.hpp>
#include <axonvex/interfaces/udp/udpSocket.hpp>
#include <axonvex/interfaces/websocket/websocketServer.hpp>
#include <gtest/gtest.h>
#include <string>
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

