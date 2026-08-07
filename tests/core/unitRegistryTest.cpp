/**
 * @file unitRegistryTest.cpp
 * @brief Unit tests for UnitRegistry (Phase 2 core decomposition, step 2)
 *
 * Pins the behavior extracted from AxonVexSystem's
 * processingUnits_/unitToIdMap_/unitsMutex_/nextUnitId_ (see
 * docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.6). AxonVexSystemTest's ProcessingUnit* tests continue to pin
 * the façade-level behavior; these tests pin the registry in isolation.
 */

#include <algorithm>
#include <atomic>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_core/unitRegistry.hpp>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

using namespace axonvex::core;
using namespace std::chrono_literals;

namespace {

class FixturePU : public ProcessingUnit {
  public:
    explicit FixturePU(const std::string& name) : ProcessingUnit(name) {}

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }
    void processSync() override {}
    void processAsync() override {}
    void reset() override {
        setState(ExecutionState::INITIALIZED);
    }
    void finalize() override {}
    std::string getTypeDescription() override {
        return "FixturePU";
    }
};

/// Destructor calls back into the registry that (allegedly) still owns it.
/// If remove()/clear() destroyed this unit WHILE still holding mutex_, the
/// count() call below would self-deadlock on the (non-recursive) mutex —
/// exactly the C18-shape bug the registry's "return ownership, destroy
/// outside the lock" design avoids. Run off the test thread with a deadline
/// per this project's no-hang-without-a-watchdog rule.
class ReentrantDtorPU : public ProcessingUnit {
  public:
    ReentrantDtorPU(const std::string& name, const UnitRegistry* registry)
        : ProcessingUnit(name), registry_(registry) {}

    ~ReentrantDtorPU() override {
        if (registry_) {
            // A hang here (self-deadlock) means the caller destroyed us
            // under its own lock; a clean return with count()==0 (we've
            // already been erased from the maps) means the lock was
            // released first.
            reentrantCallObservedCount_ = registry_->count();
            reentrantCallSucceeded_ = true;
        }
    }

    void initialize() override {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    void finalize() override {}
    std::string getTypeDescription() override {
        return "ReentrantDtorPU";
    }

    const UnitRegistry* registry_;
    static std::atomic<bool> reentrantCallSucceeded_;
    static std::atomic<size_t> reentrantCallObservedCount_;
};

std::atomic<bool> ReentrantDtorPU::reentrantCallSucceeded_{false};
std::atomic<size_t> ReentrantDtorPU::reentrantCallObservedCount_{999};

} // namespace

class UnitRegistryTest : public ::testing::Test {
  protected:
    UnitRegistry registry_{100};
};

TEST_F(UnitRegistryTest, AddAssignsMonotonicIdsAndFind) {
    uint32_t id1 = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));
    uint32_t id2 = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("b")));

    EXPECT_NE(id1, id2);
    ASSERT_NE(registry_.find(id1), nullptr);
    ASSERT_NE(registry_.find(id2), nullptr);
    EXPECT_EQ(registry_.find(id1)->getName(), "a");
    EXPECT_EQ(registry_.find(id2)->getName(), "b");
    EXPECT_EQ(registry_.find(999), nullptr);
    EXPECT_EQ(registry_.count(), 2u);
}

TEST_F(UnitRegistryTest, AddThrowsOnNullUnit) {
    EXPECT_THROW(registry_.add(nullptr), std::invalid_argument);
}

TEST_F(UnitRegistryTest, AddThrowsWhenAtCapacity) {
    UnitRegistry small(1);
    small.add(std::unique_ptr<ProcessingUnit>(new FixturePU("only")));
    EXPECT_THROW(small.add(std::unique_ptr<ProcessingUnit>(new FixturePU("overflow"))),
                 std::runtime_error);
    EXPECT_EQ(small.count(), 1u);
}

TEST_F(UnitRegistryTest, AddAndRunCallbackSeesUnitAlreadyInsertedAndReturnsSameId) {
    uint32_t observedId = 0;
    ProcessingUnit* observedPtr = nullptr;

    uint32_t id = registry_.addAndRun(std::unique_ptr<ProcessingUnit>(new FixturePU("a")),
                                      [&](uint32_t cbId, ProcessingUnit* cbPtr) {
                                          observedId = cbId;
                                          observedPtr = cbPtr;
                                          // Safe to dereference: still under mutex_, so no
                                          // concurrent clear()/remove() could have freed it.
                                          EXPECT_EQ(cbPtr->getName(), "a");
                                      });

    EXPECT_EQ(observedId, id);
    EXPECT_EQ(registry_.find(id), observedPtr);
}

// Regression for the review fix: addAndRun's callback must run while mutex_
// is held, closing the add()-then-use-the-pointer race registerProcessingUnit
// used to have. Proven RED pre-fix at the AxonVexSystem level (ASan
// heap-use-after-free); this pins the primitive addAndRun() is built on.
TEST_F(UnitRegistryTest, AddAndRunRollsBackAndPropagatesOnCallbackThrow) {
    EXPECT_THROW(registry_.addAndRun(std::unique_ptr<ProcessingUnit>(new FixturePU("a")),
                                     [](uint32_t, ProcessingUnit*) {
                                         throw std::runtime_error("schedule failed");
                                     }),
                 std::runtime_error);

    EXPECT_EQ(registry_.count(), 0u);
    EXPECT_TRUE(registry_.all().empty());
}

// Regression for the review fix (startComponents()): forEach() must hold
// mutex_ for the WHOLE iteration, not just per-call, so a concurrent
// clear() cannot free a unit out from under the caller mid-loop (this is
// exactly the shape startComponents() used to have via an unlocked all()
// snapshot). This is deterministic, not timing-dependent: forEach()'s
// callback below parks on a condition_variable while still holding mutex_
// (proven by ForEachVisitsEveryUnit/the class's locking contract), so any
// concurrent clear() -- which takes the same mutex_ -- is *structurally*
// blocked for as long as the callback runs, however the OS schedules the
// threads. Uses cv.wait() (untimed) rather than wait_for(): the timed
// overload trips a known ThreadSanitizer/libstdc++ interceptor false
// positive on pthread_cond_timedwait ("double lock of a mutex" + a phantom
// data race), reproduced in isolation with zero AxonVex/UnitRegistry code
// involved -- not a real bug, but avoided here since this project runs
// TSan as a gate. The join loop below is this test's own deadline/hang
// guard (project rule: no fixed sleeps, hang-mode tests need a deadline),
// same detach-and-FAIL pattern as RemoveDestroysUnitAfterReleasingLock.
TEST_F(UnitRegistryTest, ForEachSerializesAgainstConcurrentClear) {
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));

    std::mutex gate;
    std::condition_variable cv;
    bool iterating = false;
    bool releaseIteration = false;
    // callbackRunning is set and cleared INSIDE the callback, i.e. while
    // forEach() still holds the registry mutex. That ordering is the whole
    // point: clearing it happens-before forEach() releases the mutex, which
    // happens-before clear() acquires it, so the clearer's read below is
    // properly ordered. An earlier version of this test instead set a flag
    // *after* forEach() returned, outside the mutex — leaving a real race
    // where clear() could legitimately acquire the mutex and finish before
    // the iterator thread reached that store, failing the assertion against
    // correct code. It passed on a fast box and failed on a loaded CI runner.
    std::atomic<bool> callbackRunning{false};
    std::atomic<bool> clearSawForEachStillRunning{false};
    std::atomic<bool> iteratorDone{false};
    std::atomic<bool> clearerDone{false};

    std::thread iterator([&] {
        registry_.forEach([&](ProcessingUnit* /*u*/) {
            callbackRunning.store(true);
            std::unique_lock<std::mutex> lk(gate);
            iterating = true;
            cv.notify_all();
            cv.wait(lk, [&] { return releaseIteration; });
            callbackRunning.store(false);
        });
        iteratorDone.store(true);
    });

    {
        std::unique_lock<std::mutex> lk(gate);
        cv.wait(lk, [&] { return iterating; });
    }

    std::thread clearer([&] {
        registry_.clear(); // must block on mutex_ until forEach() releases it
        clearSawForEachStillRunning.store(callbackRunning.load());
        clearerDone.store(true);
    });

    {
        std::lock_guard<std::mutex> lk(gate);
        releaseIteration = true;
    }
    cv.notify_all();

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!iteratorDone.load() || !clearerDone.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!iteratorDone.load() || !clearerDone.load()) {
        iterator.detach();
        clearer.detach();
        FAIL() << "forEach()/clear() appear wedged (self-deadlock or missed "
                  "notify) -- mutual exclusion via mutex_ should never hang.";
        return;
    }
    iterator.join();
    clearer.join();

    // If forEach() ever released mutex_ before running its callback (the
    // pre-fix all()-then-iterate-unlocked shape), clear() could complete
    // while the callback was still parked -- this would be false.
    EXPECT_FALSE(clearSawForEachStillRunning.load());
}

TEST_F(UnitRegistryTest, ForEachVisitsEveryUnit) {
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("b")));

    std::vector<std::string> names;
    registry_.forEach([&](ProcessingUnit* u) { names.push_back(u->getName()); });

    ASSERT_EQ(names.size(), 2u);
    EXPECT_NE(std::find(names.begin(), names.end(), "a"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "b"), names.end());
}

TEST_F(UnitRegistryTest, RemoveReturnsOwnershipAndErasesEntry) {
    uint32_t id = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));

    std::unique_ptr<ProcessingUnit> removed = registry_.remove(id);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->getName(), "a");
    EXPECT_EQ(registry_.find(id), nullptr);
    EXPECT_EQ(registry_.count(), 0u);

    EXPECT_EQ(registry_.remove(id), nullptr); // already gone
}

TEST_F(UnitRegistryTest, FindByNameReturnsFirstMatchOrNull) {
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("gen")));
    ProcessingUnit* p = registry_.findByName("gen");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getName(), "gen");
    EXPECT_EQ(registry_.findByName("nonexistent"), nullptr);
}

TEST_F(UnitRegistryTest, IdOfRoundTripsWithAdd) {
    auto unit = std::unique_ptr<ProcessingUnit>(new FixturePU("a"));
    ProcessingUnit* raw = unit.get();
    uint32_t id = registry_.add(std::move(unit));

    auto found = registry_.idOf(raw);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, id);

    FixturePU notRegistered("stray");
    EXPECT_FALSE(registry_.idOf(&notRegistered).has_value());
}

TEST_F(UnitRegistryTest, AllReturnsSnapshotOfEveryUnit) {
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("b")));

    auto units = registry_.all();
    ASSERT_EQ(units.size(), 2u);
    std::vector<std::string> names;
    for (auto* u : units) {
        names.push_back(u->getName());
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "a"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "b"), names.end());
}

TEST_F(UnitRegistryTest, ClearRemovesEverythingButKeepsIdCounterAdvancing) {
    uint32_t id1 = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));
    registry_.clear();
    EXPECT_EQ(registry_.count(), 0u);
    EXPECT_TRUE(registry_.all().empty());

    uint32_t id2 = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("b")));
    EXPECT_NE(id1, id2); // counter did not reset
}

TEST_F(UnitRegistryTest, ResetIdsRestartsCounterAtOne) {
    registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("a")));
    registry_.clear();
    registry_.resetIds();

    uint32_t id = registry_.add(std::unique_ptr<ProcessingUnit>(new FixturePU("b")));
    EXPECT_EQ(id, 1u);
}

TEST_F(UnitRegistryTest, NonCopyableNonMovable) {
    EXPECT_FALSE(std::is_copy_constructible<UnitRegistry>::value);
    EXPECT_FALSE(std::is_move_constructible<UnitRegistry>::value);
}

// C18-shape regression: remove() must release mutex_ before the removed
// unit's destructor (user code) runs. Executed off-thread with a deadline —
// a regression here is a self-deadlock hang, not an assertion failure.
TEST_F(UnitRegistryTest, RemoveDestroysUnitAfterReleasingLock) {
    ReentrantDtorPU::reentrantCallSucceeded_.store(false);
    ReentrantDtorPU::reentrantCallObservedCount_.store(999);

    uint32_t id =
        registry_.add(std::unique_ptr<ProcessingUnit>(new ReentrantDtorPU("r", &registry_)));

    std::atomic<bool> done{false};
    std::thread worker([&] {
        std::unique_ptr<ProcessingUnit> removed = registry_.remove(id);
        removed.reset(); // destructor runs here, outside remove()'s lock
        done.store(true);
    });

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!done.load()) {
        worker.detach(); // wedged: fail the test instead of hanging ctest
        FAIL() << "remove() appears to have destroyed the unit while still "
                  "holding its lock (self-deadlock on reentrant count() call)";
        return;
    }
    worker.join();

    EXPECT_TRUE(ReentrantDtorPU::reentrantCallSucceeded_.load());
    EXPECT_EQ(ReentrantDtorPU::reentrantCallObservedCount_.load(), 0u);
}

TEST_F(UnitRegistryTest, ClearDestroysUnitsAfterReleasingLock) {
    ReentrantDtorPU::reentrantCallSucceeded_.store(false);
    ReentrantDtorPU::reentrantCallObservedCount_.store(999);

    registry_.add(std::unique_ptr<ProcessingUnit>(new ReentrantDtorPU("r", &registry_)));

    std::atomic<bool> done{false};
    std::thread worker([&] {
        auto removed = registry_.clear();
        removed.clear(); // destructors run here, outside clear()'s lock
        done.store(true);
    });

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!done.load()) {
        worker.detach();
        FAIL() << "clear() appears to have destroyed units while still "
                  "holding its lock (self-deadlock on reentrant count() call)";
        return;
    }
    worker.join();

    EXPECT_TRUE(ReentrantDtorPU::reentrantCallSucceeded_.load());
    EXPECT_EQ(ReentrantDtorPU::reentrantCallObservedCount_.load(), 0u);
}
