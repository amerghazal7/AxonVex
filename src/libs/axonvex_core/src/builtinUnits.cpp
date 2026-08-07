/**
 * @file builtinUnits.cpp
 * @brief Trivial reference ProcessingUnit types (implementation).
 */

#include "axonvex_core/builtinUnits.hpp"

#include <algorithm>
#include <cmath>

namespace axonvex::core::builtin {

namespace {
constexpr int kInputIndex = 0;
constexpr int kOutputIndex = 0;
} // namespace

// ---------------------------------------------------------------- SineGenerator

SineGenerator::SineGenerator(const std::string& name, const nlohmann::json& params)
    : ProcessingUnit(name), out_(createOutputPort<double>(kOutputIndex, "out")),
      frequencyHz_(params.value("frequencyHz", 1.0)), amplitude_(params.value("amplitude", 1.0)) {}

void SineGenerator::initialize() {
    phase_ = 0.0;
    setState(ExecutionState::INITIALIZED);
}

void SineGenerator::processSync() {
    // Demo unit: period is unused for a deterministic phase increment, this
    // is intentionally simplistic (not a real oscillator model).
    out_->write(amplitude_ * std::sin(phase_));
    phase_ += 2.0 * M_PI * frequencyHz_ * 0.001; // 1ms nominal step per tick
}

void SineGenerator::reset() {
    phase_ = 0.0;
}

UnitTypeDescriptor SineGenerator::describeType() {
    UnitTypeDescriptor d;
    d.typeName = "axonvex.SineGenerator";
    d.description = "Reference unit: emits amplitude*sin(phase) on 'out' every tick.";
    d.ports.push_back(PortDescriptor::make<double>(
        kOutputIndex, "out", PortDescriptor::Direction::Output, PortDescriptor::Kind::Sync));
    d.parameters.push_back({"frequencyHz", ParamDescriptor::Type::Double, 1.0,
                            "Oscillation frequency in Hz", nlohmann::json{{"min", 0.0}}});
    d.parameters.push_back({"amplitude", ParamDescriptor::Type::Double, 1.0, "Peak amplitude",
                            nlohmann::json{{"min", 0.0}}});
    return d;
}

// ---------------------------------------------------------------- MovingAverage

MovingAverage::MovingAverage(const std::string& name, const nlohmann::json& params)
    : ProcessingUnit(name), in_(createInputPort<double>(kInputIndex, "in")),
      out_(createOutputPort<double>(kOutputIndex, "out")),
      window_(static_cast<size_t>(params.at("window").get<int64_t>())), samples_(window_, 0.0) {}

void MovingAverage::initialize() {
    std::fill(samples_.begin(), samples_.end(), 0.0);
    head_ = 0;
    filled_ = 0;
    runningSum_ = 0.0;
    setState(ExecutionState::INITIALIZED);
}

void MovingAverage::processSync() {
    double value = in_->read();
    if (window_ == 0) {   // guards the ring's modulo; matches the old
        out_->write(0.0); // deque code's behavior (never accumulates).
        return;
    }
    if (filled_ < window_) {
        runningSum_ += value;
        ++filled_;
    } else {
        runningSum_ += value - samples_[head_];
    }
    samples_[head_] = value;
    head_ = (head_ + 1) % window_;
    out_->write(runningSum_ / static_cast<double>(filled_));
}

void MovingAverage::reset() {
    std::fill(samples_.begin(), samples_.end(), 0.0);
    head_ = 0;
    filled_ = 0;
    runningSum_ = 0.0;
}

UnitTypeDescriptor MovingAverage::describeType() {
    UnitTypeDescriptor d;
    d.typeName = "axonvex.MovingAverage";
    d.description = "Reference unit: sliding-window mean of 'in' written to 'out' every tick.";
    d.ports.push_back(PortDescriptor::make<double>(kInputIndex, "in",
                                                   PortDescriptor::Direction::Input,
                                                   PortDescriptor::Kind::Sync, /*required=*/true));
    d.ports.push_back(PortDescriptor::make<double>(
        kOutputIndex, "out", PortDescriptor::Direction::Output, PortDescriptor::Kind::Sync));
    d.parameters.push_back({"window", ParamDescriptor::Type::Int, nlohmann::json(),
                            "Sliding window size in samples",
                            nlohmann::json{{"min", 1}, {"max", 4096}}});
    return d;
}

// ---------------------------------------------------------------- StatsSink

StatsSink::StatsSink(const std::string& name, const nlohmann::json& /*params*/)
    : ProcessingUnit(name), in_(createInputPort<double>(kInputIndex, "in")) {}

void StatsSink::initialize() {
    count_ = 0;
    sum_ = 0.0;
    setState(ExecutionState::INITIALIZED);
}

void StatsSink::processSync() {
    sum_ += in_->read();
    ++count_;
}

void StatsSink::reset() {
    count_ = 0;
    sum_ = 0.0;
}

UnitTypeDescriptor StatsSink::describeType() {
    UnitTypeDescriptor d;
    d.typeName = "axonvex.StatsSink";
    d.description =
        "Reference unit: accumulates count/sum of 'in' for test assertions; no outputs.";
    d.ports.push_back(PortDescriptor::make<double>(kInputIndex, "in",
                                                   PortDescriptor::Direction::Input,
                                                   PortDescriptor::Kind::Sync, /*required=*/true));
    return d;
}

// ---------------------------------------------------------------- registration

void registerBuiltinUnitTypes(UnitFactory& factory) {
    factory.registerType(
        SineGenerator::describeType(), [](const std::string& name, const nlohmann::json& params) {
            return std::unique_ptr<ProcessingUnit>(new SineGenerator(name, params));
        });
    factory.registerType(
        MovingAverage::describeType(), [](const std::string& name, const nlohmann::json& params) {
            return std::unique_ptr<ProcessingUnit>(new MovingAverage(name, params));
        });
    factory.registerType(StatsSink::describeType(),
                         [](const std::string& name, const nlohmann::json& params) {
                             return std::unique_ptr<ProcessingUnit>(new StatsSink(name, params));
                         });
}

} // namespace axonvex::core::builtin
