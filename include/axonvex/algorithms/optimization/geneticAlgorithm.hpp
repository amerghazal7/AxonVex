/**
 * @brief Genetic Algorithm Implementation
 */
#pragma once
#include <axonvex/algorithms/optimization/gradientDescent.hpp>

namespace axonvex::algorithms::optimization {
class GeneticAlgorithm : public GradientDescent {
public:
    explicit GeneticAlgorithm(const std::string& name = "GeneticAlgorithm") : GradientDescent(name) {}
    std::string getTypeDescription() override { return "GeneticAlgorithm"; }
};
} // namespace axonvex::algorithms::optimization