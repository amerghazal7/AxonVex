/**
 * @file custom_system_example.cpp
 * @brief Example demonstrating the abstract AxonVexSystem with custom block layout
 * @author AxonVex Development Team
 */

#include <axonvex/axonvex.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace axonvex::core;

// =================================================================
// EXAMPLE PROCESSING UNITS
// =================================================================

/**
 * @brief Signal Generator ProcessingUnit
 */
class SignalGenerator : public ProcessingUnit {
private:
    std::atomic<uint64_t> sampleCount_{0};
    double frequency_{1.0};
    double amplitude_{1.0};
    OutputPort<double>* outputPort_;

public:
    explicit SignalGenerator(const std::string& name, double frequency = 1.0, double amplitude = 1.0)
        : ProcessingUnit(name), frequency_(frequency), amplitude_(amplitude) {
        outputPort_ = createOutputPort<double>(100, "signal_output");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "✓ Initialized " << getName() << " (freq: " << frequency_ << " Hz)" << std::endl;
    }

    void processSync() override {
        executionTimer_.start();
        setState(ExecutionState::RUNNING);

        auto count = sampleCount_.fetch_add(1);
        double time = static_cast<double>(count) * 0.001; // 1ms time steps
        double value = amplitude_ * std::sin(2.0 * M_PI * frequency_ * time);

        outputPort_->write(value);

        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }

    void processAsync() override {
        // For this example, async is same as sync
        processSync();
    }

    void reset() override {
        sampleCount_.store(0);
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "SignalGenerator";
    }

    uint64_t getSampleCount() const { return sampleCount_.load(); }
};

/**
 * @brief Signal Filter ProcessingUnit
 */
class SignalFilter : public ProcessingUnit {
private:
    InputPort<double>* inputPort_;
    OutputPort<double>* outputPort_;
    std::atomic<uint64_t> processedCount_{0};
    double filterCoeff_{0.5};

public:
    explicit SignalFilter(const std::string& name, double filterCoeff = 0.5)
        : ProcessingUnit(name), filterCoeff_(filterCoeff) {
        inputPort_ = createInputPort<double>(200, "signal_input");
        outputPort_ = createOutputPort<double>(201, "filtered_output");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "✓ Initialized " << getName() << " (coeff: " << filterCoeff_ << ")" << std::endl;
    }

    void processSync() override {
        executionTimer_.start();
        setState(ExecutionState::RUNNING);

        if (inputPort_->hasNewData()) {
            double input = inputPort_->read();
            double filtered = input * filterCoeff_;
            outputPort_->write(filtered);

            processedCount_.fetch_add(1);
            inputPort_->clearNewDataFlag();
        }

        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }

    void processAsync() override {
        processSync();
    }

    void reset() override {
        processedCount_.store(0);
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "SignalFilter";
    }

    uint64_t getProcessedCount() const { return processedCount_.load(); }
};

/**
 * @brief Data Logger ProcessingUnit
 */
class DataLogger : public ProcessingUnit {
private:
    InputPort<double>* inputPort_;
    std::atomic<uint64_t> loggedCount_{0};
    double lastValue_{0.0};

public:
    explicit DataLogger(const std::string& name) : ProcessingUnit(name) {
        inputPort_ = createInputPort<double>(300, "data_input");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        std::cout << "✓ Initialized " << getName() << std::endl;
    }

    void processSync() override {
        executionTimer_.start();
        setState(ExecutionState::RUNNING);

        if (inputPort_->hasNewData()) {
            lastValue_ = inputPort_->read();
            loggedCount_.fetch_add(1);

            if (loggedCount_.load() % 100 == 0) {
                std::cout << getName() << " logged " << loggedCount_.load() << " samples, last: " << lastValue_ << std::endl;
            }

            inputPort_->clearNewDataFlag();
        }

        executionTimer_.stop();
        updateSyncExecutionStats(std::chrono::duration_cast<std::chrono::microseconds>(executionTimer_.getElapsedNanoseconds()));
    }

    void processAsync() override {
        processSync();
    }

    void reset() override {
        loggedCount_.store(0);
        lastValue_ = 0.0;
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "DataLogger";
    }

    uint64_t getLoggedCount() const { return loggedCount_.load(); }
    double getLastValue() const { return lastValue_; }
};

// =================================================================
// CONCRETE SYSTEM IMPLEMENTATION
// =================================================================

/**
 * @brief Custom Signal Processing System
 *
 * This concrete implementation demonstrates a signal processing pipeline:
 * - SignalGenerator → SignalFilter → DataLogger
 * - System exposes generator output and filtered data as system ports
 */
class CustomSignalProcessingSystem : public AxonVexSystem {
private:
    std::unique_ptr<SignalGenerator> generator_;
    std::unique_ptr<SignalFilter> filter_;
    std::unique_ptr<DataLogger> logger_;

public:
    explicit CustomSignalProcessingSystem(const SystemConfiguration& config = SystemConfiguration{})
        : AxonVexSystem(config) {
    }

protected:
    /**
     * @brief Initialize the signal processing block layout
     */
    bool initializeBlocksLayout() override {
        std::cout << "\n=== Initializing Custom Signal Processing System Layout ===" << std::endl;

        try {
            // Create processing units
            generator_ = std::make_unique<SignalGenerator>("SignalGen", 2.0, 1.0);
            filter_ = std::make_unique<SignalFilter>("SignalFilter", 0.8);
            logger_ = std::make_unique<DataLogger>("DataLogger");

            // Register processing units with the system
            TimingConstraints generatorConstraints;
            generatorConstraints.period = std::chrono::milliseconds(10);  // 100 Hz
            generatorConstraints.deadline = std::chrono::milliseconds(5);
            generatorConstraints.priority = SchedulerPriority::HIGH;

            TimingConstraints filterConstraints;
            filterConstraints.period = std::chrono::milliseconds(20);     // 50 Hz
            filterConstraints.deadline = std::chrono::milliseconds(10);
            filterConstraints.priority = SchedulerPriority::NORMAL;

            TimingConstraints loggerConstraints;
            loggerConstraints.period = std::chrono::milliseconds(50);     // 20 Hz
            loggerConstraints.deadline = std::chrono::milliseconds(25);
            loggerConstraints.priority = SchedulerPriority::LOW;

            auto genId = registerProcessingUnit(std::move(generator_), generatorConstraints);
            auto filterId = registerProcessingUnit(std::move(filter_), filterConstraints);
            auto loggerId = registerProcessingUnit(std::move(logger_), loggerConstraints);

            // Get back raw pointers for connecting (the system now owns the unique_ptrs)
            auto* gen = getProcessingUnit(genId);
            auto* filt = getProcessingUnit(filterId);
            auto* log = getProcessingUnit(loggerId);

            if (!gen || !filt || !log) {
                std::cerr << "Failed to retrieve processing units after registration" << std::endl;
                return false;
            }

            // Connect the processing units together
            // Generator output (port 100) → Filter input (port 200)
            auto* genOutput = dynamic_cast<OutputPort<double>*>(gen->getOutputPorts().at(100));
            auto* filtInput = dynamic_cast<InputPort<double>*>(filt->getInputPorts().at(200));
            if (genOutput && filtInput) {
                genOutput->connect(filtInput);
            }

            // Filter output (port 201) → Logger input (port 300)
            auto* filtOutput = dynamic_cast<OutputPort<double>*>(filt->getOutputPorts().at(201));
            auto* logInput = dynamic_cast<InputPort<double>*>(log->getInputPorts().at(300));
            if (filtOutput && logInput) {
                filtOutput->connect(logInput);
            }

            // Assign system ports to expose internal functionality
            // System input: Allow external injection into filter
            assignSystemInputPort("filter_input", filt, 200);

            // System outputs: Expose raw signal and filtered signal
            assignSystemOutputPort("raw_signal", gen, 100);
            assignSystemOutputPort("filtered_signal", filt, 201);

            std::cout << "✓ Block layout initialized successfully" << std::endl;
            std::cout << "  - Processing pipeline: Generator → Filter → Logger" << std::endl;
            std::cout << "  - System ports: filter_input (in), raw_signal (out), filtered_signal (out)" << std::endl;

            return true;

        } catch (const std::exception& e) {
            std::cerr << "Block layout initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

public:
    /**
     * @brief Get processing statistics for monitoring
     */
    void printProcessingStats() const {
        std::cout << "\n=== Processing Statistics ===" << std::endl;

        auto units = getAllProcessingUnits();
        for (auto* unit : units) {
            auto stats = unit->getExecutionStats();
            std::cout << unit->getName() << ":" << std::endl;
            std::cout << "  Sync executions: " << stats.syncExecutionCount << std::endl;
            std::cout << "  Avg sync time: " << stats.avgSyncTime.count() << " μs" << std::endl;
            std::cout << "  Max sync time: " << stats.maxSyncTime.count() << " μs" << std::endl;
        }
    }
};

// =================================================================
// DEMONSTRATION FUNCTIONS
// =================================================================

void demonstrateCustomSystem() {
    std::cout << "\n🚀 Custom Signal Processing System Demo" << std::endl;

    // Create system configuration
    SystemConfiguration config;
    config.systemName = "CustomSignalProcessor";
    config.version = "1.0.0";
    config.logLevel = LogLevel::Info;
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;

    // Create and initialize the custom system
    CustomSignalProcessingSystem system(config);

    std::cout << "\n📋 Initializing system..." << std::endl;
    if (!system.initialize()) {
        std::cerr << "❌ System initialization failed!" << std::endl;
        return;
    }

    std::cout << "\n🏁 Starting system..." << std::endl;
    if (!system.start()) {
        std::cerr << "❌ System start failed!" << std::endl;
        return;
    }

    std::cout << "\n⏱️  Running for 3 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Print statistics
    system.printProcessingStats();

    std::cout << "\n🛑 Stopping system..." << std::endl;
    if (!system.stop()) {
        std::cerr << "❌ System stop failed!" << std::endl;
        return;
    }

    std::cout << "✅ Custom signal processing system demo completed successfully!" << std::endl;
}

// =================================================================
// MAIN FUNCTION
// =================================================================

int main() {
    try {
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "AxonVex Framework - Custom System Architecture Demo" << std::endl;
        std::cout << std::string(60, '=') << std::endl;

        demonstrateCustomSystem();

        std::cout << "\n🎉 All demonstrations completed successfully!" << std::endl;
        std::cout << "\nKey Benefits Demonstrated:" << std::endl;
        std::cout << "  ✅ Abstract system architecture with custom block layout" << std::endl;
        std::cout << "  ✅ Clean separation of system framework vs application logic" << std::endl;
        std::cout << "  ✅ Automatic processing unit lifecycle management" << std::endl;
        std::cout << "  ✅ Port connections and system port exposure" << std::endl;
        std::cout << "  ✅ Real-time scheduling with timing constraints" << std::endl;
        std::cout << "  ✅ Performance monitoring and statistics collection" << std::endl;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return 1;
    }
}
