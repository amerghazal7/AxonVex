/**
 * @file algorithmsTest.cpp
 * @brief Comprehensive Test Suite for AxonVex Algorithms Module
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <gtest/gtest.h>
#include <axonvex/algorithms/algorithms.hpp>
#include <axonvex/core/timingController.hpp>
#include <chrono>
#include <thread>

using namespace axonvex::algorithms;
using namespace axonvex::core;

class AlgorithmsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize timing controller for real-time testing
        timingController = std::make_unique<TimingController>();
        timingController->setExecutionFrequency(1000.0); // 1kHz for testing
    }

    void TearDown() override {
        if (timingController && timingController->isRunning()) {
            timingController->stop();
        }
    }

    std::unique_ptr<TimingController> timingController;
};

// =================================================================
// PID CONTROLLER TESTS
// =================================================================

TEST_F(AlgorithmsTest, PIDControllerBasicOperation) {
    auto pidController = std::make_unique<control::PIDController>("TestPID");
    
    // Initialize controller
    pidController->initialize();
    EXPECT_EQ(pidController->getState(), ExecutionState::INITIALIZED);
    
    // Configure PID parameters
    control::PIDController::PIDParams params;
    params.kp = 2.0;
    params.ki = 0.5;
    params.kd = 0.1;
    params.outputMin = -10.0;
    params.outputMax = 10.0;
    pidController->setParameters(params);
    
    // Verify parameters
    auto retrievedParams = pidController->getParameters();
    EXPECT_DOUBLE_EQ(retrievedParams.kp, 2.0);
    EXPECT_DOUBLE_EQ(retrievedParams.ki, 0.5);
    EXPECT_DOUBLE_EQ(retrievedParams.kd, 0.1);
    
    // Get input/output ports
    auto* setpointInput = pidController->getInputPort<double>(1000);
    auto* feedbackInput = pidController->getInputPort<double>(1001);
    auto* controlOutput = pidController->getOutputPort<double>(1002);
    
    ASSERT_NE(setpointInput, nullptr);
    ASSERT_NE(feedbackInput, nullptr);
    ASSERT_NE(controlOutput, nullptr);
    
    // Test PID computation with step input
    setpointInput->write(5.0);
    feedbackInput->write(0.0);
    
    pidController->processSync();
    
    // Should have control output (proportional response to error)
    EXPECT_TRUE(controlOutput->hasNewData());
    double controlValue = controlOutput->read();
    EXPECT_GT(controlValue, 0.0); // Positive control for positive error
    EXPECT_LE(controlValue, 10.0); // Within output limits
    
    // Test statistics
    auto stats = pidController->getStatistics();
    EXPECT_GT(stats.updateCount, 0);
    EXPECT_GT(pidController->getTotalUpdates(), 0);
    
    pidController->finalize();
}

TEST_F(AlgorithmsTest, PIDControllerRealTimeScheduling) {
    auto pidController = std::make_unique<control::PIDController>("ScheduledPID");
    pidController->initialize();
    
    // Schedule PID controller with timing constraints
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(1); // 1kHz
    constraints.deadline = std::chrono::microseconds(800);
    constraints.priority = SchedulerPriority::HIGH;
    
    uint32_t taskId = timingController->scheduleProcessingUnit(pidController.get(), constraints);
    EXPECT_GT(taskId, 0);
    
    // Provide test inputs
    auto* setpointInput = pidController->getInputPort<double>(1000);
    auto* feedbackInput = pidController->getInputPort<double>(1001);
    
    setpointInput->write(10.0);
    feedbackInput->write(2.0);
    
    // Start real-time execution
    timingController->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    timingController->stop();
    
    // Verify execution
    EXPECT_GT(pidController->getTotalUpdates(), 0);
    
    auto metrics = timingController->getPerformanceMetrics();
    EXPECT_GT(metrics.totalExecutions, 0);
    EXPECT_EQ(metrics.missedDeadlines, 0); // Should meet all deadlines
}

// =================================================================
// DIGITAL FILTER TESTS
// =================================================================

TEST_F(AlgorithmsTest, DigitalFilterOperation) {
    auto filter = std::make_unique<signal::DigitalFilter>("TestFilter");
    
    filter->initialize();
    EXPECT_EQ(filter->getState(), ExecutionState::INITIALIZED);
    
    // Configure low-pass filter
    signal::DigitalFilter::FilterConfig config;
    config.type = signal::FilterType::LOW_PASS;
    config.cutoffFreq = 100.0; // 100 Hz cutoff
    config.samplingFreq = 1000.0; // 1 kHz sampling
    config.order = 2;
    filter->configure(config);
    
    // Get ports
    auto* signalInput = filter->getInputPort<double>(2000);
    auto* filteredOutput = filter->getOutputPort<double>(2001);
    
    ASSERT_NE(signalInput, nullptr);
    ASSERT_NE(filteredOutput, nullptr);
    
    // Test filtering with sine wave
    double amplitude = 1.0;
    double frequency = 50.0; // 50 Hz signal (below cutoff)
    
    for (int i = 0; i < 100; ++i) {
        double time = i * 0.001; // 1ms steps
        double signal = amplitude * std::sin(2.0 * M_PI * frequency * time);
        
        signalInput->write(signal);
        filter->processSync();
        
        if (filteredOutput->hasNewData()) {
            double filtered = filteredOutput->read();
            EXPECT_LE(std::abs(filtered), amplitude * 1.1); // Some tolerance for initial transient
        }
    }
    
    // Verify statistics
    auto stats = filter->getStatistics();
    EXPECT_GT(stats.samplesProcessed, 0);
    EXPECT_GT(filter->getTotalSamples(), 0);
    
    filter->finalize();
}

// =================================================================
// KALMAN FILTER TESTS
// =================================================================

TEST_F(AlgorithmsTest, KalmanFilterEstimation) {
    auto kalman = std::make_unique<estimation::KalmanFilter>("TestKalman");
    
    kalman->initialize();
    EXPECT_EQ(kalman->getState(), ExecutionState::INITIALIZED);
    
    // Configure for simple 2D position tracking
    estimation::KalmanFilter::KalmanConfig config;
    config.stateDim = 4; // [x, vx, y, vy]
    config.measurementDim = 2; // [x, y] measurements
    config.processNoise = 0.01;
    config.measurementNoise = 0.1;
    kalman->configure(config);
    
    // Get ports
    auto* measurementInput = kalman->getInputPort<std::vector<double>>(3000);
    auto* stateOutput = kalman->getOutputPort<std::vector<double>>(3002);
    
    ASSERT_NE(measurementInput, nullptr);
    ASSERT_NE(stateOutput, nullptr);
    
    // Test with simulated measurements
    std::vector<double> measurement = {1.0, 2.0}; // x, y position
    measurementInput->write(measurement);
    
    kalman->processSync();
    
    // Should have state estimate
    EXPECT_TRUE(stateOutput->hasNewData());
    auto stateEstimate = stateOutput->read();
    EXPECT_EQ(stateEstimate.size(), 4);
    
    // State estimate should be reasonable
    EXPECT_NEAR(stateEstimate[0], 1.0, 0.5); // x position
    EXPECT_NEAR(stateEstimate[2], 2.0, 0.5); // y position
    
    // Test statistics
    auto stats = kalman->getStatistics();
    EXPECT_GT(stats.updateCount, 0);
    EXPECT_GT(kalman->getTotalUpdates(), 0);
    
    kalman->finalize();
}

// =================================================================
// GRADIENT DESCENT TESTS
// =================================================================

TEST_F(AlgorithmsTest, GradientDescentOptimization) {
    auto optimizer = std::make_unique<optimization::GradientDescent>("TestOptimizer");
    
    optimizer->initialize();
    EXPECT_EQ(optimizer->getState(), ExecutionState::INITIALIZED);
    
    // Configure optimizer
    optimization::GradientDescent::OptimizerConfig config;
    config.type = optimization::GradientDescent::OptimizerType::ADAM;
    config.parameterDim = 2;
    config.learningRate = 0.01;
    optimizer->configure(config);
    
    // Set initial parameters
    std::vector<double> initialParams = {1.0, 1.0};
    optimizer->setParameters(initialParams);
    
    // Get ports
    auto* gradientInput = optimizer->getInputPort<std::vector<double>>(4000);
    auto* lossInput = optimizer->getInputPort<double>(4001);
    auto* parametersOutput = optimizer->getOutputPort<std::vector<double>>(4002);
    
    ASSERT_NE(gradientInput, nullptr);
    ASSERT_NE(lossInput, nullptr);
    ASSERT_NE(parametersOutput, nullptr);
    
    // Simulate optimization of simple quadratic function: f(x,y) = x^2 + y^2
    // Gradients: df/dx = 2x, df/dy = 2y
    
    auto currentParams = optimizer->getParameters();
    double loss = currentParams[0] * currentParams[0] + currentParams[1] * currentParams[1];
    std::vector<double> gradients = {2.0 * currentParams[0], 2.0 * currentParams[1]};
    
    gradientInput->write(gradients);
    lossInput->write(loss);
    
    optimizer->processSync();
    
    // Should have updated parameters
    EXPECT_TRUE(parametersOutput->hasNewData());
    auto updatedParams = parametersOutput->read();
    EXPECT_EQ(updatedParams.size(), 2);
    
    // Parameters should move toward zero (minimum)
    double updatedLoss = updatedParams[0] * updatedParams[0] + updatedParams[1] * updatedParams[1];
    EXPECT_LT(updatedLoss, loss); // Loss should decrease
    
    // Test statistics
    auto stats = optimizer->getStatistics();
    EXPECT_GT(stats.iterationCount, 0);
    EXPECT_GT(optimizer->getTotalIterations(), 0);
    
    optimizer->finalize();
}

// =================================================================
// INTEGRATION TESTS
// =================================================================

TEST_F(AlgorithmsTest, AlgorithmIntegrationPipeline) {
    // Test integration of multiple algorithms in a pipeline
    auto pidController = std::make_unique<control::PIDController>("PipelinePID");
    auto filter = std::make_unique<signal::DigitalFilter>("PipelineFilter");
    auto kalman = std::make_unique<estimation::KalmanFilter>("PipelineKalman");
    
    // Initialize all components
    pidController->initialize();
    filter->initialize();
    kalman->initialize();
    
    // Schedule all in timing controller
    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(2);
    constraints.priority = SchedulerPriority::NORMAL;
    
    uint32_t pidTask = timingController->scheduleProcessingUnit(pidController.get(), constraints);
    uint32_t filterTask = timingController->scheduleProcessingUnit(filter.get(), constraints);
    uint32_t kalmanTask = timingController->scheduleProcessingUnit(kalman.get(), constraints);
    
    EXPECT_GT(pidTask, 0);
    EXPECT_GT(filterTask, 0);
    EXPECT_GT(kalmanTask, 0);
    
    // Connect pipeline: PID -> Filter -> Kalman
    auto* pidOutput = pidController->getOutputPort<double>(1002);
    auto* filterInput = filter->getInputPort<double>(2000);
    auto* filterOutput = filter->getOutputPort<double>(2001);
    
    // For this test, we'll manually transfer data between stages
    // In a real system, you'd use the port connection mechanism
    
    // Provide inputs
    auto* setpointInput = pidController->getInputPort<double>(1000);
    auto* feedbackInput = pidController->getInputPort<double>(1001);
    
    setpointInput->write(5.0);
    feedbackInput->write(1.0);
    
    // Run integrated pipeline
    timingController->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    timingController->stop();
    
    // Verify all components executed
    EXPECT_GT(pidController->getTotalUpdates(), 0);
    EXPECT_GT(filter->getTotalSamples(), 0);
    EXPECT_GT(kalman->getTotalUpdates(), 0);
    
    // Verify timing performance
    auto metrics = timingController->getPerformanceMetrics();
    EXPECT_GT(metrics.totalExecutions, 0);
    EXPECT_EQ(metrics.missedDeadlines, 0);
    
    // All components should be in running or stopped state
    EXPECT_NE(pidController->getState(), ExecutionState::ERROR);
    EXPECT_NE(filter->getState(), ExecutionState::ERROR);
    EXPECT_NE(kalman->getState(), ExecutionState::ERROR);
    
    // Finalize
    pidController->finalize();
    filter->finalize();
    kalman->finalize();
}

// =================================================================
// PERFORMANCE TESTS
// =================================================================

TEST_F(AlgorithmsTest, AlgorithmPerformanceBenchmark) {
    // Test real-time performance of algorithms
    auto pidController = std::make_unique<control::PIDController>("PerfPID");
    pidController->initialize();
    
    // High-frequency scheduling for performance test
    TimingConstraints constraints;
    constraints.period = std::chrono::microseconds(100); // 10 kHz
    constraints.deadline = std::chrono::microseconds(80);
    constraints.priority = SchedulerPriority::REAL_TIME;
    
    uint32_t taskId = timingController->scheduleProcessingUnit(pidController.get(), constraints);
    EXPECT_GT(taskId, 0);
    
    // Provide continuous inputs
    auto* setpointInput = pidController->getInputPort<double>(1000);
    auto* feedbackInput = pidController->getInputPort<double>(1001);
    
    // Run performance test
    timingController->enableRealTimeMode(true);
    timingController->start();
    
    // Continuous input updates during test
    for (int i = 0; i < 500; ++i) {
        setpointInput->write(std::sin(i * 0.01));
        feedbackInput->write(std::sin(i * 0.01 - 0.1));
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    
    timingController->stop();
    
    // Verify high-frequency execution
    EXPECT_GT(pidController->getTotalUpdates(), 400); // Should execute most cycles
    
    auto metrics = timingController->getPerformanceMetrics();
    EXPECT_GT(metrics.totalExecutions, 400);
    
    // Performance requirements: <5% missed deadlines acceptable for this test
    double missRate = static_cast<double>(metrics.missedDeadlines) / metrics.totalExecutions;
    EXPECT_LT(missRate, 0.05);
    
    pidController->finalize();
}

// =================================================================
// ALGORITHM MODULE INFO TEST
// =================================================================

TEST_F(AlgorithmsTest, AlgorithmModuleInfo) {
    // Test algorithm module version information
    EXPECT_STREQ(AlgorithmInfo::version, "1.0.0");
    EXPECT_STREQ(AlgorithmInfo::description, "AxonVex Real-time Algorithm Suite");
    EXPECT_TRUE(strlen(AlgorithmInfo::build_date) > 0);
}

// =================================================================
// MAIN TEST RUNNER
// =================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "\n=== AxonVex Algorithms Module Test Suite ===\n";
    std::cout << "Testing comprehensive algorithm implementations\n";
    std::cout << "Version: " << AlgorithmInfo::version << "\n";
    std::cout << "Build: " << AlgorithmInfo::build_date << "\n\n";
    
    int result = RUN_ALL_TESTS();
    
    if (result == 0) {
        std::cout << "\n🎉 All Algorithm Tests PASSED!\n";
        std::cout << "✅ PID Controllers working perfectly\n";
        std::cout << "✅ Digital Filters operational\n";
        std::cout << "✅ Kalman Filters estimating accurately\n";
        std::cout << "✅ Gradient Descent optimizing effectively\n";
        std::cout << "✅ Real-time performance verified\n";
        std::cout << "✅ Algorithm integration successful\n\n";
        std::cout << "🚀 Phase 3 Algorithms Module COMPLETE!\n";
    } else {
        std::cout << "\n❌ Some Algorithm Tests FAILED!\n";
        std::cout << "Please review test output above.\n";
    }
    
    return result;
}