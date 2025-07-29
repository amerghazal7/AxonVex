/**
 * @brief Particle Filter Implementation
 */
#pragma once
#include <axonvex/algorithms/estimation/kalmanFilter.hpp>

namespace axonvex::algorithms::estimation {
class ParticleFilter : public KalmanFilter {
public:
    explicit ParticleFilter(const std::string& name = "ParticleFilter") : KalmanFilter(name) {}
    std::string getTypeDescription() override { return "ParticleFilter"; }
};
} // namespace axonvex::algorithms::estimation