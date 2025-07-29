/**
 * @file algorithms.hpp
 * @brief Main header for AxonVex Algorithms Module
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

// Control algorithms
#include <axonvex/algorithms/control/pidController.hpp>
#include <axonvex/algorithms/control/mrftController.hpp>
#include <axonvex/algorithms/control/adaptiveController.hpp>

// Estimation algorithms
#include <axonvex/algorithms/estimation/kalmanFilter.hpp>
#include <axonvex/algorithms/estimation/extendedKalman.hpp>
#include <axonvex/algorithms/estimation/particleFilter.hpp>

// Signal processing algorithms
#include <axonvex/algorithms/signal/signalProcessor.hpp>
#include <axonvex/algorithms/signal/digitalFilter.hpp>
#include <axonvex/algorithms/signal/fft.hpp>

// Optimization algorithms
#include <axonvex/algorithms/optimization/optimizer.hpp>
#include <axonvex/algorithms/optimization/gradientDescent.hpp>
#include <axonvex/algorithms/optimization/geneticAlgorithm.hpp>

// Numerical algorithms
#include <axonvex/algorithms/numerical/linearAlgebra.hpp>
#include <axonvex/algorithms/numerical/integration.hpp>
#include <axonvex/algorithms/numerical/interpolation.hpp>

// Machine learning algorithms
#include <axonvex/algorithms/machine/mlModel.hpp>
#include <axonvex/algorithms/machine/neuralNetwork.hpp>
#include <axonvex/algorithms/machine/classifier.hpp>

/**
 * @namespace axonvex::algorithms
 * @brief AxonVex Algorithms Module
 * 
 * This module provides high-performance mathematical algorithms, control systems,
 * signal processing, and machine learning capabilities optimized for real-time
 * applications. All algorithms are designed to integrate seamlessly with the
 * AxonVex ProcessingUnit architecture and system port management.
 */
namespace axonvex::algorithms {

/**
 * @brief Algorithm module version information
 */
struct AlgorithmInfo {
    static constexpr const char* version = "1.0.0";
    static constexpr const char* build_date = __DATE__;
    static constexpr const char* description = "AxonVex Real-time Algorithm Suite";
};

} // namespace axonvex::algorithms