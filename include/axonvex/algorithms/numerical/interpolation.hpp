/**
 * @brief Interpolation Algorithms
 */
#pragma once
#include <axonvex/core/processingUnit.hpp>

namespace axonvex::algorithms::numerical {
class Interpolation : public core::ProcessingUnit {
public:
    explicit Interpolation(const std::string& name = "Interpolation") : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    std::string getTypeDescription() override { return "Interpolation"; }
};
} // namespace axonvex::algorithms::numerical