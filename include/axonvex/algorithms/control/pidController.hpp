/**
 * @file pidController.hpp
 * @brief PID Controller Algorithm Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/core/processingUnit.hpp>
#include <axonvex/core/ports.hpp>
#include <axonvex/core/precisionTimer.hpp>
#include <atomic>
#include <chrono>
#include <mutex>

namespace axonvex::algorithms::control {

/**
 * @brief High-performance PID Controller for real-time control systems
 * 
 * Features:
 * - Microsecond-precision timing
 * - Anti-windup protection
 * - Derivative kick prevention
 * - Configurable output limits
 * - Real-time safe implementation
 */
class PIDController : public core::ProcessingUnit {
public:
    /**
     * @brief PID Controller parameters
     */
    struct PIDParams {
        double kp{1.0};           // Proportional gain
        double ki{0.1};           // Integral gain
        double kd{0.05};          // Derivative gain
        double outputMin{-100.0}; // Minimum output limit
        double outputMax{100.0};  // Maximum output limit
        bool enableAntiWindup{true}; // Anti-windup protection
        
        void reset() {
            kp = 1.0; ki = 0.1; kd = 0.05;
            outputMin = -100.0; outputMax = 100.0;
            enableAntiWindup = true;
        }
    };

    /**
     * @brief PID Controller statistics
     */
    struct PIDStats {
        double lastError{0.0};
        double integral{0.0};
        double derivative{0.0};
        double lastOutput{0.0};
        uint64_t updateCount{0};
        std::chrono::microseconds avgUpdateTime{0};
        
        void reset() {
            lastError = 0.0; integral = 0.0; derivative = 0.0;
            lastOutput = 0.0; updateCount = 0;
            avgUpdateTime = std::chrono::microseconds{0};
        }
    };

private:
    // Input/Output ports
    core::InputPort<double>* setpointInput_;
    core::InputPort<double>* feedbackInput_;
    core::OutputPort<double>* controlOutput_;
    core::OutputPort<PIDStats>* statsOutput_;

    // PID parameters and state
    PIDParams params_;
    PIDStats stats_;
    mutable std::mutex stateMutex_;

    // Timing
    core::PrecisionTimer updateTimer_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
    bool firstUpdate_{true};

    // Performance tracking
    std::atomic<uint64_t> totalUpdates_{0};
    std::atomic<uint64_t> saturatedOutputs_{0};

public:
    /**
     * @brief Constructor
     */
    explicit PIDController(const std::string& name = "PIDController")
        : ProcessingUnit(name) {
        
        // Create system ports that can be exposed at system level
        setpointInput_ = createInputPort<double>(1000, "setpoint");
        feedbackInput_ = createInputPort<double>(1001, "feedback");
        controlOutput_ = createOutputPort<double>(1002, "control_output");
        statsOutput_ = createOutputPort<PIDStats>(1003, "pid_stats");
        
        // Initialize timing
        lastUpdateTime_ = std::chrono::steady_clock::now();
    }

    /**
     * @brief Initialize the PID controller
     */
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        resetController();
    }

    /**
     * @brief Main PID control algorithm (synchronous processing)
     */
    void processSync() override {
        updateTimer_.start();
        setState(ExecutionState::RUNNING);

        // Check for new inputs
        if (!setpointInput_->hasNewData() || !feedbackInput_->hasNewData()) {
            updateTimer_.stop();
            return;
        }

        // Read inputs
        double setpoint = setpointInput_->read();
        double feedback = feedbackInput_->read();

        // Calculate time delta
        auto currentTime = std::chrono::steady_clock::now();
        double dt = 0.001; // Default 1ms for first update
        
        if (!firstUpdate_) {
            auto timeDelta = std::chrono::duration_cast<std::chrono::microseconds>(
                currentTime - lastUpdateTime_);
            dt = timeDelta.count() / 1e6; // Convert to seconds
        }
        firstUpdate_ = false;
        lastUpdateTime_ = currentTime;

        // Compute PID control output
        double output = computePID(setpoint, feedback, dt);

        // Write outputs
        controlOutput_->write(output);
        
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            stats_.lastOutput = output;
            stats_.updateCount++;
            statsOutput_->write(stats_);
        }

        // Clear input flags
        setpointInput_->clearNewDataFlag();
        feedbackInput_->clearNewDataFlag();

        // Update performance metrics
        updateTimer_.stop();
        totalUpdates_.fetch_add(1);
        
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(
            updateTimer_.getElapsedNanoseconds()));
    }

    /**
     * @brief Asynchronous processing (same as sync for this controller)
     */
    void processAsync() override {
        processSync();
    }

    /**
     * @brief Set PID parameters
     */
    void setParameters(const PIDParams& params) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        params_ = params;
    }

    /**
     * @brief Set individual PID gains
     */
    void setGains(double kp, double ki, double kd) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        params_.kp = kp;
        params_.ki = ki;
        params_.kd = kd;
    }

    /**
     * @brief Set output limits
     */
    void setOutputLimits(double min, double max) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        params_.outputMin = min;
        params_.outputMax = max;
    }

    /**
     * @brief Reset controller state
     */
    void resetController() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stats_.reset();
        firstUpdate_ = true;
        totalUpdates_.store(0);
        saturatedOutputs_.store(0);
    }

    /**
     * @brief Get current PID parameters
     */
    PIDParams getParameters() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return params_;
    }

    /**
     * @brief Get current PID statistics
     */
    PIDStats getStatistics() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return stats_;
    }

    /**
     * @brief Get performance metrics
     */
    uint64_t getTotalUpdates() const {
        return totalUpdates_.load();
    }

    uint64_t getSaturatedOutputs() const {
        return saturatedOutputs_.load();
    }

    /**
     * @brief ProcessingUnit interface implementations
     */
    void reset() override {
        resetController();
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "PIDController";
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
    }

private:
    /**
     * @brief Core PID computation algorithm
     */
    double computePID(double setpoint, double feedback, double dt) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        // Calculate error
        double error = setpoint - feedback;

        // Proportional term
        double proportional = params_.kp * error;

        // Integral term with anti-windup
        stats_.integral += error * dt;
        
        if (params_.enableAntiWindup) {
            // Clamp integral to prevent windup
            double maxIntegral = (params_.outputMax - proportional) / params_.ki;
            double minIntegral = (params_.outputMin - proportional) / params_.ki;
            
            if (params_.ki != 0.0) {
                stats_.integral = std::max(minIntegral, std::min(maxIntegral, stats_.integral));
            }
        }
        
        double integral = params_.ki * stats_.integral;

        // Derivative term (derivative on measurement to avoid derivative kick)
        stats_.derivative = (stats_.lastError - error) / dt;
        double derivative = params_.kd * stats_.derivative;

        // Calculate total output
        double output = proportional + integral + derivative;

        // Apply output limits
        bool saturated = false;
        if (output > params_.outputMax) {
            output = params_.outputMax;
            saturated = true;
        } else if (output < params_.outputMin) {
            output = params_.outputMin;
            saturated = true;
        }

        if (saturated) {
            saturatedOutputs_.fetch_add(1);
        }

        // Update state for next iteration
        stats_.lastError = error;

        return output;
    }
};

} // namespace axonvex::algorithms::control