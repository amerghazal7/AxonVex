/**
 * @brief Numerical Integration
 */
#pragma once
#include <axonvex/core/processingUnit.hpp>

namespace axonvex::algorithms::numerical {
class Integration : public core::ProcessingUnit {
public:
    explicit Integration(const std::string& name = "Integration") : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    std::string getTypeDescription() override { return "Integration"; }
};
} // namespace axonvex::algorithms::numerical