/**
 * @brief Signal Processor Implementation
 */
#pragma once
#include <axonvex/algorithms/signal/digitalFilter.hpp>

namespace axonvex::algorithms::signal {
class SignalProcessor : public DigitalFilter {
public:
    explicit SignalProcessor(const std::string& name = "SignalProcessor") : DigitalFilter(name) {}
    std::string getTypeDescription() override { return "SignalProcessor"; }
};
} // namespace axonvex::algorithms::signal