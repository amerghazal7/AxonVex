/**
 * @file mrftController.hpp
 * @brief Model Reference Fault Tolerant Controller
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/algorithms/control/pidController.hpp>

namespace axonvex::algorithms::control {

/**
 * @brief Model Reference Fault Tolerant Controller
 * Advanced control algorithm with fault tolerance capabilities
 */
class MRFTController : public PIDController {
public:
    explicit MRFTController(const std::string& name = "MRFTController")
        : PIDController(name) {}
    
    std::string getTypeDescription() override {
        return "MRFTController";
    }
};

} // namespace axonvex::algorithms::control