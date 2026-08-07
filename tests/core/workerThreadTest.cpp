/**
 * @file workerThreadTest.cpp
 * @brief Unit tests for detail::WorkerThread (Phase 2 core decomposition, step 3)
 *
 * Pins the thread-id publish/clear (C41/C42), join-before-assign (C39), and
 * self-join-refusal (C33-shape) discipline extracted from system.cpp's
 * eventProcessingThread_/monitoringThread_ and timingController.cpp's
 * schedulerThread_ (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.2), plus the C46 concurrent start()/join() serialization added
 * while fixing the Part 1 lifecycle hang.
 */

#include <atomic>
#include <axonvex_core/detail/workerThread.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <thread>

using namespace axonvex::core::detail;
using namespace std::chrono_literals;

namespace {

// Polls `pred` against a steady_clock deadline instead of a fixed sleep --
// this project's no-sleep-based-synchronization rule.
template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

TEST(WorkerThreadTest, StartRunsLoopAndJoinWaitsForIt) {
    WorkerThread worker;
    std::atomic<bool> ran{false};

    worker.start([&ran] { ran.store(true); });

    EXPECT_TRUE(worker.join());
    EXPECT_TRUE(ran.load());
    EXPECT_FALSE(worker.joinable());
}

TEST(WorkerThreadTest, IsOnThisThreadTrueOnlyInsideTheLoop) {
    WorkerThread worker;
    std::atomic<bool> observedTrueInsideLoop{false};
    std::atomic<bool> loopStarted{false};
    std::atomic<bool> mayExit{false};

    worker.start([&] {
        observedTrueInsideLoop.store(worker.isOnThisThread());
        loopStarted.store(true);
        while (!mayExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ASSERT_TRUE(waitFor([&] { return loopStarted.load(); }, 5s));
    EXPECT_TRUE(observedTrueInsideLoop.load());
    EXPECT_FALSE(worker.isOnThisThread()); // this (test) thread is not the worker

    mayExit.store(true);
    EXPECT_TRUE(worker.join());
    // C41/C42: cleared on every exit path, so a later external check is false.
    EXPECT_FALSE(worker.isOnThisThread());
}

// C33-shape: join() called from inside the loop itself (a self-join) must
// refuse -- return false, do nothing -- rather than blocking (join() on
// your own thread throws std::system_error, which would escape the loop's
// entry lambda and terminate the process if this refusal were missing).
TEST(WorkerThreadTest, SelfJoinIsRefusedNotAttempted) {
    WorkerThread worker;
    auto selfJoinResult = std::make_shared<std::atomic<bool>>(true);
    auto attempted = std::make_shared<std::atomic<bool>>(false);
    auto mayExit = std::make_shared<std::atomic<bool>>(false);

    worker.start([&worker, selfJoinResult, attempted, mayExit] {
        selfJoinResult->store(worker.join()); // must be false, not a hang/terminate
        attempted->store(true);
        while (!mayExit->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ASSERT_TRUE(waitFor([&] { return attempted->load(); }, 5s));
    EXPECT_FALSE(selfJoinResult->load());
    EXPECT_TRUE(worker.joinable()) << "self-join must not have reaped the handle";

    mayExit->store(true);
    EXPECT_TRUE(worker.join()); // external join now reaps it
}

// C39: start() must join a stale-but-joinable handle from a PRIOR run
// before assigning a new thread over it -- never observed here as a crash
// (assigning over a joinable std::thread is std::terminate), but this
// pins the join actually completing (the second loop runs and finishes).
TEST(WorkerThreadTest, StartJoinsStaleHandleBeforeAssigningNewOne) {
    WorkerThread worker;
    std::atomic<int> generation{0};

    worker.start([&generation] { generation.fetch_add(1); });
    // Deliberately no join() here -- start() below must reap this handle
    // itself (C39) before the assert makes sense.
    ASSERT_TRUE(waitFor([&] { return generation.load() >= 1; }, 5s));

    worker.start([&generation] { generation.fetch_add(1); });
    EXPECT_TRUE(worker.join());
    EXPECT_EQ(generation.load(), 2);
}

// beforeSpawn runs strictly between the stale-handle join and the new
// thread's creation -- exercised by a caller-owned "started" flag exactly
// like RealTimeScheduler::running_ (see WorkerThread::start()'s doc: a
// first version of the Part 2 migration flipped this flag BEFORE calling
// start(), which let a still-running previous loop observe it "revived"
// and never exit, hanging the internal stale-handle join forever).
TEST(WorkerThreadTest, BeforeSpawnRunsBetweenStaleJoinAndNewThread) {
    WorkerThread worker;
    auto running = std::make_shared<std::atomic<bool>>(false);
    auto observedRunningAtLoopStart = std::make_shared<std::atomic<bool>>(false);

    worker.start(
        [running, observedRunningAtLoopStart] {
            observedRunningAtLoopStart->store(running->load());
        },
        [running] { running->store(true); });

    ASSERT_TRUE(waitFor([&] { return observedRunningAtLoopStart->load(); }, 5s));
    EXPECT_TRUE(worker.join());
}

TEST(WorkerThreadTest, WithHandleRunsOnlyWhileAHandleExists) {
    WorkerThread worker;
    std::atomic<int> callCount{0};

    // No handle yet: no-op, not a crash.
    worker.withHandle([&callCount](std::thread&) { callCount.fetch_add(1); });
    EXPECT_EQ(callCount.load(), 0);

    std::atomic<bool> mayExit{false};
    worker.start([&mayExit] {
        while (!mayExit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    worker.withHandle([&callCount](std::thread&) { callCount.fetch_add(1); });
    EXPECT_EQ(callCount.load(), 1);

    mayExit.store(true);
    EXPECT_TRUE(worker.join());
}

// NOTE: the destructor's detach-instead-of-self-join branch (self-destruct
// on the worker's own thread) is deliberately NOT covered by a test here,
// same as ~RealTimeScheduler/~AxonVexSystem: detaching avoids the
// std::terminate a raw self-join would cause, but the underlying hazard --
// a detached loop can keep executing after the very members it captured
// (including this object's own threadId_) are freed -- remains documented
// UB per those classes' @warning, not a contract this extraction can make
// safe. Exercising it concretely here would just be asserting a
// use-after-free doesn't get caught, which is the wrong thing to pin.

// C46 regression: start()/join() hammered concurrently from two external
// threads must never hang -- see RealTimeScheduler's identical test
// (TimingControllerTest.ConcurrentStartStopDoesNotHang) for the mechanism
// this guards (a torn/stale handle read producing a join() nothing will
// ever signal). A hang here has no natural timeout, so a watchdog aborts
// the process if the racing loop doesn't finish within a generous
// deadline -- CLAUDE.md: "a test whose failure mode is a HANG needs its
// own deadline/watchdog."
TEST(WorkerThreadTest, ConcurrentStartJoinDoesNotHang) {
    std::atomic<bool> finished{false};
    std::thread watchdog([&finished] {
        auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!finished.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!finished.load()) {
            std::fprintf(stderr, "ConcurrentStartJoinDoesNotHang: watchdog deadline exceeded -- "
                                 "start()/join() race hung\n");
            std::abort();
        }
    });

    WorkerThread worker;
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> loopShouldExit{false};

    std::thread starter([&] {
        while (!stopFlag.load()) {
            worker.start([&loopShouldExit] {
                while (!loopShouldExit.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        }
    });
    std::thread joiner([&] {
        while (!stopFlag.load()) {
            loopShouldExit.store(true);
            worker.join();
            loopShouldExit.store(false);
        }
    });

    auto raceDeadline = std::chrono::steady_clock::now() + 1000ms;
    while (std::chrono::steady_clock::now() < raceDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stopFlag.store(true);
    loopShouldExit.store(true);
    starter.join();
    joiner.join();
    worker.join(); // deterministic teardown

    finished.store(true);
    watchdog.join();
    SUCCEED();
}
