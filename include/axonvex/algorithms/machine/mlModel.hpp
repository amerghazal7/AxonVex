/**
 * @brief Machine Learning Model Interface
 */
#pragma once
#include <axonvex/core/processingUnit.hpp>

namespace axonvex::algorithms::machine {
class MLModel : public core::ProcessingUnit {
public:
    explicit MLModel(const std::string& name = "MLModel") : ProcessingUnit(name) {}
    void processSync() override {}
    void processAsync() override {}
    void reset() override {}
    std::string getTypeDescription() override { return "MLModel"; }
};
} // namespace axonvex::algorithms::machine