// Regression tests for two callback-locking defects in the axonvex_net
// transports (phase2/net review blockers):
//
// (1) TcpClient::setErrorCallback() / UdpSocket::setErrorCallback() held
//     cbMutex_ and then called the locking public registerErrorHandler()
//     override — a 100%-reproducible self-deadlock on the first call, on a
//     mandatory ProtocolInterface API that had zero coverage.
//
// (2) TcpClient::start() / UdpSocket::start() dispatched connect/bind-failure
//     errors while holding lifecycleMutex_, so an error handler reacting with
//     stop() or start() (both supported reentrancy patterns) deadlocked on
//     the non-recursive mutex. Errors must be built under the lock and
//     dispatched after it is released (CLAUDE.md rule 3).

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_net/tcp/tcpClient.hpp>
#include <axonvex_net/udp/udpSocket.hpp>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <net/transportTestUtils.hpp>
#include <string>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)

namespace {

using testnet::finishesWithin;

/// Error handler that runs a caller-supplied reaction exactly once. The
/// once-guard matters: a reaction that calls start() on an unreachable
/// endpoint triggers further error reports, which must not re-enter the
/// reaction (that would recurse forever regardless of any transport fix).
class ReactingErrorHandler final : public axonvex::core::Callback<std::string> {
  public:
    std::function<void()> reaction;
    std::atomic<int> count{0};

    void callbackPerform(const std::string) override {
        ++count;
        if (reaction) {
            std::function<void()> once;
            once.swap(reaction);
            once();
        }
    }
};

} // namespace

// Blocker 1, TCP shape: setErrorCallback() must not self-deadlock, must
// actually deliver errors to the callback, and clearing it must both not
// deadlock and stop delivery (pre-fix the clear path also left a dangling
// adapter pointer registered in errorKeyed_).
TEST(TransportCallbackApiTest, TcpSetErrorCallbackRegistersWithoutDeadlockAndFires) {
    std::atomic<int> errors{0};
    const bool finished = finishesWithin(
        [&errors]() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", 65531);
            client.setErrorCallback([&errors](const std::string&) { ++errors; });
            // Never started: send() refuses loudly through the new callback.
            EXPECT_FALSE(client.send({1, 2, 3}));
            EXPECT_GE(errors.load(), 1) << "setErrorCallback() callback never fired";

            client.setErrorCallback(nullptr); // must not deadlock or dangle
            const int before = errors.load();
            EXPECT_FALSE(client.send({4, 5, 6}));
            EXPECT_EQ(errors.load(), before) << "cleared callback still receives errors";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished) << "TcpClient::setErrorCallback() self-deadlocked";
}

// Blocker 1, UDP shape: identical defect, identical contract.
TEST(TransportCallbackApiTest, UdpSetErrorCallbackRegistersWithoutDeadlockAndFires) {
    std::atomic<int> errors{0};
    const bool finished = finishesWithin(
        [&errors]() {
            axonvex::interfaces::udp::UdpSocket sock("127.0.0.1", 0);
            sock.setErrorCallback([&errors](const std::string&) { ++errors; });
            // No destination configured: send() refuses loudly.
            EXPECT_FALSE(sock.send({1, 2, 3}));
            EXPECT_GE(errors.load(), 1) << "setErrorCallback() callback never fired";

            sock.setErrorCallback(nullptr);
            const int before = errors.load();
            EXPECT_FALSE(sock.send({4, 5, 6}));
            EXPECT_EQ(errors.load(), before) << "cleared callback still receives errors";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished) << "UdpSocket::setErrorCallback() self-deadlocked";
}

// Blocker 2, TCP shape: a connect failure is reported on the caller's thread;
// a handler that reacts by calling stop() then start() must not deadlock on
// lifecycleMutex_ (pre-fix: start() still held it during the dispatch).
TEST(TransportCallbackApiTest, TcpConnectFailureHandlerMayCallStopAndStart) {
    const bool finished = finishesWithin(
        []() {
            // Port 1 (tcpmux): nothing listens there on a test host, so
            // connect() fails fast with ECONNREFUSED — the error path under test.
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", 1);
            ReactingErrorHandler err;
            err.reaction = [&client]() {
                client.stop();
                (void)client.start(); // fails again; must not deadlock either
            };
            client.registerErrorHandler("default", &err);
            EXPECT_FALSE(client.start());
            EXPECT_GE(err.count.load(), 1) << "connect failure was never reported";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished)
        << "error handler calling stop()/start() deadlocked on TcpClient::lifecycleMutex_";
}

// Blocker 2, UDP shape: same for a bind failure.
TEST(TransportCallbackApiTest, UdpBindFailureHandlerMayCallStopAndStart) {
    const bool finished = finishesWithin(
        []() {
            // 192.0.2.1 (TEST-NET-1, RFC 5737) is never a local interface
            // address, so bind() fails with EADDRNOTAVAIL — deterministically
            // and without DNS (numeric literal).
            axonvex::interfaces::udp::UdpSocket sock("192.0.2.1", 0);
            ReactingErrorHandler err;
            err.reaction = [&sock]() {
                sock.stop();
                (void)sock.start();
            };
            sock.registerErrorHandler("default", &err);
            EXPECT_FALSE(sock.start());
            EXPECT_GE(err.count.load(), 1) << "bind failure was never reported";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished)
        << "error handler calling stop()/start() deadlocked on UdpSocket::lifecycleMutex_";
}

// Reviewer note (silent-false mirror): UDP send() with a configured
// destination but no open socket used to return a silent false; it must
// refuse loudly like every other refusal on the send path.
TEST(TransportCallbackApiTest, UdpSendBeforeStartRefusesLoudly) {
    axonvex::interfaces::udp::UdpSocket sock;
    ReactingErrorHandler err;
    sock.registerErrorHandler("default", &err);
    ASSERT_TRUE(sock.configure("remote_host", "127.0.0.1"));
    ASSERT_TRUE(sock.configure("remote_port", "9"));

    const int before = err.count.load();
    EXPECT_FALSE(sock.send({1, 2, 3}));
    EXPECT_GT(err.count.load(), before) << "the refusal must be reported, not swallowed";
}

// Recursion hazard introduced by the loud send-refusal (item 5/6): send() on
// a dead/unopened connection now calls reportError(), so a handler that
// reacts to the report by calling send() again re-enters reportError() from
// inside the still-running dispatch. Unbounded, that is
// reportError -> handler -> send -> reportError forever -> stack overflow;
// the old silent `return false` broke the cycle by construction. The handler
// here retries send() exactly once (its own once-guard, not the fix under
// test), which is enough to observe the fix: with the recursion guard, that
// nested reportError() call is suppressed and the handler fires exactly
// once; without it, the nested call re-enters the handler and it fires
// twice. (An always-retrying handler would be the "real" unbounded case, but
// that intentionally crashes the process via stack overflow and has no place
// in a CI-run suite — see the standalone repro in the task evidence instead.)
TEST(TransportCallbackApiTest, TcpSendRefusalHandlerRetryDoesNotReenterDispatch) {
    const bool finished = finishesWithin(
        []() {
            axonvex::interfaces::tcp::TcpClient client("127.0.0.1", 65534);
            ReactingErrorHandler err;
            err.reaction = [&client]() {
                (void)client.send({9, 9, 9}); // dead connection; retries the refusal
            };
            client.registerErrorHandler("default", &err);
            EXPECT_FALSE(client.send({1, 2, 3}));
            EXPECT_EQ(err.count.load(), 1)
                << "the nested send()'s report must be suppressed by the recursion guard";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished) << "send-refusal handler retry was not bounded";
}

// UDP shape of the same hazard: send() with no destination configured calls
// reportError(); a handler retrying send() must not re-enter the dispatch.
TEST(TransportCallbackApiTest, UdpSendRefusalHandlerRetryDoesNotReenterDispatch) {
    const bool finished = finishesWithin(
        []() {
            axonvex::interfaces::udp::UdpSocket sock("127.0.0.1", 0);
            ReactingErrorHandler err;
            err.reaction = [&sock]() {
                (void)sock.send({9, 9, 9}); // still no destination configured
            };
            sock.registerErrorHandler("default", &err);
            EXPECT_FALSE(sock.send({1, 2, 3}));
            EXPECT_EQ(err.count.load(), 1)
                << "the nested send()'s report must be suppressed by the recursion guard";
        },
        std::chrono::milliseconds(8000));
    ASSERT_TRUE(finished) << "send-refusal handler retry was not bounded";
}

#endif // AXONVEX_PLATFORM_LINUX
