// Regression tests for the UDP half-open backlog (reviewer follow-up to the
// TCP half-open fix in commit 40230b1): UdpSocket::start() had no reap path
// for a worker that stopped without going through an external stop() — a
// deferred self-stop (stop() called from the worker's own message callback)
// leaves worker_ joinable but not joined. The old start() went straight to
// `worker_ = std::thread(...)`, and move-assigning onto a still-joinable
// std::thread is std::terminate: not a silent failure, a crash. This mirrors
// the connectionAlive_ mechanism TcpClient already has: recvLoop marks the
// connection dead on every exit, isRunning() reflects it, and start() reaps
// the dead worker before reopening.

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_net/udp/udpSocket.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <net/transportTestUtils.hpp>
#include <string>
#include <thread>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)

namespace {

using testnet::finishesWithin;
using testnet::waitUntil;

class SelfStoppingMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    axonvex::interfaces::udp::UdpSocket* target{nullptr};
    std::atomic<int> count{0};

    void callbackPerform(const std::vector<uint8_t>) override {
        ++count;
        if (target) {
            target->stop(); // deferred self-stop: leaves worker_ joinable
        }
    }
};

class CountingMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    void callbackPerform(const std::vector<uint8_t>) override {
        ++count;
    }
    std::atomic<int> count{0};
};

class StartOnMessageCallback final : public axonvex::core::Callback<std::vector<uint8_t>> {
  public:
    axonvex::interfaces::udp::UdpSocket* target{nullptr};
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

// A deferred self-stop leaves worker_ joinable and not yet joined (running_
// is cleared immediately, but the worker's own join/close is left to the
// destructor or an external stop() — see UdpSocket::stop()). start() must
// reap that worker — not move-assign a new std::thread over a joinable one
// (std::terminate) — and the restarted socket's receive path must actually
// be alive, not just report success.
TEST(UdpConnectionLifecycleTest, RestartAfterSelfStopReapsDeadWorker) {
    const uint16_t kPort = 39430;
    const bool finished = finishesWithin(
        [kPort]() {
            axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
            SelfStoppingMessageCallback cb;
            cb.target = &rx;
            rx.setMessageCallback(&cb);
            ASSERT_TRUE(rx.start());

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
            ASSERT_GT(cb.count.load(), 0) << "receiver never got the trigger datagram";

            // The self-stop is deferred: give the worker a bounded window to
            // actually exit recvLoop and clear connectionAlive_ (running_ is
            // already false, so the loop notices on its very next condition
            // check — no RECV_TIMEOUT_MS wait needed).
            ASSERT_TRUE(waitUntil([&rx]() { return !rx.isRunning(); }, std::chrono::seconds(5)))
                << "self-stopped worker never cleared isRunning()";

            // worker_ is joinable but was never joined by an external stop().
            // Pre-fix, start() skipped straight to `worker_ = std::thread(...)`
            // with no joinable check at all: move-assigning over a joinable
            // std::thread is std::terminate.
            ASSERT_TRUE(rx.start()) << "restart after a deferred self-stop must reap, not crash";

            CountingMessageCallback cb2;
            rx.setMessageCallback(&cb2);
            const auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (cb2.count.load() == 0 && std::chrono::steady_clock::now() < deadline2) {
                tx.send(payload);
                std::this_thread::yield();
            }
            EXPECT_GT(cb2.count.load(), 0)
                << "restarted socket's receive path must actually be alive, not just report true";

            tx.stop();
            rx.stop();
        },
        std::chrono::milliseconds(20000));
    EXPECT_TRUE(finished) << "restart-after-self-stop wedged or crashed";
}

// Thread-teardown checklist item 1, UDP shape (mirrors
// TcpConnectionLifecycleTest.StartFromCallbackIsRefusedNotDeadlockedOrTerminated):
// a callback runs ON the worker thread, so a start() from inside it could
// join its own thread while reaping (std::terminate) or deadlock against an
// external stop() holding lifecycleMutex_. It must be refused loudly instead.
TEST(UdpConnectionLifecycleTest, StartFromCallbackIsRefusedNotDeadlockedOrTerminated) {
    const uint16_t kPort = 39431;
    const bool finished = finishesWithin(
        [kPort]() {
            axonvex::interfaces::udp::UdpSocket rx("127.0.0.1", kPort);
            StartOnMessageCallback cb;
            cb.target = &rx;
            rx.setMessageCallback(&cb);
            ASSERT_TRUE(rx.start());

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
            EXPECT_FALSE(cb.startResult.load())
                << "start() from a transport callback must be refused";

            tx.stop();
            rx.stop();
        },
        std::chrono::milliseconds(15000));
    EXPECT_TRUE(finished) << "start-from-callback wedged";
}

#endif // AXONVEX_PLATFORM_LINUX
