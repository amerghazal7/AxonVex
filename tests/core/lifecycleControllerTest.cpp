/**
 * @file lifecycleControllerTest.cpp
 * @brief Unit tests for LifecycleController (Phase 2 core decomposition, step 6)
 *
 * Pins the behavior extracted from AxonVexSystem's
 * currentState_/stateMutex_/isShuttingDown_/shutdownMutex_/transitionState()/
 * isOnWorkerThread() (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.3). AxonVexSystemTest's lifecycle tests continue to pin the
 * façade-level behavior; these tests pin the controller in isolation.
 */

#include <atomic>
#include <axonvex_core/lifecycleController.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// Test fixture wiring LifecycleController's injected callables to plain
/// local state -- no AxonVexSystem -- so these tests pin the controller's
/// own contract, not the façade's.
class LifecycleControllerTest : public ::testing::Test {
  protected:
    std::vector<std::pair<SystemState, SystemState>> transitioned_;
    std::vector<std::pair<SystemState, SystemState>> invalidAttempts_;
    std::atomic<bool> onEventDispatchThread_{false};
    std::atomic<bool> onHealthMonitorThread_{false};
    std::atomic<bool> onSchedulerThread_{false};

    LifecycleController controller_{
        [this](SystemState o, SystemState n) { transitioned_.emplace_back(o, n); },
        [this](SystemState o, SystemState n) { invalidAttempts_.emplace_back(o, n); },
        [this] { return onEventDispatchThread_.load(); },
        [this] { return onHealthMonitorThread_.load(); },
        [this] { return onSchedulerThread_.load(); }};
};

} // namespace

TEST_F(LifecycleControllerTest, InitialStateIsUninitialized) {
    EXPECT_EQ(controller_.state(), SystemState::UNINITIALIZED);
}

TEST_F(LifecycleControllerTest, ValidTransitionSucceedsAndInvokesOnTransitioned) {
    EXPECT_TRUE(controller_.transition(SystemState::INITIALIZING));
    EXPECT_EQ(controller_.state(), SystemState::INITIALIZING);
    ASSERT_EQ(transitioned_.size(), 1u);
    EXPECT_EQ(transitioned_[0].first, SystemState::UNINITIALIZED);
    EXPECT_EQ(transitioned_[0].second, SystemState::INITIALIZING);
    EXPECT_TRUE(invalidAttempts_.empty());
}

TEST_F(LifecycleControllerTest, InvalidTransitionFailsLeavesStateUntouchedAndInvokesHook) {
    // UNINITIALIZED -> RUNNING is not in the table.
    EXPECT_FALSE(controller_.transition(SystemState::RUNNING));
    EXPECT_EQ(controller_.state(), SystemState::UNINITIALIZED);
    ASSERT_EQ(invalidAttempts_.size(), 1u);
    EXPECT_EQ(invalidAttempts_[0].first, SystemState::UNINITIALIZED);
    EXPECT_EQ(invalidAttempts_[0].second, SystemState::RUNNING);
    EXPECT_TRUE(transitioned_.empty());
}

// C7: FATAL_ERROR is legal from ANY state and must never be refused.
TEST_F(LifecycleControllerTest, FatalErrorIsLegalFromAnyState) {
    ASSERT_TRUE(controller_.transition(SystemState::INITIALIZING));
    ASSERT_TRUE(controller_.transition(SystemState::INITIALIZED));
    ASSERT_TRUE(controller_.transition(SystemState::STARTING));
    ASSERT_TRUE(controller_.transition(SystemState::RUNNING));

    EXPECT_TRUE(controller_.transition(SystemState::FATAL_ERROR));
    EXPECT_EQ(controller_.state(), SystemState::FATAL_ERROR);
}

// C7: FATAL_ERROR only accepts a transition to UNINITIALIZED.
TEST_F(LifecycleControllerTest, FatalErrorOnlyAcceptsTransitionToUninitialized) {
    ASSERT_TRUE(controller_.transition(SystemState::FATAL_ERROR));

    EXPECT_FALSE(controller_.transition(SystemState::INITIALIZED));
    EXPECT_EQ(controller_.state(), SystemState::FATAL_ERROR);

    EXPECT_TRUE(controller_.transition(SystemState::UNINITIALIZED));
    EXPECT_EQ(controller_.state(), SystemState::UNINITIALIZED);
}

TEST_F(LifecycleControllerTest, ClearShutdownLatchAndRequestShutdown) {
    EXPECT_FALSE(controller_.isShuttingDown());
    controller_.requestShutdown();
    EXPECT_TRUE(controller_.isShuttingDown());
    controller_.clearShutdownLatch();
    EXPECT_FALSE(controller_.isShuttingDown());
}

// C38: shutdownRequested() folds isShuttingDown() OR'd with the ">=
// STOPPING" state guard into one predicate.
TEST_F(LifecycleControllerTest, ShutdownRequestedTrueWhenLatchSetOrStateAtLeastStopping) {
    EXPECT_FALSE(controller_.shutdownRequested());

    controller_.requestShutdown();
    EXPECT_TRUE(controller_.shutdownRequested());
    controller_.clearShutdownLatch();
    EXPECT_FALSE(controller_.shutdownRequested());

    ASSERT_TRUE(controller_.transition(SystemState::INITIALIZING));
    ASSERT_TRUE(controller_.transition(SystemState::INITIALIZED));
    ASSERT_TRUE(controller_.transition(SystemState::STARTING));
    ASSERT_TRUE(controller_.transition(SystemState::RUNNING));
    ASSERT_TRUE(controller_.transition(SystemState::STOPPING));
    // STOPPING >= STOPPING -- shutdownRequested() must be true even though
    // the latch was never set for this path.
    EXPECT_TRUE(controller_.shutdownRequested());
}

TEST_F(LifecycleControllerTest, IsOnWorkerThreadAggregatesAllThreePredicates) {
    EXPECT_FALSE(controller_.isOnWorkerThread());

    onEventDispatchThread_.store(true);
    EXPECT_TRUE(controller_.isOnWorkerThread());
    onEventDispatchThread_.store(false);
    EXPECT_FALSE(controller_.isOnWorkerThread());

    onHealthMonitorThread_.store(true);
    EXPECT_TRUE(controller_.isOnWorkerThread());
    onHealthMonitorThread_.store(false);

    onSchedulerThread_.store(true);
    EXPECT_TRUE(controller_.isOnWorkerThread());
}

TEST_F(LifecycleControllerTest, AcquireTeardownIsReentrantAcrossSequentialCalls) {
    {
        auto lock = controller_.acquireTeardown();
        EXPECT_TRUE(lock.owns_lock());
    }
    // The first lock's destructor released the mutex; a second acquisition
    // must not block or fail.
    auto lock2 = controller_.acquireTeardown();
    EXPECT_TRUE(lock2.owns_lock());
}

// C2: tryAcquireTeardown() must not block while another thread already
// owns the teardown token -- the shape emergencyShutdown() relies on to
// avoid deadlocking stop()'s join of a thread entering emergencyShutdown.
TEST_F(LifecycleControllerTest, TryAcquireTeardownFailsWhileAnotherThreadHoldsIt) {
    std::atomic<bool> holderAcquired{false};
    std::atomic<bool> mayRelease{false};

    std::thread holder([&] {
        auto lock = controller_.acquireTeardown();
        holderAcquired.store(true);
        while (!mayRelease.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ASSERT_TRUE(waitFor([&] { return holderAcquired.load(); }));

    auto tryLock = controller_.tryAcquireTeardown();
    EXPECT_FALSE(tryLock.owns_lock());

    mayRelease.store(true);
    holder.join();

    auto tryLockAfter = controller_.tryAcquireTeardown();
    EXPECT_TRUE(tryLockAfter.owns_lock());
}

// A blocking acquireTeardown() must wait for a concurrent holder to release
// rather than failing -- the shape stop()/the destructor rely on.
TEST_F(LifecycleControllerTest, AcquireTeardownBlocksUntilConcurrentHolderReleases) {
    std::atomic<bool> holderAcquired{false};
    std::atomic<bool> mayRelease{false};
    std::atomic<bool> secondAcquired{false};

    std::thread holder([&] {
        auto lock = controller_.acquireTeardown();
        holderAcquired.store(true);
        while (!mayRelease.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    ASSERT_TRUE(waitFor([&] { return holderAcquired.load(); }));

    std::thread second([&] {
        auto lock = controller_.acquireTeardown();
        secondAcquired.store(true);
    });

    // The second thread must still be blocked while the holder has not
    // released -- a bounded poll that the state stays false, not a sleep
    // used as a synchronization primitive.
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(secondAcquired.load());

    mayRelease.store(true);
    holder.join();

    ASSERT_TRUE(waitFor([&] { return secondAcquired.load(); }));
    second.join();
}
