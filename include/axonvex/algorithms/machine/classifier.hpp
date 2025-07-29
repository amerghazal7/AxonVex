/**
 * @brief Classifier Implementation
 */
#pragma once
#include <axonvex/algorithms/machine/mlModel.hpp>

namespace axonvex::algorithms::machine {
class Classifier : public MLModel {
public:
    explicit Classifier(const std::string& name = "Classifier") : MLModel(name) {}
    std::string getTypeDescription() override { return "Classifier"; }
};
} // namespace axonvex::algorithms::machine