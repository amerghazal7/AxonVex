/**
 * @brief Linear Algebra Operations
 */
#pragma once
#include <axonvex/core/processingUnit.hpp>

namespace axonvex::algorithms::numerical {
class LinearAlgebra : public core::ProcessingUnit {
public:
    explicit LinearAlgebra(const std::string& name = "LinearAlgebra") : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    std::string getTypeDescription() override { return "LinearAlgebra"; }
};
} // namespace axonvex::algorithms::numerical