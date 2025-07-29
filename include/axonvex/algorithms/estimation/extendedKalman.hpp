/**
 * @brief Extended Kalman Filter Implementation
 */
#pragma once
#include <axonvex/algorithms/estimation/kalmanFilter.hpp>

namespace axonvex::algorithms::estimation {
class ExtendedKalman : public KalmanFilter {
public:
    explicit ExtendedKalman(const std::string& name = "ExtendedKalman") : KalmanFilter(name) {}
    std::string getTypeDescription() override { return "ExtendedKalman"; }
};
} // namespace axonvex::algorithms::estimation