/**
 * @file builtinUnits.hpp
 * @brief Trivial reference ProcessingUnit types used to prove the UnitFactory
 *        + introspection-metadata contract end to end (design spec §2.3).
 *
 * SineGenerator -> MovingAverage -> StatsSink is exactly the Phase-2
 * exit-criterion example spec document. These are demo units, not v1.0
 * signal-processing features: keep them boring on purpose.
 */

#pragma once

#include "processingUnit.hpp"
#include "unitFactory.hpp"

#include <atomic>
#include <vector>

namespace axonvex::core::builtin {

/// axonvex.SineGenerator — one Sync output port ("out", double). Params:
/// frequencyHz (double, default 1.0, >=0), amplitude (double, default 1.0, >=0).
class SineGenerator final : public ProcessingUnit {
  public:
    SineGenerator(const std::string& name, const nlohmann::json& params);

    void processSync() override;
    void processAsync() override {}
    void reset() override;
    void initialize() override;
    std::string getTypeDescription() override {
        return "axonvex.SineGenerator";
    }

    static UnitTypeDescriptor describeType();

  private:
    OutputPort<double>* out_;
    double frequencyHz_;
    double amplitude_;
    double phase_{0.0};
};

// Test-only accessor (defined in builtinUnitsTest.cpp), granted friendship
// below so the RT-path (no-heap-allocation) regression test can read
// samples_'s capacity directly instead of a process-wide operator-new
// override (C50: that override governed allocation for the entire
// test_core binary, so a bug in it -- once, a missing nothrow overload --
// took down every other test with it). std::vector::capacity() never
// decreases on its own and increases exactly when a (re)allocation grows
// the buffer, so "capacity unchanged since a captured baseline" is a
// reliable, allocator-address-reuse-immune proxy for "no allocation
// happened since then" -- unlike comparing data() pointers, which a
// same-size free-then-malloc can satisfy by luck even after a real
// allocation round-trip.
class MovingAverageTestAccessor;

/// axonvex.MovingAverage — one Sync input port ("in", double, required), one
/// Sync output port ("out", double). Params: window (int, required, 1..4096).
class MovingAverage final : public ProcessingUnit {
  public:
    MovingAverage(const std::string& name, const nlohmann::json& params);

    friend class MovingAverageTestAccessor;

    void processSync() override;
    void processAsync() override {}
    void reset() override;
    void initialize() override;
    std::string getTypeDescription() override {
        return "axonvex.MovingAverage";
    }

    static UnitTypeDescriptor describeType();

  private:
    InputPort<double>* in_;
    OutputPort<double>* out_;
    size_t window_;
    // Fixed-size ring buffer, sized once in the constructor: window_ is known
    // at construction (unlike std::deque's push_back/pop_front, indexing a
    // pre-sized std::vector never allocates on processSync(), the scheduler
    // hot path — CLAUDE.md rule 2).
    std::vector<double> samples_;
    size_t head_{0};
    size_t filled_{0}; ///< samples written so far, saturating at window_.
    double runningSum_{0.0};
};

/// axonvex.StatsSink — one Sync input port ("in", double, required), no
/// outputs, no params. Tracks count/min/max/mean for test assertions.
class StatsSink final : public ProcessingUnit {
  public:
    StatsSink(const std::string& name, const nlohmann::json& params);

    void processSync() override;
    void processAsync() override {}
    void reset() override;
    void initialize() override;
    std::string getTypeDescription() override {
        return "axonvex.StatsSink";
    }

    // count_ is written by processSync() on whatever thread drives the unit
    // (the RT scheduler thread in normal use) and legitimately read from any
    // other thread through this accessor -- e.g. a monitoring/test thread
    // polling for "has data flowed yet" while the system runs, exactly like
    // ProcessingUnit's own execution-stats fields (V8: single writer, plain
    // field would race a concurrent reader). atomic + relaxed ordering: it is
    // a pure counter with no ordering role over sum_ or any other state.
    uint64_t sampleCount() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }
    // ponytail: sum_ has the identical shape of latent cross-thread issue if
    // a caller ever reads mean() while the system is running -- no test
    // exercises that today (TSan only caught count_, via sampleCount()), and
    // std::atomic<double> has no fetch_add under C++14. Give it the same
    // treatment (load/compare_exchange loop, or a mutex) if that read
    // pattern becomes real.
    double mean() const noexcept {
        uint64_t n = count_.load(std::memory_order_relaxed);
        return n == 0 ? 0.0 : sum_ / static_cast<double>(n);
    }

    static UnitTypeDescriptor describeType();

  private:
    InputPort<double>* in_;
    std::atomic<uint64_t> count_{0};
    double sum_{0.0};
};

/// Registers all three demo types with `factory`. Explicit call, not
/// static-init magic: keeps UnitFactory::global() free of surprise content
/// for other tests/tracks unless a caller opts in.
void registerBuiltinUnitTypes(UnitFactory& factory);

} // namespace axonvex::core::builtin
