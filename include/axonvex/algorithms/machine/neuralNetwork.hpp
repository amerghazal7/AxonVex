/**
 * @brief Neural Network Implementation
 */
#pragma once
#include <axonvex/algorithms/machine/mlModel.hpp>

namespace axonvex::algorithms::machine {
class NeuralNetwork : public MLModel {
public:
    explicit NeuralNetwork(const std::string& name = "NeuralNetwork") : MLModel(name) {}
    std::string getTypeDescription() override { return "NeuralNetwork"; }
};
} // namespace axonvex::algorithms::machine