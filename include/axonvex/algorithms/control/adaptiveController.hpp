/**
 * @file adaptiveController.hpp
 * @brief Adaptive Controller Implementation
 */

#pragma once

#include <axonvex/algorithms/control/pidController.hpp>

namespace axonvex::algorithms::control {

class AdaptiveController : public PIDController {
public:
    explicit AdaptiveController(const std::string& name = "AdaptiveController")
        : PIDController(name) {}
    
    std::string getTypeDescription() override {
        return "AdaptiveController";
    }
};

} // namespace axonvex::algorithms::control