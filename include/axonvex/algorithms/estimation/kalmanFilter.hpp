/**
 * @file kalmanFilter.hpp
 * @brief Kalman Filter Implementation for State Estimation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/core/processingUnit.hpp>
#include <axonvex/core/ports.hpp>
#include <axonvex/utils/math/matrix.hpp>
#include <axonvex/core/precisionTimer.hpp>
#include <atomic>
#include <mutex>
#include <vector>

namespace axonvex::algorithms::estimation {

/**
 * @brief High-performance Kalman Filter for real-time state estimation
 * 
 * Features:
 * - Configurable state and measurement dimensions
 * - Optimized matrix operations using AxonVex math utilities
 * - Real-time performance monitoring
 * - Thread-safe operation
 * - Innovation sequence monitoring
 */
class KalmanFilter : public core::ProcessingUnit {
public:
    /**
     * @brief Kalman filter configuration
     */
    struct KalmanConfig {
        uint32_t stateDim{4};        // State vector dimension
        uint32_t measurementDim{2};  // Measurement vector dimension
        uint32_t controlDim{0};      // Control input dimension
        double processNoise{0.01};   // Process noise variance
        double measurementNoise{0.1}; // Measurement noise variance
        bool enableInnovationMonitoring{true}; // Monitor innovation sequence
        
        void reset() {
            stateDim = 4; measurementDim = 2; controlDim = 0;
            processNoise = 0.01; measurementNoise = 0.1;
            enableInnovationMonitoring = true;
        }
    };

    /**
     * @brief Kalman filter statistics
     */
    struct KalmanStats {
        uint64_t updateCount{0};
        uint64_t predictionCount{0};
        double avgUpdateTime{0.0};
        double maxUpdateTime{0.0};
        double innovationMagnitude{0.0};
        double stateCovariance{0.0};
        bool filterDiverged{false};
        
        void reset() {
            updateCount = 0; predictionCount = 0;
            avgUpdateTime = 0.0; maxUpdateTime = 0.0;
            innovationMagnitude = 0.0; stateCovariance = 0.0;
            filterDiverged = false;
        }
    };

private:
    // Input/Output ports
    core::InputPort<std::vector<double>>* measurementInput_;
    core::InputPort<std::vector<double>>* controlInput_;
    core::OutputPort<std::vector<double>>* stateOutput_;
    core::OutputPort<std::vector<double>>* covarianceOutput_;
    core::OutputPort<KalmanStats>* statsOutput_;

    // Filter configuration and state
    KalmanConfig config_;
    KalmanStats stats_;
    mutable std::mutex filterMutex_;

    // Kalman filter matrices
    utils::math::Matrix stateTransition_;     // F - State transition matrix
    utils::math::Matrix controlMatrix_;       // B - Control input matrix  
    utils::math::Matrix measurementMatrix_;   // H - Measurement matrix
    utils::math::Matrix processNoise_;        // Q - Process noise covariance
    utils::math::Matrix measurementNoise_;    // R - Measurement noise covariance
    
    // Filter state
    utils::math::Matrix state_;               // x - State estimate
    utils::math::Matrix covariance_;          // P - Error covariance matrix
    utils::math::Matrix innovation_;          // Innovation sequence
    
    // Performance tracking
    std::atomic<uint64_t> totalUpdates_{0};
    core::PrecisionTimer updateTimer_;
    
    // Divergence monitoring
    double covarianceTrace_{0.0};
    double maxCovarianceTrace_{1000.0};

public:
    /**
     * @brief Constructor
     */
    explicit KalmanFilter(const std::string& name = "KalmanFilter")
        : ProcessingUnit(name) {
        
        // Create system ports
        measurementInput_ = createInputPort<std::vector<double>>(3000, "measurements");
        controlInput_ = createInputPort<std::vector<double>>(3001, "control_input");
        stateOutput_ = createOutputPort<std::vector<double>>(3002, "state_estimate");
        covarianceOutput_ = createOutputPort<std::vector<double>>(3003, "covariance");
        statsOutput_ = createOutputPort<KalmanStats>(3004, "kalman_stats");
        
        // Initialize with default configuration
        config_.reset();
        initializeMatrices();
    }

    /**
     * @brief Initialize the Kalman filter
     */
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        resetFilter();
    }

    /**
     * @brief Main Kalman filter algorithm (synchronous processing)
     */
    void processSync() override {
        updateTimer_.start();
        setState(ExecutionState::RUNNING);

        bool hasMeasurement = measurementInput_->hasNewData();
        bool hasControl = controlInput_->hasNewData();

        if (!hasMeasurement) {
            // Prediction step only
            predict(hasControl);
        } else {
            // Full predict-update cycle
            predict(hasControl);
            update();
        }

        // Output current state estimate
        outputResults();

        // Update performance metrics
        updateTimer_.stop();
        totalUpdates_.fetch_add(1);
        
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(
            updateTimer_.getElapsedNanoseconds()));
    }

    /**
     * @brief Asynchronous processing
     */
    void processAsync() override {
        processSync();
    }

    /**
     * @brief Configure the filter
     */
    void configure(const KalmanConfig& config) {
        std::lock_guard<std::mutex> lock(filterMutex_);
        config_ = config;
        initializeMatrices();
        resetFilter();
    }

    /**
     * @brief Set state transition matrix
     */
    void setStateTransitionMatrix(const utils::math::Matrix& F) {
        std::lock_guard<std::mutex> lock(filterMutex_);
        if (F.rows() == config_.stateDim && F.cols() == config_.stateDim) {
            stateTransition_ = F;
        }
    }

    /**
     * @brief Set measurement matrix
     */
    void setMeasurementMatrix(const utils::math::Matrix& H) {
        std::lock_guard<std::mutex> lock(filterMutex_);
        if (H.rows() == config_.measurementDim && H.cols() == config_.stateDim) {
            measurementMatrix_ = H;
        }
    }

    /**
     * @brief Get current state estimate
     */
    std::vector<double> getStateEstimate() const {
        std::lock_guard<std::mutex> lock(filterMutex_);
        std::vector<double> stateVec(config_.stateDim);
        for (size_t i = 0; i < config_.stateDim; ++i) {
            stateVec[i] = state_(i, 0);
        }
        return stateVec;
    }

    /**
     * @brief Get filter statistics
     */
    KalmanStats getStatistics() const {
        std::lock_guard<std::mutex> lock(filterMutex_);
        return stats_;
    }

    /**
     * @brief Reset filter state
     */
    void resetFilter() {
        std::lock_guard<std::mutex> lock(filterMutex_);
        
        // Reset state to zero
        state_ = utils::math::Matrix(config_.stateDim, 1);
        
        // Reset covariance to identity
        covariance_ = utils::math::Matrix::identity(config_.stateDim);
        
        stats_.reset();
        totalUpdates_.store(0);
        covarianceTrace_ = 0.0;
    }

    /**
     * @brief ProcessingUnit interface implementations
     */
    void reset() override {
        resetFilter();
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "KalmanFilter";
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
    }

    /**
     * @brief Get total updates performed
     */
    uint64_t getTotalUpdates() const {
        return totalUpdates_.load();
    }

private:
    /**
     * @brief Initialize filter matrices
     */
    void initializeMatrices() {
        // Initialize matrices with default configurations
        stateTransition_ = utils::math::Matrix::identity(config_.stateDim);
        measurementMatrix_ = utils::math::Matrix::identity(config_.measurementDim, config_.stateDim);
        
        // Process noise (Q matrix)
        processNoise_ = utils::math::Matrix::identity(config_.stateDim) * config_.processNoise;
        
        // Measurement noise (R matrix)
        measurementNoise_ = utils::math::Matrix::identity(config_.measurementDim) * config_.measurementNoise;
        
        // Control matrix (if needed)
        if (config_.controlDim > 0) {
            controlMatrix_ = utils::math::Matrix(config_.stateDim, config_.controlDim);
        }
        
        // Initialize state and covariance
        state_ = utils::math::Matrix(config_.stateDim, 1);
        covariance_ = utils::math::Matrix::identity(config_.stateDim);
    }

    /**
     * @brief Prediction step
     */
    void predict(bool hasControl) {
        std::lock_guard<std::mutex> lock(filterMutex_);
        
        // State prediction: x = F * x + B * u
        state_ = stateTransition_ * state_;
        
        if (hasControl && config_.controlDim > 0) {
            auto controlVec = controlInput_->read();
            if (controlVec.size() == config_.controlDim) {
                utils::math::Matrix u(config_.controlDim, 1);
                for (size_t i = 0; i < config_.controlDim; ++i) {
                    u(i, 0) = controlVec[i];
                }
                state_ = state_ + controlMatrix_ * u;
            }
        }
        
        // Covariance prediction: P = F * P * F' + Q
        covariance_ = stateTransition_ * covariance_ * stateTransition_.transpose() + processNoise_;
        
        // Monitor for divergence
        covarianceTrace_ = covariance_.trace();
        if (covarianceTrace_ > maxCovarianceTrace_) {
            stats_.filterDiverged = true;
        }
        
        stats_.predictionCount++;
        
        if (hasControl) {
            controlInput_->clearNewDataFlag();
        }
    }

    /**
     * @brief Update step (correction)
     */
    void update() {
        std::lock_guard<std::mutex> lock(filterMutex_);
        
        auto measurementVec = measurementInput_->read();
        if (measurementVec.size() != config_.measurementDim) {
            measurementInput_->clearNewDataFlag();
            return;
        }
        
        // Convert measurement to matrix
        utils::math::Matrix z(config_.measurementDim, 1);
        for (size_t i = 0; i < config_.measurementDim; ++i) {
            z(i, 0) = measurementVec[i];
        }
        
        // Innovation: y = z - H * x
        innovation_ = z - measurementMatrix_ * state_;
        
        // Innovation covariance: S = H * P * H' + R
        auto S = measurementMatrix_ * covariance_ * measurementMatrix_.transpose() + measurementNoise_;
        
        // Kalman gain: K = P * H' * S^(-1)
        auto K = covariance_ * measurementMatrix_.transpose() * S.inverse();
        
        // State update: x = x + K * y
        state_ = state_ + K * innovation_;
        
        // Covariance update: P = (I - K * H) * P
        auto I = utils::math::Matrix::identity(config_.stateDim);
        covariance_ = (I - K * measurementMatrix_) * covariance_;
        
        // Update statistics
        stats_.updateCount++;
        if (config_.enableInnovationMonitoring) {
            stats_.innovationMagnitude = innovation_.norm();
        }
        stats_.stateCovariance = covariance_.trace();
        
        auto updateTime = updateTimer_.getElapsedNanoseconds().count() / 1000.0; // microseconds
        stats_.avgUpdateTime = (stats_.avgUpdateTime * (stats_.updateCount - 1) + updateTime) / stats_.updateCount;
        stats_.maxUpdateTime = std::max(stats_.maxUpdateTime, updateTime);
        
        measurementInput_->clearNewDataFlag();
    }

    /**
     * @brief Output current results
     */
    void outputResults() {
        std::lock_guard<std::mutex> lock(filterMutex_);
        
        // Output state estimate
        std::vector<double> stateVec(config_.stateDim);
        for (size_t i = 0; i < config_.stateDim; ++i) {
            stateVec[i] = state_(i, 0);
        }
        stateOutput_->write(stateVec);
        
        // Output covariance diagonal (for monitoring)
        std::vector<double> covVec(config_.stateDim);
        for (size_t i = 0; i < config_.stateDim; ++i) {
            covVec[i] = covariance_(i, i);
        }
        covarianceOutput_->write(covVec);
        
        // Output statistics
        statsOutput_->write(stats_);
    }
};

} // namespace axonvex::algorithms::estimation