/**
 * @file gradientDescent.hpp
 * @brief Gradient Descent Optimization Algorithm
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/core/processingUnit.hpp>
#include <axonvex/core/ports.hpp>
#include <axonvex/core/precisionTimer.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <cmath>

namespace axonvex::algorithms::optimization {

/**
 * @brief High-performance gradient descent optimizer for real-time applications
 * 
 * Features:
 * - Multiple optimization algorithms (SGD, Adam, RMSprop)
 * - Adaptive learning rate
 * - Momentum and acceleration
 * - Real-time parameter updates
 * - Convergence monitoring
 */
class GradientDescent : public core::ProcessingUnit {
public:
    /**
     * @brief Optimization algorithm types
     */
    enum class OptimizerType {
        SGD,        // Stochastic Gradient Descent
        SGD_MOMENTUM,  // SGD with momentum
        ADAM,       // Adam optimizer
        RMSPROP,    // RMSprop optimizer
        ADAGRAD     // Adagrad optimizer
    };

    /**
     * @brief Optimizer configuration
     */
    struct OptimizerConfig {
        OptimizerType type{OptimizerType::ADAM};
        uint32_t parameterDim{10};        // Number of parameters to optimize
        double learningRate{0.001};       // Initial learning rate
        double beta1{0.9};                // Momentum decay rate (Adam)
        double beta2{0.999};              // Second moment decay rate (Adam)
        double epsilon{1e-8};             // Numerical stability constant
        double gradientClipThreshold{1.0}; // Gradient clipping threshold
        bool enableAdaptiveLR{true};      // Enable adaptive learning rate
        double minLearningRate{1e-6};     // Minimum learning rate
        
        void reset() {
            type = OptimizerType::ADAM;
            parameterDim = 10; learningRate = 0.001;
            beta1 = 0.9; beta2 = 0.999; epsilon = 1e-8;
            gradientClipThreshold = 1.0; enableAdaptiveLR = true;
            minLearningRate = 1e-6;
        }
    };

    /**
     * @brief Optimization statistics
     */
    struct OptimizerStats {
        uint64_t iterationCount{0};
        double currentLoss{0.0};
        double bestLoss{std::numeric_limits<double>::max()};
        double gradientNorm{0.0};
        double currentLearningRate{0.0};
        bool hasConverged{false};
        double convergenceRate{0.0};
        
        void reset() {
            iterationCount = 0; currentLoss = 0.0;
            bestLoss = std::numeric_limits<double>::max();
            gradientNorm = 0.0; currentLearningRate = 0.0;
            hasConverged = false; convergenceRate = 0.0;
        }
    };

private:
    // Input/Output ports
    core::InputPort<std::vector<double>>* gradientInput_;
    core::InputPort<double>* lossInput_;
    core::OutputPort<std::vector<double>>* parametersOutput_;
    core::OutputPort<OptimizerStats>* statsOutput_;

    // Optimizer configuration and state
    OptimizerConfig config_;
    OptimizerStats stats_;
    mutable std::mutex optimizerMutex_;

    // Optimization state
    std::vector<double> parameters_;           // Current parameters
    std::vector<double> momentum_;             // Momentum terms (SGD momentum, Adam m)
    std::vector<double> velocity_;             // Velocity terms (Adam v, RMSprop)
    std::vector<double> gradientHistory_;      // Gradient accumulation (Adagrad)
    
    // Adaptive learning rate
    double currentLearningRate_;
    double initialLearningRate_;
    
    // Convergence monitoring
    std::vector<double> lossHistory_;
    static constexpr size_t LOSS_HISTORY_SIZE = 10;
    double convergenceThreshold_{1e-6};
    
    // Performance tracking
    std::atomic<uint64_t> totalIterations_{0};
    core::PrecisionTimer optimizationTimer_;

public:
    /**
     * @brief Constructor
     */
    explicit GradientDescent(const std::string& name = "GradientDescent")
        : ProcessingUnit(name) {
        
        // Create system ports
        gradientInput_ = createInputPort<std::vector<double>>(4000, "gradients");
        lossInput_ = createInputPort<double>(4001, "loss_value");
        parametersOutput_ = createOutputPort<std::vector<double>>(4002, "parameters");
        statsOutput_ = createOutputPort<OptimizerStats>(4003, "optimizer_stats");
        
        // Initialize with default configuration
        config_.reset();
        initializeOptimizer();
    }

    /**
     * @brief Initialize the optimizer
     */
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        resetOptimizer();
    }

    /**
     * @brief Main optimization algorithm (synchronous processing)
     */
    void processSync() override {
        optimizationTimer_.start();
        setState(ExecutionState::RUNNING);

        // Check for new gradient data
        if (!gradientInput_->hasNewData()) {
            optimizationTimer_.stop();
            return;
        }

        // Read gradient and loss
        auto gradients = gradientInput_->read();
        double loss = 0.0;
        if (lossInput_->hasNewData()) {
            loss = lossInput_->read();
            lossInput_->clearNewDataFlag();
        }

        // Validate gradient dimensions
        if (gradients.size() != config_.parameterDim) {
            gradientInput_->clearNewDataFlag();
            optimizationTimer_.stop();
            return;
        }

        // Apply gradient clipping
        clipGradients(gradients);

        // Perform optimization step
        optimizationStep(gradients, loss);

        // Output updated parameters
        parametersOutput_->write(parameters_);

        // Update and output statistics
        updateStatistics(loss, gradients);
        statsOutput_->write(stats_);

        gradientInput_->clearNewDataFlag();
        totalIterations_.fetch_add(1);

        optimizationTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(
            optimizationTimer_.getElapsedNanoseconds()));
    }

    /**
     * @brief Asynchronous processing
     */
    void processAsync() override {
        processSync();
    }

    /**
     * @brief Configure the optimizer
     */
    void configure(const OptimizerConfig& config) {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        config_ = config;
        initializeOptimizer();
        resetOptimizer();
    }

    /**
     * @brief Set initial parameters
     */
    void setParameters(const std::vector<double>& params) {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        if (params.size() == config_.parameterDim) {
            parameters_ = params;
        }
    }

    /**
     * @brief Get current parameters
     */
    std::vector<double> getParameters() const {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        return parameters_;
    }

    /**
     * @brief Get optimizer statistics
     */
    OptimizerStats getStatistics() const {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        return stats_;
    }

    /**
     * @brief Reset optimizer state
     */
    void resetOptimizer() {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        
        // Reset parameters to zero (or could be random initialization)
        std::fill(parameters_.begin(), parameters_.end(), 0.0);
        std::fill(momentum_.begin(), momentum_.end(), 0.0);
        std::fill(velocity_.begin(), velocity_.end(), 0.0);
        std::fill(gradientHistory_.begin(), gradientHistory_.end(), 0.0);
        
        // Reset learning rate
        currentLearningRate_ = initialLearningRate_;
        
        // Clear loss history
        lossHistory_.clear();
        
        stats_.reset();
        stats_.currentLearningRate = currentLearningRate_;
        totalIterations_.store(0);
    }

    /**
     * @brief ProcessingUnit interface implementations
     */
    void reset() override {
        resetOptimizer();
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "GradientDescent";
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
    }

    /**
     * @brief Get total iterations performed
     */
    uint64_t getTotalIterations() const {
        return totalIterations_.load();
    }

private:
    /**
     * @brief Initialize optimizer state vectors
     */
    void initializeOptimizer() {
        parameters_.resize(config_.parameterDim, 0.0);
        momentum_.resize(config_.parameterDim, 0.0);
        velocity_.resize(config_.parameterDim, 0.0);
        gradientHistory_.resize(config_.parameterDim, 0.0);
        
        currentLearningRate_ = config_.learningRate;
        initialLearningRate_ = config_.learningRate;
        
        lossHistory_.reserve(LOSS_HISTORY_SIZE);
    }

    /**
     * @brief Apply gradient clipping
     */
    void clipGradients(std::vector<double>& gradients) {
        double gradNorm = 0.0;
        for (double grad : gradients) {
            gradNorm += grad * grad;
        }
        gradNorm = std::sqrt(gradNorm);
        
        if (gradNorm > config_.gradientClipThreshold) {
            double scale = config_.gradientClipThreshold / gradNorm;
            for (double& grad : gradients) {
                grad *= scale;
            }
        }
    }

    /**
     * @brief Perform optimization step based on algorithm type
     */
    void optimizationStep(const std::vector<double>& gradients, double loss) {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        
        switch (config_.type) {
            case OptimizerType::SGD:
                applySGD(gradients);
                break;
            case OptimizerType::SGD_MOMENTUM:
                applySGDMomentum(gradients);
                break;
            case OptimizerType::ADAM:
                applyAdam(gradients);
                break;
            case OptimizerType::RMSPROP:
                applyRMSprop(gradients);
                break;
            case OptimizerType::ADAGRAD:
                applyAdagrad(gradients);
                break;
        }
        
        // Update adaptive learning rate if enabled
        if (config_.enableAdaptiveLR) {
            updateLearningRate(loss);
        }
    }

    /**
     * @brief Apply standard SGD update
     */
    void applySGD(const std::vector<double>& gradients) {
        for (size_t i = 0; i < config_.parameterDim; ++i) {
            parameters_[i] -= currentLearningRate_ * gradients[i];
        }
    }

    /**
     * @brief Apply SGD with momentum update
     */
    void applySGDMomentum(const std::vector<double>& gradients) {
        for (size_t i = 0; i < config_.parameterDim; ++i) {
            momentum_[i] = config_.beta1 * momentum_[i] + currentLearningRate_ * gradients[i];
            parameters_[i] -= momentum_[i];
        }
    }

    /**
     * @brief Apply Adam optimizer update
     */
    void applyAdam(const std::vector<double>& gradients) {
        double t = static_cast<double>(stats_.iterationCount + 1);
        
        for (size_t i = 0; i < config_.parameterDim; ++i) {
            // Update biased first moment estimate
            momentum_[i] = config_.beta1 * momentum_[i] + (1.0 - config_.beta1) * gradients[i];
            
            // Update biased second raw moment estimate
            velocity_[i] = config_.beta2 * velocity_[i] + (1.0 - config_.beta2) * gradients[i] * gradients[i];
            
            // Compute bias-corrected first moment estimate
            double m_hat = momentum_[i] / (1.0 - std::pow(config_.beta1, t));
            
            // Compute bias-corrected second raw moment estimate
            double v_hat = velocity_[i] / (1.0 - std::pow(config_.beta2, t));
            
            // Update parameters
            parameters_[i] -= currentLearningRate_ * m_hat / (std::sqrt(v_hat) + config_.epsilon);
        }
    }

    /**
     * @brief Apply RMSprop optimizer update
     */
    void applyRMSprop(const std::vector<double>& gradients) {
        for (size_t i = 0; i < config_.parameterDim; ++i) {
            velocity_[i] = config_.beta2 * velocity_[i] + (1.0 - config_.beta2) * gradients[i] * gradients[i];
            parameters_[i] -= currentLearningRate_ * gradients[i] / (std::sqrt(velocity_[i]) + config_.epsilon);
        }
    }

    /**
     * @brief Apply Adagrad optimizer update
     */
    void applyAdagrad(const std::vector<double>& gradients) {
        for (size_t i = 0; i < config_.parameterDim; ++i) {
            gradientHistory_[i] += gradients[i] * gradients[i];
            parameters_[i] -= currentLearningRate_ * gradients[i] / (std::sqrt(gradientHistory_[i]) + config_.epsilon);
        }
    }

    /**
     * @brief Update adaptive learning rate
     */
    void updateLearningRate(double loss) {
        // Simple adaptive learning rate based on loss improvement
        if (lossHistory_.size() >= 2) {
            double previousLoss = lossHistory_[lossHistory_.size() - 2];
            if (loss > previousLoss) {
                // Loss increased, reduce learning rate
                currentLearningRate_ *= 0.95;
                currentLearningRate_ = std::max(currentLearningRate_, config_.minLearningRate);
            } else if (loss < previousLoss * 0.99) {
                // Good improvement, slightly increase learning rate
                currentLearningRate_ *= 1.01;
            }
        }
    }

    /**
     * @brief Update optimization statistics
     */
    void updateStatistics(double loss, const std::vector<double>& gradients) {
        std::lock_guard<std::mutex> lock(optimizerMutex_);
        
        stats_.iterationCount++;
        stats_.currentLoss = loss;
        stats_.currentLearningRate = currentLearningRate_;
        
        // Update best loss
        if (loss < stats_.bestLoss) {
            stats_.bestLoss = loss;
        }
        
        // Calculate gradient norm
        stats_.gradientNorm = 0.0;
        for (double grad : gradients) {
            stats_.gradientNorm += grad * grad;
        }
        stats_.gradientNorm = std::sqrt(stats_.gradientNorm);
        
        // Update loss history
        lossHistory_.push_back(loss);
        if (lossHistory_.size() > LOSS_HISTORY_SIZE) {
            lossHistory_.erase(lossHistory_.begin());
        }
        
        // Check convergence
        if (lossHistory_.size() >= LOSS_HISTORY_SIZE) {
            double avgRecentLoss = 0.0;
            for (size_t i = lossHistory_.size() - 5; i < lossHistory_.size(); ++i) {
                avgRecentLoss += lossHistory_[i];
            }
            avgRecentLoss /= 5.0;
            
            if (std::abs(loss - avgRecentLoss) < convergenceThreshold_) {
                stats_.hasConverged = true;
            }
            
            // Calculate convergence rate
            if (lossHistory_.size() >= 2) {
                stats_.convergenceRate = std::abs(lossHistory_.back() - lossHistory_[lossHistory_.size() - 2]);
            }
        }
    }
};

} // namespace axonvex::algorithms::optimization