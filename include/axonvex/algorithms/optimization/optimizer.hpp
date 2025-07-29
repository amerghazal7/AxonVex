/**
 * @brief Optimizer Base Class
 */
#pragma once
#include <axonvex/algorithms/optimization/gradientDescent.hpp>

namespace axonvex::algorithms::optimization {
class Optimizer : public GradientDescent {
public:
    explicit Optimizer(const std::string& name = "Optimizer") : GradientDescent(name) {}
    std::string getTypeDescription() override { return "Optimizer"; }
};
} // namespace axonvex::algorithms::optimization