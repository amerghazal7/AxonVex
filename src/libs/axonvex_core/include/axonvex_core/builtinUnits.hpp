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

#include <deque>

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

/// axonvex.MovingAverage — one Sync input port ("in", double, required), one
/// Sync output port ("out", double). Params: window (int, required, 1..4096).
class MovingAverage final : public ProcessingUnit {
  public:
    MovingAverage(const std::string& name, const nlohmann::json& params);

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
    std::deque<double> samples_;
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

    uint64_t sampleCount() const noexcept {
        return count_;
    }
    double mean() const noexcept {
        return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_);
    }

    static UnitTypeDescriptor describeType();

  private:
    InputPort<double>* in_;
    uint64_t count_{0};
    double sum_{0.0};
};

/// Registers all three demo types with `factory`. Explicit call, not
/// static-init magic: keeps UnitFactory::global() free of surprise content
/// for other tests/tracks unless a caller opts in.
void registerBuiltinUnitTypes(UnitFactory& factory);

} // namespace axonvex::core::builtin
