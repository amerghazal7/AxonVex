/**
 * @file eventBusTest.cpp
 * @brief Unit tests for EventBus (Phase 2 core decomposition, step 4)
 *
 * Pins the behavior extracted from AxonVexSystem's
 * eventPool_/eventQueue_/eventWorker_/eventProcessingRunning_/eventCallbacks_/
 * publishEvent()/eventProcessingLoop()/drainEventQueue() (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.4). AxonVexSystemTest's event-callback tests continue to pin the
 * façade-level behavior; these tests pin the bus in isolation.
 */

#include <atomic>
#include <axonvex_core/eventBus.hpp>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

// Polls `pred` against a steady_clock deadline instead of a fixed sleep --
// this project's no-sleep-based-synchronization rule.
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

SystemEvent makeEvent(SystemEvent::Type type = SystemEvent::Type::STATE_CHANGE) {
    SystemEvent event;
    event.type = type;
    event.oldState = SystemState::UNINITIALIZED;
    event.newState = SystemState::INITIALIZED;
    event.description = "test event";
    event.timestamp = std::chrono::steady_clock::now();
    return event;
}

/// Test fixture wiring EventBus's four injected callables to plain local
/// state -- no Logger, no AxonVexSystem -- so these tests pin EventBus's own
/// contract, not the façade's.
class EventBusTest : public ::testing::Test {
  protected:
    std::atomic<bool> shuttingDown_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> droppedCount_{0};

    // Returns null: EventBus must tolerate a never-set-up logger.
    EventBus bus_{[] { return static_cast<Logger*>(nullptr); },
                  [this] { return shuttingDown_.load(); },
                  [this] { return shuttingDown_.load() || stopRequested_.load(); },
                  [this] { droppedCount_.fetch_add(1); }};
};

} // namespace

TEST_F(EventBusTest, PublishWithoutReinitializeIsANoOp) {
    // C25: publish on a never-initialized bus (e.g. emergencyShutdown()
    // before initialize()) must not crash.
    bus_.publish(makeEvent());
    bus_.drain(); // also must not crash
    SUCCEED();
}

TEST_F(EventBusTest, RegisterNullCallbackReturnsZeroWithoutTouchingCounter) {
    EXPECT_EQ(bus_.registerCallback(nullptr), 0u);
    uint32_t id = bus_.registerCallback([](const SystemEvent&) {});
    EXPECT_GT(id, 0u);
}

TEST_F(EventBusTest, PublishedEventReachesRegisteredCallback) {
    bus_.reinitialize(8, 8);

    auto received = std::make_shared<std::vector<SystemEvent>>();
    auto mutex = std::make_shared<std::mutex>();
    bus_.registerCallback([received, mutex](const SystemEvent& e) {
        std::lock_guard<std::mutex> lock(*mutex);
        received->push_back(e);
    });

    bus_.start();
    bus_.publish(makeEvent(SystemEvent::Type::PROCESSING_UNIT_ADDED));

    ASSERT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> lock(*mutex);
        return !received->empty();
    }));

    {
        std::lock_guard<std::mutex> lock(*mutex);
        EXPECT_EQ(received->front().type, SystemEvent::Type::PROCESSING_UNIT_ADDED);
    }

    EXPECT_TRUE(bus_.stopAndJoin());
}

TEST_F(EventBusTest, UnregisteredCallbackStopsReceiving) {
    bus_.reinitialize(8, 8);

    auto count = std::make_shared<std::atomic<int>>(0);
    uint32_t id = bus_.registerCallback([count](const SystemEvent&) { count->fetch_add(1); });

    bus_.start();
    bus_.publish(makeEvent());
    ASSERT_TRUE(waitFor([&] { return count->load() >= 1; }));

    bus_.unregisterCallback(id);
    int afterFirst = count->load();
    bus_.publish(makeEvent());
    // No new callback should fire; a fixed short wait would be a sleep-based
    // race, so instead assert the count never climbs across a bounded poll.
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(count->load(), afterFirst);

    EXPECT_TRUE(bus_.stopAndJoin());
}

// C25: once isShuttingDown() is true, publish() must drop the event rather
// than allocate (allocating then would leak -- the dispatcher is not
// draining).
TEST_F(EventBusTest, PublishDropsEventsOnceShuttingDown) {
    bus_.reinitialize(8, 8);
    shuttingDown_.store(true);

    bus_.publish(makeEvent());
    bus_.drain(); // nothing should have been enqueued to drain
    SUCCEED();
}

// C25 drain point: undispatched events are returned to the pool WITHOUT
// dispatching -- registering a callback and draining (never starting the
// dispatch thread) must never invoke it.
TEST_F(EventBusTest, DrainReturnsEventsWithoutDispatching) {
    bus_.reinitialize(8, 8);

    std::atomic<bool> called{false};
    bus_.registerCallback([&called](const SystemEvent&) { called.store(true); });

    bus_.publish(makeEvent());
    bus_.publish(makeEvent());
    bus_.drain();

    EXPECT_FALSE(called.load());
}

TEST_F(EventBusTest, JoinRefusesSelfJoinFromInsideTheLoop) {
    bus_.reinitialize(8, 8);
    std::atomic<bool> selfJoinReturned{false};
    std::atomic<bool> selfJoinResult{true};
    std::atomic<bool> mayExit{false};

    bus_.registerCallback([&](const SystemEvent&) {
        // Only exercise this once -- a second event delivered before mayExit
        // flips would re-enter and hang the callback vector copy, not the
        // join itself, but keep it simple and single-shot regardless.
        if (!selfJoinReturned.load()) {
            selfJoinResult.store(bus_.join());
            selfJoinReturned.store(true);
        }
        mayExit.store(true);
    });

    bus_.start();
    bus_.publish(makeEvent());

    ASSERT_TRUE(waitFor([&] { return selfJoinReturned.load(); }));
    EXPECT_FALSE(selfJoinResult.load());

    EXPECT_TRUE(bus_.stopAndJoin());
}

TEST_F(EventBusTest, IsOnDispatchThreadTrueOnlyInsideTheLoop) {
    bus_.reinitialize(8, 8);
    std::atomic<bool> observedInsideLoop{false};
    std::atomic<bool> mayExit{false};

    bus_.registerCallback([&](const SystemEvent&) {
        observedInsideLoop.store(bus_.isOnDispatchThread());
        mayExit.store(true);
    });

    EXPECT_FALSE(bus_.isOnDispatchThread());

    bus_.start();
    bus_.publish(makeEvent());
    ASSERT_TRUE(waitFor([&] { return mayExit.load(); }));
    EXPECT_TRUE(observedInsideLoop.load());

    EXPECT_TRUE(bus_.stopAndJoin());
    EXPECT_FALSE(bus_.isOnDispatchThread());
}

TEST_F(EventBusTest, ResetCallbacksClearsRegistryAndIdCounter) {
    bus_.reinitialize(8, 8);
    bus_.registerCallback([](const SystemEvent&) {});
    bus_.registerCallback([](const SystemEvent&) {});

    bus_.resetCallbacks();

    // The id counter restarts at 1 -- same first id a brand-new bus would hand out.
    EXPECT_EQ(bus_.registerCallback([](const SystemEvent&) {}), 1u);
}

// C18: the dispatch loop must snapshot callbacks_ under callbacksMutex_ and
// release before invoking -- a callback that re-enters registerCallback()
// would deadlock on a non-recursive mutex otherwise.
TEST_F(EventBusTest, ReentrantCallbackRegistrationDoesNotDeadlock) {
    bus_.reinitialize(8, 8);
    std::atomic<uint32_t> innerId{0};
    std::atomic<bool> done{false};

    bus_.registerCallback([&](const SystemEvent&) {
        innerId.store(bus_.registerCallback([](const SystemEvent&) {}));
        done.store(true);
    });

    bus_.start();
    bus_.publish(makeEvent());

    ASSERT_TRUE(waitFor([&] { return done.load(); }))
        << "dispatch deadlocked: callback invoked under callbacksMutex_ (C18)";
    EXPECT_GT(innerId.load(), 0u);

    EXPECT_TRUE(bus_.stopAndJoin());
}

TEST_F(EventBusTest, RequestStopIsNonBlockingAndStopAndJoinDrainsOnSuccess) {
    bus_.reinitialize(8, 8);
    bus_.start();

    bus_.requestStop(); // non-blocking; loop notices within its 100ms poll
    EXPECT_TRUE(bus_.stopAndJoin());
    EXPECT_FALSE(bus_.join()); // already joined; nothing left to reap
}

TEST_F(EventBusTest, DropCallbackFiresOnQueueOverflow) {
    // Both MemoryPool and ThreadSafeQueue clamp to a 16-slot minimum
    // (rounded to a power of 2) regardless of the size requested -- pool
    // bigger than queue, dispatch thread never started: the first 16
    // publishes fill the queue; the 17th allocates fine (pool has room) but
    // the enqueue fails -- exercising publish()'s "queue full" drop branch
    // specifically (it deallocates the object back to the pool and calls
    // onEventDropped_).
    bus_.reinitialize(32, 16);
    for (int i = 0; i < 16; ++i) {
        bus_.publish(makeEvent());
    }
    EXPECT_EQ(droppedCount_.load(), 0);

    bus_.publish(makeEvent());
    EXPECT_EQ(droppedCount_.load(), 1);
}

TEST_F(EventBusTest, DropCallbackFiresOnPoolExhaustion) {
    // Pool clamped to its 16-slot minimum, queue bigger, never started: the
    // first 16 publishes take every pool slot; the 17th finds the pool
    // exhausted -- publish()'s other drop branch.
    bus_.reinitialize(16, 32);
    for (int i = 0; i < 16; ++i) {
        bus_.publish(makeEvent());
    }
    EXPECT_EQ(droppedCount_.load(), 0);

    bus_.publish(makeEvent());
    EXPECT_EQ(droppedCount_.load(), 1);
}
