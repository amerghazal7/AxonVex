/**
 * @file telemetryBus.hpp
 * @brief Bounded, non-blocking telemetry fan-out: RT publishers -> one egress thread -> sinks.
 *
 * Frame contract (Phase 9 dashboard / gateway wire format)
 * --------------------------------------------------------
 * Every published record is delivered to subscribers as a TelemetryFrame whose
 * envelope is stable and self-describing:
 *
 *   { "channel":   string   — publisher-chosen routing key,
 *     "seq":       uint64   — per-channel, gapless, assigned in delivery order,
 *     "timestamp": uint64   — nanoseconds since Unix epoch, captured at publish(),
 *     "payload":   byte[]   — opaque application bytes (JSON: array of numbers) }
 *
 * TelemetryFrame carries the structured fields alongside `json`, the v1 (JSON)
 * encoding of that envelope. A MessagePack encoding (Phase 9) slots in as an
 * alternate encoder over the same four fields — the envelope keys and semantics
 * are the contract, not the text encoding.
 *
 * Threading model
 * ---------------
 * - publish() is safe from any number of RT/unit threads: lock-free w.r.t. the
 *   egress thread (house Vyukov MPMC ThreadSafeQueue), never blocks, never runs
 *   user code, and performs no heap allocation when the caller moves in the
 *   payload (channel strings within the small-string buffer also stay
 *   allocation-free). Full queue => the publish FAILS (drop-newest), the frame
 *   is discarded, and droppedCount() advances — publishers are never stalled.
 * - All subscriber callbacks run on the single bus-owned egress thread,
 *   dispatched via snapshot-then-dispatch: the registry lock is NEVER held
 *   while user code runs.
 * - stop() stops accepting new frames, drains what was already accepted, then
 *   joins. The destructor stops. Calling stop() from inside a subscriber
 *   callback is safe (deferred self-join, Watchdog pattern).
 *
 * Memory-ordering argument for the lock-free publish/egress handoff: record
 * contents are published by ThreadSafeQueue's release store on the slot
 * sequence and acquired by the consumer's load (see threadSafeQueue.hpp for
 * the full argument); running_ uses release/acquire so a publisher that
 * observes running_==true cannot race the queue's destruction (stop() joins
 * before teardown). Proven by the TSan multi-producer stress test
 * (TelemetryBusTest.MultiProducerStressPreservesPerChannelOrder).
 *
 * Subscriber lifetime: the bus does not own subscriber pointers. unsubscribe()
 * does not wait for an in-flight dispatch on the egress thread; destroy a
 * subscriber only after stop() has returned (or before start()).
 */

#pragma once

#include <atomic>
#include <axonvex_core/callback.hpp>
#include <axonvex_core/callerKeyed.hpp>
#include <axonvex_core/utils/containers/threadSafeQueue.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace axonvex::visualization {

struct TelemetryFrame {
    std::string channel;
    uint64_t seq = 0;         ///< per-channel, gapless, delivery order
    uint64_t timestampNs = 0; ///< ns since Unix epoch, captured at publish()
    std::vector<uint8_t> payload;
    std::string json; ///< v1 encoding of the envelope (see file header)
};

class TelemetryBus {
  public:
    using Message = std::vector<uint8_t>;
    using Subscriber = axonvex::core::Callback<TelemetryFrame>;

    static constexpr size_t DEFAULT_QUEUE_CAPACITY = 1024;

    explicit TelemetryBus(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY) : queue_(queueCapacity) {}

    ~TelemetryBus() {
        TelemetryBus::stop();
        if (worker_.joinable()) {
            // Only reachable when the destructor runs on the egress thread
            // itself (bus destroyed from its own callback): stop() deferred the
            // join and ~std::thread on a joinable thread is std::terminate.
            // Not a supported lifecycle; detaching is the least-bad option.
            worker_.detach();
        }
    }

    TelemetryBus(const TelemetryBus&) = delete;
    TelemetryBus& operator=(const TelemetryBus&) = delete;
    TelemetryBus(TelemetryBus&&) = delete; // owns a mutex and a thread
    TelemetryBus& operator=(TelemetryBus&&) = delete;

    /// Spawns the egress thread. Idempotent; returns true when running.
    bool start() {
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        if (running_.exchange(true))
            return true;
        if (worker_.joinable()) {
            // Reclaim a worker left joinable by a deferred self-stop; its loop
            // has already exited (running_ was false), so this join is prompt.
            worker_.join();
        }
        worker_ = std::thread([this]() {
            // Published by the worker as its FIRST act and cleared LAST so
            // stop() can detect self-stop without reading the std::thread
            // object (start()'s move-assign races the child otherwise).
            workerId_.store(std::this_thread::get_id(), std::memory_order_release);
            run();
            workerId_.store(std::thread::id(), std::memory_order_release);
        });
        return true;
    }

    /// Stops accepting frames, drains everything already accepted, joins.
    /// Safe to call from a subscriber callback: the join is deferred to the
    /// destructor or a later external stop().
    void stop() {
        // No early return on running_: after a deferred self-stop the flag is
        // already false while the thread still needs reclaiming.
        running_.store(false, std::memory_order_release);

        // Self-stop check BEFORE any mutex (checklist items 1 and 4): a
        // subscriber callback runs ON the egress thread; joining here would be
        // a self-join (std::terminate), and blocking on lifecycleMutex_ while
        // an external stop() holds it waiting for our join would deadlock both.
        if (workerId_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            return; // run() exits after the callback returns; join deferred
        }

        // Serialises the join against concurrent external stop()/start().
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        if (worker_.joinable()) {
            worker_.join();
        }
        workerId_.store(std::thread::id(), std::memory_order_release);
    }

    bool isRunning() const {
        return running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Enqueue one telemetry record. RT-safe: bounded, lock-free w.r.t.
     * the egress thread, non-blocking, runs no user code.
     *
     * Both parameters are taken by value: move them in for an allocation-free
     * publish (a copied lvalue payload allocates in the CALLER's move-from,
     * not here).
     *
     * @return true if accepted; false if the bus is not running or the queue
     *         is full (drop-newest — the frame is discarded and droppedCount()
     *         advances).
     */
    bool publish(std::string channel, Message payload) {
        if (!running_.load(std::memory_order_acquire))
            return false;
        Record rec;
        rec.channel = std::move(channel);
        rec.timestampNs =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());
        rec.payload = std::move(payload);
        // Full queue => enqueue fails without blocking; counted by the queue's
        // enqueue-failure statistic, surfaced as droppedCount().
        return queue_.enqueue(std::move(rec));
    }

    /// Frames dropped because the queue was full (drop-newest policy).
    uint64_t droppedCount() const {
        return queue_.getStatistics().getEnqueueFailures();
    }

    /// Subscribe to a channel; "*" receives every channel. The bus does not
    /// take ownership; see the lifetime note in the file header.
    void subscribe(const std::string& channel, Subscriber* cb) {
        std::lock_guard<std::mutex> g(subscribersMutex_);
        subscribers_.registerKeyedCallback(channel, cb);
    }

    bool unsubscribe(const std::string& channel, Subscriber* cb) {
        std::lock_guard<std::mutex> g(subscribersMutex_);
        return subscribers_.unregisterKeyedCallback(channel, cb);
    }

    size_t unsubscribeAll(const std::string& channel) {
        std::lock_guard<std::mutex> g(subscribersMutex_);
        return subscribers_.unregisterAllCallbacksForKey(channel);
    }

  private:
    struct Record {
        std::string channel;
        uint64_t timestampNs = 0;
        Message payload;
    };

    void run() {
        while (running_.load(std::memory_order_acquire)) {
            // ponytail: polling drain mirrors Logger's worker; swap in an
            // eventfd wakeup if idle egress CPU ever matters.
            auto rec = queue_.tryDequeue(std::chrono::milliseconds(5));
            if (rec.has_value()) {
                dispatch(*rec);
            }
        }
        // stop() contract: drain what was accepted before the flag flipped.
        // Bounded — publishers are refused once running_ is false.
        for (;;) {
            auto rec = queue_.dequeue();
            if (!rec.has_value())
                break;
            dispatch(*rec);
        }
    }

    // Egress thread only.
    void dispatch(Record& rec) {
        TelemetryFrame frame;
        frame.channel = std::move(rec.channel);
        frame.seq = nextSeq_[frame.channel]++; // egress-thread-owned, no sync
        frame.timestampNs = rec.timestampNs;
        frame.payload = std::move(rec.payload);

        nlohmann::json j;
        j["channel"] = frame.channel;
        j["seq"] = frame.seq;
        j["timestamp"] = frame.timestampNs;
        j["payload"] = frame.payload;
        frame.json = j.dump();

        // Snapshot-then-dispatch: never run user code under subscribersMutex_.
        std::vector<Subscriber*> targets;
        {
            std::lock_guard<std::mutex> g(subscribersMutex_);
            targets = subscribers_.snapshotCallbacksForKey(frame.channel);
            const auto wildcard = subscribers_.snapshotCallbacksForKey("*");
            targets.insert(targets.end(), wildcard.begin(), wildcard.end());
        }
        for (Subscriber* cb : targets) {
            if (cb) {
                cb->callbackPerform(frame);
            }
        }
    }

    axonvex::utils::containers::ThreadSafeQueue<Record> queue_;
    std::atomic<bool> running_{false};
    /// Serialises start()/stop() so only one caller ever joins the worker.
    std::mutex lifecycleMutex_;
    std::thread worker_;
    /// Published by the worker itself; see stop().
    std::atomic<std::thread::id> workerId_{std::thread::id()};

    std::mutex subscribersMutex_;
    axonvex::core::CallerKeyed<std::string, TelemetryFrame> subscribers_;
    std::map<std::string, uint64_t> nextSeq_; // egress thread only
};

} // namespace axonvex::visualization
