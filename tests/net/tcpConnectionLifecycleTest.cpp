// Regression tests for the TCP half-open backlog (plan §8 Phase 2 / §5 item 4):
// every recvLoop exit (framing violation, peer EOF, hard recv/send error) used
// to leave the transport half-open — isRunning() stayed true, send() kept
// returning true, and start() was a no-op — with no way back short of stop().
// Now a dead connection is observable (isRunning() false), send() refuses
// loudly, and start() reaps the dead worker and reconnects.

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_net/tcp/tcpClient.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <net/transportTestUtils.hpp>
#include <string>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)

namespace {

using testnet::finishesWithin;
using testnet::framed;
using testnet::LoopbackTcpServer;
using testnet::waitForCount;
using testnet::waitUntil;

class CountingErrorCallback final : public axonvex::core::Callback<std::string> {
  public:
    void callbackPerform(const std::string data) override {
        {
            std::lock_guard<std::mutex> lock(m);
            messages.push_back(data);
        }
        ++count;
    }
    bool sawSubstring(const std::string& needle) {
        std::lock_guard<std::mutex> lock(m);
        for (const auto& msg : messages) {
            if (msg.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }
    std::mutex m;
    std::vector<std::string> messages;
    std::atomic<int> count{0};
};

class CountingMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    void callbackPerform(const std::vector<uint8_t>) override {
        ++count;
    }
    std::atomic<int> count{0};
};

/// Drives one full round-trip over a freshly accepted connection to prove a
/// restarted client is genuinely usable in both directions.
void expectRoundTripWorks(axonvex::interfaces::tcp::TcpClient& client, LoopbackTcpServer& server,
                          CountingMessageCallback& rx) {
    const std::vector<uint8_t> payload{0xAB, 0xCD, 0xEF};
    const int rxBefore = rx.count.load();
    ASSERT_TRUE(client.send(payload)) << "send on the restarted connection must work";
    std::vector<uint8_t> wire;
    ASSERT_TRUE(server.readExactly(6 + payload.size(), wire));
    EXPECT_EQ(wire, framed(payload));
    ASSERT_TRUE(server.writeRaw(framed(payload)));
    ASSERT_TRUE(waitForCount(rx.count, rxBefore + 1, std::chrono::seconds(5)));
}

} // namespace

// 2a: a framing violation terminates the receive loop; that exit must mark the
// connection dead (isRunning() false), make send() refuse loudly, and leave
// start() able to reap the dead worker and reconnect.
TEST(TcpConnectionLifecycleTest, FramingViolationDeadensConnectionAndRestartRecovers) {
    LoopbackTcpServer server;
    ASSERT_TRUE(server.valid());

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", server.port());
            CountingMessageCallback rx;
            CountingErrorCallback err;
            client.setMessageCallback(&rx);
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // Wrong magic: fatal for the connection, recvLoop exits.
            ASSERT_TRUE(server.writeRaw({'H', 'e', 'l', 'l', 'o', '!'}));
            ASSERT_TRUE(waitForCount(err.count, 1, std::chrono::seconds(5)));
            EXPECT_TRUE(err.sawSubstring("not an AxonVex frame"));

            EXPECT_TRUE(
                waitUntil([&client]() { return !client.isRunning(); }, std::chrono::seconds(5)))
                << "isRunning() must go false once the receive loop is dead";

            const int errorsBefore = err.count.load();
            EXPECT_FALSE(client.send({1, 2, 3})) << "send() must refuse on a dead connection";
            EXPECT_GT(err.count.load(), errorsBefore) << "the refusal must be loud";
            EXPECT_EQ(rx.count.load(), 0);

            // start() reaps the dead worker and reconnects; pre-fix it either
            // returned true without doing anything (running_ still true) or
            // move-assigned over a joinable thread handle (std::terminate).
            ASSERT_TRUE(client.start()) << "restart after a dead connection must work";
            ASSERT_TRUE(server.acceptOne());
            EXPECT_TRUE(client.isRunning());
            expectRoundTripWorks(client, server, rx);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "framing-violation lifecycle wedged";
}

// 2a, EOF shape: a peer close is a recvLoop exit like any other and must not
// leave the transport half-open.
TEST(TcpConnectionLifecycleTest, PeerEofDeadensConnectionAndRestartRecovers) {
    LoopbackTcpServer server;
    ASSERT_TRUE(server.valid());

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", server.port());
            CountingMessageCallback rx;
            CountingErrorCallback err;
            client.setMessageCallback(&rx);
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            server.closeConnection(); // orderly FIN -> client sees EOF
            ASSERT_TRUE(waitForCount(err.count, 1, std::chrono::seconds(5)));
            EXPECT_TRUE(err.sawSubstring("closed by peer"));

            EXPECT_TRUE(
                waitUntil([&client]() { return !client.isRunning(); }, std::chrono::seconds(5)));
            const int errorsBefore = err.count.load();
            EXPECT_FALSE(client.send({9, 9})) << "send() must refuse after peer EOF";
            EXPECT_GT(err.count.load(), errorsBefore);

            ASSERT_TRUE(client.start()) << "restart after peer EOF must work";
            ASSERT_TRUE(server.acceptOne());
            expectRoundTripWorks(client, server, rx);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "peer-EOF lifecycle wedged";
}

// 2b: once a send hits a hard socket error the stream has no resync point (a
// partially written frame cannot be completed), so the connection is poisoned:
// every later send() must refuse until start() reconnects. The peer aborts
// with RST so the failure surfaces on the send path, not as a clean EOF.
TEST(TcpConnectionLifecycleTest, HardSendErrorPoisonsConnectionUntilRestart) {
    LoopbackTcpServer server;
    ASSERT_TRUE(server.valid());

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", server.port());
            CountingMessageCallback rx;
            CountingErrorCallback err;
            client.setMessageCallback(&rx);
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            server.abortConnection(); // RST: the next writes fail hard

            // The RST may take a few sends to surface (the first can land in
            // the kernel buffer); once one send fails, the connection must be
            // dead for good, not "try again and see".
            const std::vector<uint8_t> payload(1024, 0x55);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            bool refused = false;
            while (std::chrono::steady_clock::now() < deadline) {
                if (!client.send(payload)) {
                    refused = true;
                    break;
                }
                std::this_thread::yield();
            }
            ASSERT_TRUE(refused) << "a send on an aborted connection never failed";

            EXPECT_FALSE(client.send(payload)) << "the poisoned connection must stay refused";
            EXPECT_TRUE(
                waitUntil([&client]() { return !client.isRunning(); }, std::chrono::seconds(5)))
                << "a poisoned connection must not report itself as running";

            ASSERT_TRUE(client.start()) << "restart after a poisoned connection must work";
            ASSERT_TRUE(server.acceptOne());
            expectRoundTripWorks(client, server, rx);
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "send-poison lifecycle wedged";
}

// A wrong-protocol peer that sends fewer than kFrameHeaderSize bytes and then
// waits used to go undiagnosed until EOF (the magic was only checked once a
// full 6-byte header was buffered). The magic must be checked as soon as its
// bytes arrive.
TEST(TcpConnectionLifecycleTest, ShortWrongProtocolPrefixIsDiagnosedBeforeEof) {
    LoopbackTcpServer server;
    ASSERT_TRUE(server.valid());

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", server.port());
            CountingErrorCallback err;
            client.registerErrorHandler("default", &err);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            // Two bytes, wrong magic, and the connection deliberately stays
            // open: the violation must be reported anyway.
            ASSERT_TRUE(server.writeRaw({'H', 'i'}));
            ASSERT_TRUE(waitForCount(err.count, 1, std::chrono::seconds(5)))
                << "short wrong-protocol prefix was not diagnosed until EOF";
            EXPECT_TRUE(err.sawSubstring("not an AxonVex frame"));
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "short-prefix diagnosis wedged";
}

// send() before start() (or after stop()) must refuse loudly, not return a
// silent false: a silent false is indistinguishable from a transient error.
TEST(TcpConnectionLifecycleTest, SendWithoutLiveConnectionRefusesLoudly) {
    axonvex::interfaces::tcp::TcpClient client("127.0.0.1", 65532);
    CountingErrorCallback err;
    client.registerErrorHandler("default", &err);

    EXPECT_FALSE(client.send({1, 2, 3}));
    EXPECT_GE(err.count.load(), 1) << "the refusal must be reported, not swallowed";
}

// Thread-teardown checklist item 1: a callback runs ON the worker thread, so a
// start() from inside it could join its own thread while reaping (std::terminate)
// or deadlock against an external stop() holding lifecycleMutex_. It must be
// refused loudly instead.
namespace {
class StartOnMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    axonvex::interfaces::tcp::TcpClient* target{nullptr};
    std::atomic<bool> startResult{true};
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t>) override {
        if (target) {
            startResult.store(target->start());
        }
        ++count;
    }
};
} // namespace

TEST(TcpConnectionLifecycleTest, StartFromCallbackIsRefusedNotDeadlockedOrTerminated) {
    LoopbackTcpServer server;
    ASSERT_TRUE(server.valid());

    const bool finished = finishesWithin(
        [&]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", server.port());
            StartOnMessageCallback cb;
            cb.target = &client;
            client.setMessageCallback(&cb);
            ASSERT_TRUE(client.start());
            ASSERT_TRUE(server.acceptOne());

            ASSERT_TRUE(server.writeRaw(framed({0x01})));
            ASSERT_TRUE(waitForCount(cb.count, 1, std::chrono::seconds(5)));
            EXPECT_FALSE(cb.startResult.load())
                << "start() from a transport callback must be refused";
            client.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "start-from-callback wedged";
}

#endif // AXONVEX_PLATFORM_LINUX
