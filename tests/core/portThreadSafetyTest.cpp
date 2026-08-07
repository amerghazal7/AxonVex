/**
 * @file portThreadSafetyTest.cpp
 * @brief Thread-safety tests for the port system.
 *
 * Ports used to carry a lock-free "pooled" fast path: writers published a
 * MemoryPool block through an atomic pointer and freed the previous block
 * immediately, while readers dereferenced that same pointer unguarded. With no
 * reclamation scheme a reader could still be inside a block the pool had
 * already recycled (defect C29). The path was deleted rather than fixed; these
 * tests pin the behaviour of the mutex path that replaced it and keep the
 * concurrent coverage the old MemoryPool tests provided.
 */

#include <atomic>
#include <axonvex_core/ports.hpp>
#include <axonvex_core/processingUnit.hpp>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace axonvex::core;

class PortThreadSafetyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        processingUnit = std::make_unique<MockProcessingUnit>();
    }

    void TearDown() override {
        processingUnit.reset();
    }

    class MockProcessingUnit : public ProcessingUnit {
      public:
        MockProcessingUnit() : ProcessingUnit("MockProcessingUnit") {}
        ~MockProcessingUnit() override = default;

        void processSync() override {}
        void processAsync() override {}
        void finalize() override {
            setState(ExecutionState::INITIALIZED);
        }
        void initialize() override {}
        void reset() override {
            setState(ExecutionState::INITIALIZED);
        }
        std::string getTypeDescription() override {
            return "MockProcessingUnit";
        }
    };

    std::unique_ptr<MockProcessingUnit> processingUnit;
};

// C31: thread safety is chosen at construction and cannot be changed. It used to
// be settable, which meant a writer that had read `true` could take the mutex
// path while a reader that had read `false` took the unlocked one, both on the
// same payload — an atomic flag makes the read safe, not the discipline switch.
TEST_F(PortThreadSafetyTest, ThreadSafetyIsFixedAtConstruction) {
    auto plainPort = std::make_unique<InputPort<int>>(1, "plain", processingUnit.get());
    EXPECT_FALSE(plainPort->isThreadSafe());

    auto safePort =
        std::make_unique<InputPort<int>>(2, "safe", processingUnit.get(), /*threadSafe=*/true);
    EXPECT_TRUE(safePort->isThreadSafe());

    // Both disciplines round-trip a value; only the guarantee differs.
    plainPort->writeData(41);
    safePort->writeData(42);
    EXPECT_EQ(plainPort->read(), 41);
    EXPECT_EQ(safePort->read(), 42);
    EXPECT_TRUE(safePort->hasNewData());
}

TEST_F(PortThreadSafetyTest, OutputPortDeliversToThreadSafeInput) {
    auto outputPort = std::make_unique<OutputPort<double>>(2, "test_output", processingUnit.get(),
                                                           /*threadSafe=*/true);
    auto inputPort = std::make_unique<InputPort<double>>(3, "test_input", processingUnit.get(),
                                                         /*threadSafe=*/true);

    outputPort->connect(inputPort.get());

    outputPort->write(3.14159);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_DOUBLE_EQ(inputPort->read(), 3.14159);
}

TEST_F(PortThreadSafetyTest, AsyncInputPortThreadSafeUpdateAndRead) {
    auto asyncInput = std::make_unique<AsyncInputPort<std::string>>(
        4, "async_input", processingUnit.get(), /*threadSafe=*/true);

    EXPECT_TRUE(asyncInput->isThreadSafe());

    asyncInput->update("Hello ports!");
    EXPECT_TRUE(asyncInput->wasUpdated());

    std::string result;
    asyncInput->read(result);
    EXPECT_EQ(result, "Hello ports!");
    EXPECT_FALSE(asyncInput->wasUpdated());
}

TEST_F(PortThreadSafetyTest, AsyncOutputPortDeliversToThreadSafeInput) {
    auto asyncOutput = std::make_unique<AsyncOutputPort<int>>(
        5, "async_output", processingUnit.get(), /*threadSafe=*/true);
    auto asyncInput = std::make_unique<AsyncInputPort<int>>(6, "async_input", processingUnit.get(),
                                                            /*threadSafe=*/true);

    asyncOutput->connect(asyncInput.get());

    asyncOutput->write(999);
    EXPECT_TRUE(asyncInput->wasUpdated());
    EXPECT_EQ(asyncInput->read(), 999);
}

TEST_F(PortThreadSafetyTest, ConcurrentReadersAndWritersOnInputPort) {
    const int NUM_THREADS = 4;
    const int OPERATIONS_PER_THREAD = 1000;

    auto inputPort = std::make_unique<InputPort<int>>(7, "concurrent", processingUnit.get(),
                                                      /*threadSafe=*/true);

    std::atomic<int> writeCounter{0};
    std::atomic<int> readCounter{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS / 2; ++t) {
        threads.emplace_back([&inputPort, &writeCounter, OPERATIONS_PER_THREAD]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                inputPort->writeData(writeCounter.fetch_add(1));
                std::this_thread::yield();
            }
        });
    }

    for (int t = 0; t < NUM_THREADS / 2; ++t) {
        threads.emplace_back([&inputPort, &readCounter, OPERATIONS_PER_THREAD]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                if (inputPort->hasNewData()) {
                    inputPort->read();
                    readCounter.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(writeCounter.load(), NUM_THREADS * OPERATIONS_PER_THREAD / 2);
    EXPECT_GT(readCounter.load(), 0);
}

// C29 regression: readers must never observe a torn or recycled value while
// writers churn. A non-trivial T makes any use-after-free visible — the old
// pooled path recycled the block a reader was still copying from, which ASan
// caught as a heap-use-after-free and TSan as a race on the payload.
TEST_F(PortThreadSafetyTest, ConcurrentAccessKeepsNonTrivialPayloadIntact) {
    struct Payload {
        std::string name;
        std::vector<double> values;

        Payload() : name("payload-0"), values(static_cast<size_t>(8), 0.0) {}
        explicit Payload(int seed)
            : name("payload-" + std::to_string(seed)),
              values(static_cast<size_t>(8), static_cast<double>(seed)) {}
    };

    const int NUM_WRITERS = 2;
    const int NUM_READERS = 2;
    const int WRITES_PER_WRITER = 500;

    auto inputPort = std::make_unique<InputPort<Payload>>(8, "payload", processingUnit.get(),
                                                          /*threadSafe=*/true);
    inputPort->writeData(Payload(0));

    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<bool> corrupted{false};
    std::vector<std::thread> threads;

    for (int w = 0; w < NUM_WRITERS; ++w) {
        threads.emplace_back([&inputPort, w, WRITES_PER_WRITER]() {
            for (int i = 0; i < WRITES_PER_WRITER; ++i) {
                inputPort->writeData(Payload(w * WRITES_PER_WRITER + i + 1));
            }
        });
    }

    for (int r = 0; r < NUM_READERS; ++r) {
        threads.emplace_back([&inputPort, &stop, &reads, &corrupted]() {
            // do-while, not while: this must be a POST-check. Main joins the
            // writers and then immediately does stop.store(true) before joining
            // us here -- nothing orders "a reader thread has been scheduled at
            // least once" against that store. With a pre-check while(), a
            // reader whose first scheduling quantum lands after the store sees
            // stop already true and exits having run the body zero times, so
            // reads stays 0 and the EXPECT_GT below fails even though nothing
            // is corrupted -- reproduces deterministically (198/200) on this
            // box with `taskset -c 0`, which is exactly what a contended CI
            // runner does to thread scheduling. A do-while runs the body --
            // the read() + invariant check + counters -- unconditionally once
            // per thread before ever consulting stop, which is a language
            // guarantee, not a timing one, so it fixes this without touching
            // the corruption check or any memory-ordering argument here.
            do {
                Payload seen = inputPort->read();
                // Every published Payload has 8 values all equal to its seed and
                // a name derived from that same seed. Anything else means the
                // reader copied out of a block that was being reused.
                bool ok = seen.values.size() == 8;
                if (ok) {
                    ok = seen.name == "payload-" + std::to_string(static_cast<int>(seen.values[0]));
                }
                if (ok) {
                    for (double v : seen.values) {
                        if (v != seen.values[0]) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (!ok) {
                    corrupted.store(true, std::memory_order_release);
                }
                reads.fetch_add(1, std::memory_order_relaxed);
            } while (!stop.load(std::memory_order_acquire));
        });
    }

    for (int i = 0; i < NUM_WRITERS; ++i) {
        threads[static_cast<size_t>(i)].join();
    }
    stop.store(true, std::memory_order_release);
    for (size_t i = static_cast<size_t>(NUM_WRITERS); i < threads.size(); ++i) {
        threads[i].join();
    }

    EXPECT_FALSE(corrupted.load(std::memory_order_acquire));
    EXPECT_GT(reads.load(), 0);
}

// C32: the dispatch path reads the callback members with no lock, and must keep
// doing so — locking there would run user code under a port lock and copying the
// std::function out would allocate on the port path. Registration is therefore
// setup-only, and a late registration must fail loudly rather than silently race
// a concurrent dispatch.
TEST_F(PortThreadSafetyTest, CallbackRegistrationIsRejectedAfterTraffic) {
    auto inputPort = std::make_unique<InputPort<int>>(20, "late_cb", processingUnit.get());
    auto outputPort = std::make_unique<OutputPort<int>>(21, "late_out", processingUnit.get());

    // Before any traffic, registration is fine.
    EXPECT_NO_THROW(inputPort->setDataCallback([](const int&) {}));
    EXPECT_NO_THROW(inputPort->setValidationCallback([](const int&) { return true; }));
    EXPECT_NO_THROW(outputPort->setOutputCallback([](const int&) {}));

    inputPort->writeData(1);
    outputPort->write(1);

    EXPECT_THROW(inputPort->setDataCallback([](const int&) {}), std::logic_error);
    EXPECT_THROW(inputPort->setValidationCallback([](const int&) { return true; }),
                 std::logic_error);
    EXPECT_THROW(outputPort->setOutputCallback([](const int&) {}), std::logic_error);
}

// C35: OutputPort::write and AsyncOutputPort::write used to hold
// connectionMutex_ while calling into the connected ports, and that call runs
// the receiving port's user callback. A callback that touched the sender's own
// connection API therefore deadlocked on a non-recursive mutex. Dispatch now
// runs off an immutable snapshot with no lock held.
//
// Guarded by a deadline: a regression here hangs rather than fails, and a hung
// test blocks the suite. Polled, not condition-variable based — GCC 11's libtsan
// does not intercept pthread_cond_clockwait and reports bogus races for wait_for.
namespace {
bool completesWithin(std::function<void()> fn, std::chrono::milliseconds limit) {
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
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}
} // namespace

TEST_F(PortThreadSafetyTest, DownstreamCallbackMayReenterConnectionApi) {
    auto* unit = processingUnit.get();
    std::atomic<int> delivered{0};

    const bool finished = completesWithin(
        [unit, &delivered]() {
            OutputPort<int> out(30, "reentrant_out", unit);
            InputPort<int> first(31, "first", unit);
            InputPort<int> second(32, "second", unit);

            out.connect(&first);

            // Runs while the sender is dispatching; every call below takes
            // connectionMutex_, which the sender used to still be holding.
            first.setDataCallback([&out, &second, &delivered](const int&) {
                out.getConnectionCount();
                out.isConnected();
                out.connect(&second);
                out.disconnect(&second);
                delivered.fetch_add(1);
            });

            out.write(42);
        },
        std::chrono::milliseconds(5000));

    EXPECT_TRUE(finished) << "a downstream callback re-entering the connection API deadlocked";
    EXPECT_EQ(delivered.load(), 1);
}

TEST_F(PortThreadSafetyTest, ResetClearsData) {
    auto inputPort = std::make_unique<InputPort<int>>(9, "reset_test", processingUnit.get(),
                                                      /*threadSafe=*/true);

    inputPort->writeData(123);
    EXPECT_TRUE(inputPort->hasNewData());

    inputPort->reset();
    EXPECT_FALSE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 0);

    inputPort->writeData(456);
    EXPECT_TRUE(inputPort->hasNewData());
    EXPECT_EQ(inputPort->read(), 456);
}

TEST_F(PortThreadSafetyTest, ComplexDataTypesRoundTrip) {
    struct ComplexData {
        int id;
        std::string name;
        std::vector<double> values;

        ComplexData() : id(0), name(""), values() {}

        ComplexData(int i, const std::string& n, std::vector<double> v)
            : id(i), name(n), values(std::move(v)) {}

        bool operator==(const ComplexData& other) const {
            return id == other.id && name == other.name && values == other.values;
        }
    };

    auto inputPort = std::make_unique<InputPort<ComplexData>>(
        10, "complex_test", processingUnit.get(), /*threadSafe=*/true);

    ComplexData testData{42, "TestObject", {1.0, 2.5, 3.14159}};

    inputPort->writeData(testData);
    EXPECT_TRUE(inputPort->hasNewData());

    ComplexData result = inputPort->read();
    EXPECT_EQ(result, testData);
}

TEST_F(PortThreadSafetyTest, MessageStatistics) {
    auto inputPort = std::make_unique<InputPort<int>>(11, "stats_test", processingUnit.get(),
                                                      /*threadSafe=*/true);

    for (int i = 0; i < 10; ++i) {
        inputPort->writeData(i);
    }

    EXPECT_EQ(inputPort->getTotalMessages(), 10);
    EXPECT_EQ(inputPort->getValidMessages(), 10);
    EXPECT_EQ(inputPort->getInvalidMessages(), 0);
}

TEST_F(PortThreadSafetyTest, ConcurrentProducersAndConsumers) {
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int MESSAGES_PER_PRODUCER = 100;

    auto outputPort = std::make_unique<OutputPort<int>>(12, "producer", processingUnit.get(),
                                                        /*threadSafe=*/true);
    auto inputPort =
        std::make_unique<InputPort<int>>(13, "consumer", processingUnit.get(), /*threadSafe=*/true);

    outputPort->connect(inputPort.get());

    std::atomic<int> totalProduced{0};
    std::atomic<int> totalConsumed{0};
    std::vector<std::thread> threads;

    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        threads.emplace_back([&outputPort, &totalProduced, MESSAGES_PER_PRODUCER, p]() {
            for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i) {
                outputPort->write(p * MESSAGES_PER_PRODUCER + i);
                totalProduced.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }

    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        threads.emplace_back([&inputPort, &totalConsumed, NUM_PRODUCERS, MESSAGES_PER_PRODUCER]() {
            while (totalConsumed.load() < NUM_PRODUCERS * MESSAGES_PER_PRODUCER) {
                if (inputPort->hasNewData()) {
                    inputPort->read();
                    totalConsumed.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(totalProduced.load(), NUM_PRODUCERS * MESSAGES_PER_PRODUCER);
    // Consumers race between hasNewData() and read(), so the count can overshoot.
    EXPECT_GE(totalConsumed.load(), NUM_PRODUCERS * MESSAGES_PER_PRODUCER);
}

// V9/C35 regression: InputPort<T>::writeData used to hold bridgeMutex_ across
// the entire bridged fan-out, including the nested bridgedPort->writeData()
// call — which runs the BRIDGED port's own dataCallback_. A callback that
// reentered the ORIGINAL port's bridge API (add/removeBridgedPort, or
// isBridgePort()) therefore deadlocked on a non-recursive mutex. Dispatch now
// runs off an immutable snapshot with no lock held, so this must complete.
TEST_F(PortThreadSafetyTest, DownstreamCallbackMayReenterBridgeApi) {
    auto* unit = processingUnit.get();
    std::atomic<int> delivered{0};

    const bool finished = completesWithin(
        [unit, &delivered]() {
            InputPort<int> source(40, "bridge_source", unit);
            InputPort<int> bridged(41, "bridge_target", unit);
            InputPort<int> extra(42, "bridge_extra", unit);

            source.addBridgedPort(&bridged);

            // Runs while source.writeData() is still dispatching to `bridged`;
            // every call below takes source's bridgeMutex_, which the old code
            // would still be holding at this point.
            bridged.setDataCallback([&source, &extra, &delivered](const int&) {
                source.isBridgePort();
                source.addBridgedPort(&extra);
                source.removeBridgedPort(&extra);
                delivered.fetch_add(1);
            });

            source.writeData(7);
        },
        std::chrono::milliseconds(5000));

    EXPECT_TRUE(finished) << "a downstream callback re-entering the bridge API deadlocked";
    EXPECT_EQ(delivered.load(), 1);
}

// V9 TSan stress test: writers hammer writeData() while a churner thread
// concurrently adds/removes bridged ports. This must be race-free (no torn
// reads of the bridge list) and must never crash — TSan is the real gate.
TEST_F(PortThreadSafetyTest, ConcurrentBridgeChurnDuringWriteDataIsRaceFree) {
    auto* unit = processingUnit.get();
    auto source = std::make_unique<InputPort<int>>(43, "churn_source", unit);
    auto bridgedA = std::make_unique<InputPort<int>>(44, "churn_bridge_a", unit);
    auto bridgedB = std::make_unique<InputPort<int>>(45, "churn_bridge_b", unit);

    std::atomic<int> deliveredToA{0};
    std::atomic<int> deliveredToB{0};
    bridgedA->setDataCallback([&deliveredToA](const int&) { deliveredToA.fetch_add(1); });
    bridgedB->setDataCallback([&deliveredToB](const int&) { deliveredToB.fetch_add(1); });

    const int WRITES = 20000;
    const int CHURNS = 5000;
    std::atomic<bool> churnDone{false};

    std::thread writer([&source, WRITES]() {
        for (int i = 0; i < WRITES; ++i) {
            source->writeData(i);
        }
    });

    std::thread churner([&source, &bridgedA, &bridgedB, CHURNS, &churnDone]() {
        for (int i = 0; i < CHURNS; ++i) {
            source->addBridgedPort(bridgedA.get());
            source->addBridgedPort(bridgedB.get());
            source->removeBridgedPort(bridgedA.get());
            source->removeBridgedPort(bridgedB.get());
        }
        churnDone.store(true);
    });

    writer.join();
    churner.join();

    EXPECT_TRUE(churnDone.load());
    EXPECT_EQ(source->getTotalMessages(), static_cast<uint64_t>(WRITES));
    // Every delivered message actually carried a value the writer sent; no
    // assertion on the exact count since bridging is torn down/rebuilt
    // throughout — the point is TSan silence plus no crash/UB.
}

// Same COW recipe, AsyncInputPort<T> side (update() rather than writeData()).
TEST_F(PortThreadSafetyTest, AsyncInputPortConcurrentBridgeChurnDuringUpdateIsRaceFree) {
    auto* unit = processingUnit.get();
    auto source = std::make_unique<AsyncInputPort<int>>(46, "async_churn_source", unit);
    auto bridged = std::make_unique<AsyncInputPort<int>>(47, "async_churn_bridge", unit);

    const int WRITES = 20000;
    const int CHURNS = 5000;

    std::thread writer([&source, WRITES]() {
        for (int i = 0; i < WRITES; ++i) {
            source->update(i);
        }
    });

    std::thread churner([&source, &bridged, CHURNS]() {
        for (int i = 0; i < CHURNS; ++i) {
            source->addBridgedPort(bridged.get());
            source->removeBridgedPort(bridged.get());
        }
    });

    writer.join();
    churner.join();

    EXPECT_EQ(source->getTotalMessages(), static_cast<uint64_t>(WRITES));
}

// V10 TSan stress test: writers hammer OutputPort<T>::write() while a churner
// thread concurrently connects/disconnects input ports. Pins the "keep the
// mutex" decision (see ports.hpp's snapshotConnections comment) under TSan.
TEST_F(PortThreadSafetyTest, ConcurrentConnectDisconnectDuringWriteStaysConsistent) {
    auto* unit = processingUnit.get();
    auto output = std::make_unique<OutputPort<int>>(48, "connect_churn_out", unit);
    auto steady = std::make_unique<InputPort<int>>(49, "connect_churn_steady", unit);
    auto churned = std::make_unique<InputPort<int>>(50, "connect_churn_target", unit);

    std::atomic<int> steadyDeliveries{0};
    steady->setDataCallback([&steadyDeliveries](const int&) { steadyDeliveries.fetch_add(1); });
    output->connect(steady.get());

    const int WRITES = 20000;
    const int CHURNS = 5000;

    std::thread writer([&output, WRITES]() {
        for (int i = 0; i < WRITES; ++i) {
            output->write(i);
        }
    });

    std::thread churner([&output, &churned, CHURNS]() {
        for (int i = 0; i < CHURNS; ++i) {
            output->connect(churned.get());
            output->disconnect(churned.get());
        }
    });

    writer.join();
    churner.join();

    EXPECT_EQ(output->getTotalMessages(), static_cast<uint64_t>(WRITES));
    // `steady` was connected for the whole run and never touched by the
    // churner, so it must have seen every single write.
    EXPECT_EQ(steadyDeliveries.load(), WRITES);
}
