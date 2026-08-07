/**
 * @file subsystem_example.cpp
 * @brief AxonVex Subsystem Composition and Cross-System Data Flow Example
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * This example demonstrates the revolutionary subsystem composition capability
 * of AxonVex, where multiple AxonVexSystems can be interconnected to create
 * complex, hierarchical processing architectures:
 *
 * - System-level input/output port assignment
 * - Cross-system data flow and connectivity
 * - Hierarchical system composition
 * - Subsystem interface definition
 * - Multi-level system orchestration
 *
 * Architecture Demonstrated:
 *   [Data Source System] → [Processing System] → [Analytics System]
 *        ↓                       ↓                      ↓
 *   Raw sensor data      →   Filtered data    →   Statistics & Insights
 */

#include <atomic>
#include <axonvex_core/axonvex.hpp>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <random>
#include <thread>
#include <vector>

using namespace axonvex;
using namespace axonvex::core;
#include "exampleLog.hpp"

using examplelog::Error;
using examplelog::Info;
using examplelog::Warn;

// =================================================================
// SPECIALIZED PROCESSING UNITS FOR EACH SUBSYSTEM
// =================================================================

/**
 * @brief Sensor Data Generator - simulates real sensor readings
 */
class SensorDataGenerator : public ProcessingUnit {
  private:
    OutputPort<double>* rawDataOut_;
    OutputPort<std::string>* statusOut_;
    std::atomic<uint64_t> sampleCount_{0};
    std::mt19937 generator_;
    std::normal_distribution<double> noiseDistribution_;
    double baseFrequency_;

  public:
    explicit SensorDataGenerator(const std::string& name, double frequency = 1.0)
        : ProcessingUnit(name), baseFrequency_(frequency),
          generator_(std::chrono::steady_clock::now().time_since_epoch().count()),
          noiseDistribution_(0.0, 0.1) {

        rawDataOut_ = createOutputPort<double>(2000, "raw_data");
        statusOut_ = createOutputPort<std::string>(2001, "sensor_status");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        Info() << "🌡️  " << getName() << " initialized - simulating sensor readings";
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        // Generate realistic sensor data with noise
        double time = sampleCount_.load() * 0.1;
        double signal = std::sin(baseFrequency_ * time) + noiseDistribution_(generator_);
        double sensorValue = 20.0 + 5.0 * signal; // Temperature-like readings (15-25°C range)

        rawDataOut_->write(sensorValue);

        // Periodic status updates
        if (sampleCount_.load() % 50 == 0) {
            statusOut_->write("SENSOR_ACTIVE_" + std::to_string(sampleCount_.load()));
        }

        sampleCount_.fetch_add(1);
    }

    void processAsync() override {
        processSync();
    }

    uint64_t getSampleCount() const {
        return sampleCount_.load();
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
        Info() << "🌡️  " << getName() << " finalized - generated " << sampleCount_.load()
               << " samples";
    }

    void reset() override {
        setState(ExecutionState::UNINITIALIZED);
        Info() << "🌡️  " << getName() << " reset - reset sensor readings";
    }

    std::string getTypeDescription() override {
        return "SensorDataGenerator";
    }
};

/**
 * @brief Digital Filter - processes raw sensor data
 */
class DigitalFilter : public ProcessingUnit {
  private:
    InputPort<double>* rawDataIn_;
    OutputPort<double>* filteredDataOut_;
    OutputPort<double>* filterMetricsOut_;

    std::vector<double> history_;
    size_t filterLength_;
    std::atomic<uint64_t> processedSamples_{0};

  public:
    explicit DigitalFilter(const std::string& name, size_t filterLength = 10)
        : ProcessingUnit(name), filterLength_(filterLength) {

        rawDataIn_ = createInputPort<double>(2100, "raw_input");
        filteredDataOut_ = createOutputPort<double>(2101, "filtered_output");
        filterMetricsOut_ = createOutputPort<double>(2102, "filter_metrics");

        history_.reserve(filterLength_);
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        history_.clear();
        Info() << "🔧 " << getName() << " initialized - " << filterLength_
               << "-point moving average filter";
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        if (rawDataIn_->hasNewData()) {
            double rawValue = rawDataIn_->read();

            // Add to history
            history_.push_back(rawValue);
            if (history_.size() > filterLength_) {
                history_.erase(history_.begin());
            }

            // Compute moving average
            double sum = 0.0;
            for (double val : history_) {
                sum += val;
            }
            double filtered = sum / history_.size();

            filteredDataOut_->write(filtered);

            // Calculate filter effectiveness (noise reduction)
            double variance = 0.0;
            double mean = filtered;
            for (double val : history_) {
                variance += (val - mean) * (val - mean);
            }
            variance /= history_.size();
            double noiseReduction = std::max(0.0, 1.0 - std::sqrt(variance));

            filterMetricsOut_->write(noiseReduction);

            processedSamples_.fetch_add(1);
            rawDataIn_->clearNewDataFlag();
        }
    }

    void processAsync() override {
        processSync();
    }

    uint64_t getProcessedSamples() const {
        return processedSamples_.load();
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
        Info() << "🔧 " << getName() << " finalized - processed " << processedSamples_.load()
               << " samples";
    }

    std::string getTypeDescription() override {
        return "DigitalFilter";
    }

    void reset() override {
        setState(ExecutionState::UNINITIALIZED);
        Info() << "🔧 " << getName() << " reset - reset filter";
    }
};

/**
 * @brief Statistical Analyzer - computes analytics on processed data
 */
class StatisticalAnalyzer : public ProcessingUnit {
  private:
    InputPort<double>* dataIn_;
    InputPort<double>* metricsIn_;
    OutputPort<std::string>* analyticsOut_;

    std::vector<double> dataHistory_;
    std::atomic<uint64_t> analysisCount_{0};
    double runningSum_{0.0};
    double runningSumSquared_{0.0};

  public:
    explicit StatisticalAnalyzer(const std::string& name) : ProcessingUnit(name) {

        dataIn_ = createInputPort<double>(2200, "data_input");
        metricsIn_ = createInputPort<double>(2201, "metrics_input");
        analyticsOut_ = createOutputPort<std::string>(2202, "analytics_output");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        dataHistory_.clear();
        runningSum_ = 0.0;
        runningSumSquared_ = 0.0;
        Info() << "📊 " << getName() << " initialized - statistical analysis engine";
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        bool hasNewData = false;
        double dataValue = 0.0;
        double metricsValue = 0.0;

        // Collect data from both inputs
        if (dataIn_->hasNewData()) {
            dataValue = dataIn_->read();
            dataHistory_.push_back(dataValue);
            runningSum_ += dataValue;
            runningSumSquared_ += dataValue * dataValue;
            hasNewData = true;
            dataIn_->clearNewDataFlag();
        }

        if (metricsIn_->hasNewData()) {
            metricsValue = metricsIn_->read();
            metricsIn_->clearNewDataFlag();
        }

        // Generate periodic analytics reports
        if (hasNewData && dataHistory_.size() % 25 == 0) {
            generateAnalyticsReport(metricsValue);
        }
    }

    void processAsync() override {
        processSync();
    }

    void generateAnalyticsReport(double currentMetrics) {
        if (dataHistory_.empty())
            return;

        size_t n = dataHistory_.size();
        double mean = runningSum_ / n;
        double variance = (runningSumSquared_ / n) - (mean * mean);
        double stdDev = std::sqrt(std::max(0.0, variance));

        // Find min/max
        auto minmax = std::minmax_element(dataHistory_.begin(), dataHistory_.end());

        // Create analytics report
        std::ostringstream report;
        report << std::fixed << std::setprecision(2);
        report << "ANALYTICS_REPORT|Samples:" << n;
        report << "|Mean:" << mean;
        report << "|StdDev:" << stdDev;
        report << "|Min:" << *minmax.first;
        report << "|Max:" << *minmax.second;
        report << "|FilterQuality:" << currentMetrics;
        report << "|Timestamp:"
               << std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();

        analyticsOut_->write(report.str());
        analysisCount_.fetch_add(1);

        // Keep history manageable
        if (dataHistory_.size() > 500) {
            dataHistory_.erase(dataHistory_.begin(), dataHistory_.begin() + 100);
            // Recalculate running sums for remaining data
            runningSum_ = 0.0;
            runningSumSquared_ = 0.0;
            for (double val : dataHistory_) {
                runningSum_ += val;
                runningSumSquared_ += val * val;
            }
        }
    }

    uint64_t getAnalysisCount() const {
        return analysisCount_.load();
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
        Info() << "📊 " << getName() << " finalized - generated " << analysisCount_.load()
               << " analytics reports";
    }

    void reset() override {
        setState(ExecutionState::UNINITIALIZED);
        Info() << "📊 " << getName() << " reset - reset analytics reports";
    }

    std::string getTypeDescription() override {
        return "StatisticalAnalyzer";
    }
};

/**
 * @brief Results Monitor - displays final system outputs
 */
class ResultsMonitor : public ProcessingUnit {
  private:
    InputPort<std::string>* analyticsIn_;
    InputPort<std::string>* statusIn_;
    std::atomic<uint64_t> reportsProcessed_{0};

  public:
    explicit ResultsMonitor(const std::string& name) : ProcessingUnit(name) {
        analyticsIn_ = createInputPort<std::string>(2300, "analytics_input");
        statusIn_ = createInputPort<std::string>(2301, "status_input");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
        Info() << "📺 " << getName() << " initialized - monitoring system outputs";
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        if (analyticsIn_->hasNewData()) {
            std::string report = analyticsIn_->read();

            // Parse and display the analytics report
            Info() << "📈 Analytics Report: " << report;
            reportsProcessed_.fetch_add(1);
            analyticsIn_->clearNewDataFlag();
        }

        if (statusIn_->hasNewData()) {
            std::string status = statusIn_->read();
            Info() << "🔧 System Status: " << status;
            statusIn_->clearNewDataFlag();
        }
    }

    void processAsync() override {
        processSync();
    }

    uint64_t getReportsProcessed() const {
        return reportsProcessed_.load();
    }

    void finalize() override {
        setState(ExecutionState::STOPPED);
        Info() << "📺 " << getName() << " finalized - processed " << reportsProcessed_.load()
               << " reports";
    }

    std::string getTypeDescription() override {
        return "ResultsMonitor";
    }

    void reset() override {
        setState(ExecutionState::UNINITIALIZED);
        Info() << "📺 " << getName() << " reset - reset reports";
    }
};

// =================================================================
// SUBSYSTEM CREATION AND MANAGEMENT
// =================================================================

// creating a mock axonvex system
class MockAxonVexSystem : public AxonVexSystem {
  public:
    MockAxonVexSystem(const SystemConfiguration& config = SystemConfiguration{})
        : AxonVexSystem(config) {}

    bool initializeBlocksLayout() override {
        return true;
    }
};

/**
 * @brief Create Data Source Subsystem
 */
std::unique_ptr<AxonVexSystem> createDataSourceSystem() {
    SystemConfiguration config;
    config.systemName = "DataSource-Subsystem";
    config.version = "1.0.0";
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;

    auto system = std::make_unique<MockAxonVexSystem>(config);

    if (!system->initialize()) {
        Error() << "Failed to initialize Data Source System";
        return nullptr;
    }

    // Create sensor generators
    auto tempSensor = std::make_unique<SensorDataGenerator>("TempSensor", 0.5);
    auto pressureSensor = std::make_unique<SensorDataGenerator>("PressureSensor", 0.3);

    SensorDataGenerator* tempPtr = tempSensor.get();
    SensorDataGenerator* pressurePtr = pressureSensor.get();

    // Register with timing constraints
    TimingConstraints sensorConstraints;
    sensorConstraints.period = std::chrono::milliseconds(50); // 20 Hz
    sensorConstraints.priority = SchedulerPriority::HIGH;

    system->registerProcessingUnit(std::move(tempSensor), sensorConstraints);
    system->registerProcessingUnit(std::move(pressureSensor), sensorConstraints);

    // Assign system ports - expose sensor outputs as system outputs
    system->assignSystemOutputPort("temperature_data", tempPtr, 2000);
    system->assignSystemOutputPort("pressure_data", pressurePtr, 2000);
    system->assignSystemOutputPort("system_status", tempPtr, 2001); // Use temp sensor for status

    Info() << "✅ Created Data Source Subsystem with " << system->getProcessingUnitCount()
           << " sensors";
    Info() << "   System Output Ports: " << system->getSystemOutputPortNames().size();

    return system;
}

/**
 * @brief Create Processing Subsystem
 */
std::unique_ptr<AxonVexSystem> createProcessingSystem() {
    SystemConfiguration config;
    config.systemName = "Processing-Subsystem";
    config.version = "1.0.0";
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;

    auto system = std::make_unique<MockAxonVexSystem>(config);

    if (!system->initialize()) {
        Error() << "Failed to initialize Processing System";
        return nullptr;
    }

    // Create filters
    auto tempFilter = std::make_unique<DigitalFilter>("TempFilter", 8);
    auto pressureFilter = std::make_unique<DigitalFilter>("PressureFilter", 12);

    DigitalFilter* tempFilterPtr = tempFilter.get();
    DigitalFilter* pressureFilterPtr = pressureFilter.get();

    // Register with timing constraints
    TimingConstraints filterConstraints;
    filterConstraints.period = std::chrono::milliseconds(30); // ~33 Hz
    filterConstraints.priority = SchedulerPriority::NORMAL;

    system->registerProcessingUnit(std::move(tempFilter), filterConstraints);
    system->registerProcessingUnit(std::move(pressureFilter), filterConstraints);

    // Assign system ports - inputs from data source, outputs to analytics
    system->assignSystemInputPort("temp_raw_input", tempFilterPtr, 2100);
    system->assignSystemInputPort("pressure_raw_input", pressureFilterPtr, 2100);
    system->assignSystemOutputPort("temp_filtered_output", tempFilterPtr, 2101);
    system->assignSystemOutputPort("pressure_filtered_output", pressureFilterPtr, 2101);
    system->assignSystemOutputPort("temp_metrics", tempFilterPtr, 2102);
    system->assignSystemOutputPort("pressure_metrics", pressureFilterPtr, 2102);

    Info() << "✅ Created Processing Subsystem with " << system->getProcessingUnitCount()
           << " filters";
    Info() << "   System Input Ports: " << system->getSystemInputPortNames().size();
    Info() << "   System Output Ports: " << system->getSystemOutputPortNames().size();

    return system;
}

/**
 * @brief Create Analytics Subsystem
 */
std::unique_ptr<AxonVexSystem> createAnalyticsSystem() {
    SystemConfiguration config;
    config.systemName = "Analytics-Subsystem";
    config.version = "1.0.0";
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;

    auto system = std::make_unique<MockAxonVexSystem>(config);

    if (!system->initialize()) {
        Error() << "Failed to initialize Analytics System";
        return nullptr;
    }

    // Create analyzers
    auto tempAnalyzer = std::make_unique<StatisticalAnalyzer>("TempAnalyzer");
    auto pressureAnalyzer = std::make_unique<StatisticalAnalyzer>("PressureAnalyzer");
    auto resultsMonitor = std::make_unique<ResultsMonitor>("ResultsMonitor");

    StatisticalAnalyzer* tempAnalyzerPtr = tempAnalyzer.get();
    StatisticalAnalyzer* pressureAnalyzerPtr = pressureAnalyzer.get();
    ResultsMonitor* monitorPtr = resultsMonitor.get();

    // Register with timing constraints
    TimingConstraints analyticsConstraints;
    analyticsConstraints.period = std::chrono::milliseconds(100); // 10 Hz
    analyticsConstraints.priority = SchedulerPriority::LOW;

    system->registerProcessingUnit(std::move(tempAnalyzer), analyticsConstraints);
    system->registerProcessingUnit(std::move(pressureAnalyzer), analyticsConstraints);
    system->registerProcessingUnit(std::move(resultsMonitor), analyticsConstraints);

    // Connect analyzers to monitor internally
    auto tempAnalyticsOut = tempAnalyzerPtr->getOutputPort<std::string>(2202);
    auto pressureAnalyticsOut = pressureAnalyzerPtr->getOutputPort<std::string>(2202);
    auto monitorAnalyticsIn = monitorPtr->getInputPort<std::string>(2300);

    if (tempAnalyticsOut && monitorAnalyticsIn) {
        static_cast<OutputPort<std::string>*>(tempAnalyticsOut)
            ->connect(static_cast<InputPort<std::string>*>(monitorAnalyticsIn));
    }

    // Assign system ports
    system->assignSystemInputPort("temp_data_input", tempAnalyzerPtr, 2200);
    system->assignSystemInputPort("pressure_data_input", pressureAnalyzerPtr, 2200);
    system->assignSystemInputPort("temp_metrics_input", tempAnalyzerPtr, 2201);
    system->assignSystemInputPort("pressure_metrics_input", pressureAnalyzerPtr, 2201);
    system->assignSystemInputPort("status_input", monitorPtr, 2301);

    Info() << "✅ Created Analytics Subsystem with " << system->getProcessingUnitCount()
           << " analyzers";
    Info() << "   System Input Ports: " << system->getSystemInputPortNames().size();

    return system;
}

// =================================================================
// MAIN DEMONSTRATION
// =================================================================

int main() {
    Info() << "🚀 AxonVex Subsystem Composition - Advanced Architecture Demonstration";
    Info() << "   Showcasing hierarchical system design and cross-system data flow";

    try {
        // Create all subsystems
        Info() << "\n🏗️  Creating Subsystems...";
        auto dataSourceSystem = createDataSourceSystem();
        auto processingSystem = createProcessingSystem();
        auto analyticsSystem = createAnalyticsSystem();

        if (!dataSourceSystem || !processingSystem || !analyticsSystem) {
            Error() << "Failed to create one or more subsystems";
            return 1;
        }

        // Display subsystem port information
        Info() << "\n📋 Subsystem Port Information:";
        Info() << dataSourceSystem->getSystemPortInfo();
        Info() << processingSystem->getSystemPortInfo();
        Info() << analyticsSystem->getSystemPortInfo();

        // Connect systems together - create the data flow pipeline
        Info() << "\n🔗 Connecting Subsystems...";

        // DataSource → Processing connections
        bool conn1 = dataSourceSystem->connectToSystem<double>(
            "temperature_data", processingSystem.get(), "temp_raw_input");
        bool conn2 = dataSourceSystem->connectToSystem<double>(
            "pressure_data", processingSystem.get(), "pressure_raw_input");

        // Processing → Analytics connections
        bool conn3 = processingSystem->connectToSystem<double>(
            "temp_filtered_output", analyticsSystem.get(), "temp_data_input");
        bool conn4 = processingSystem->connectToSystem<double>(
            "pressure_filtered_output", analyticsSystem.get(), "pressure_data_input");
        bool conn5 = processingSystem->connectToSystem<double>(
            "temp_metrics", analyticsSystem.get(), "temp_metrics_input");
        bool conn6 = processingSystem->connectToSystem<double>(
            "pressure_metrics", analyticsSystem.get(), "pressure_metrics_input");

        // Status data flow
        bool conn7 = dataSourceSystem->connectToSystem<std::string>(
            "system_status", analyticsSystem.get(), "status_input");

        // Verify all connections
        std::vector<bool> connections = {conn1, conn2, conn3, conn4, conn5, conn6, conn7};
        int successfulConnections = std::count(connections.begin(), connections.end(), true);

        Info() << "   ✅ Established " << successfulConnections << "/" << connections.size()
               << " cross-system connections";

        if (successfulConnections < static_cast<int>(connections.size())) {
            Warn() << "   ⚠️  Some connections failed - system may not work optimally";
        }

        // Start all subsystems
        Info() << "\n▶️  Starting Subsystem Pipeline...";

        if (!dataSourceSystem->start()) {
            Error() << "Failed to start Data Source System";
            return 1;
        }
        Info() << "   🌡️  Data Source System: RUNNING";

        if (!processingSystem->start()) {
            Error() << "Failed to start Processing System";
            return 1;
        }
        Info() << "   🔧 Processing System: RUNNING";

        if (!analyticsSystem->start()) {
            Error() << "Failed to start Analytics System";
            return 1;
        }
        Info() << "   📊 Analytics System: RUNNING";

        // Let the pipeline run and process data
        Info() << "\n⏱️  Running integrated subsystem pipeline for 10 seconds...";
        Info() << "   📈 Watch for analytics reports and system statistics!";
        std::this_thread::sleep_for(std::chrono::seconds(10));

        // Display final statistics
        Info() << "\n📊 Final Subsystem Statistics:";

        const auto& dataStats = dataSourceSystem->getStatistics();
        Info() << "🌡️  Data Source System:";
        Info() << "     Total Executions: " << dataStats.totalExecutions.load();
        Info() << "     Success Rate: " << std::fixed << std::setprecision(1)
               << (dataStats.getSuccessRate() * 100.0) << "%";

        const auto& procStats = processingSystem->getStatistics();
        Info() << "🔧 Processing System:";
        Info() << "     Total Executions: " << procStats.totalExecutions.load();
        Info() << "     Success Rate: " << std::fixed << std::setprecision(1)
               << (procStats.getSuccessRate() * 100.0) << "%";

        const auto& analyticsStats = analyticsSystem->getStatistics();
        Info() << "📊 Analytics System:";
        Info() << "     Total Executions: " << analyticsStats.totalExecutions.load();
        Info() << "     Success Rate: " << std::fixed << std::setprecision(1)
               << (analyticsStats.getSuccessRate() * 100.0) << "%";

        // Calculate overall system performance
        uint64_t totalSystemExecutions = dataStats.totalExecutions.load() +
                                         procStats.totalExecutions.load() +
                                         analyticsStats.totalExecutions.load();
        uint64_t totalSuccessfulExecutions = dataStats.successfulExecutions.load() +
                                             procStats.successfulExecutions.load() +
                                             analyticsStats.successfulExecutions.load();

        double overallSuccessRate = static_cast<double>(totalSuccessfulExecutions) /
                                    static_cast<double>(totalSystemExecutions);

        Info() << "🏆 Overall Pipeline Performance:";
        Info() << "     Combined Executions: " << totalSystemExecutions;
        Info() << "     Combined Success Rate: " << std::fixed << std::setprecision(1)
               << (overallSuccessRate * 100.0) << "%";

        // Graceful shutdown
        Info() << "\n⏹️  Shutting down subsystems...";
        analyticsSystem->stop();
        processingSystem->stop();
        dataSourceSystem->stop();

        Info() << "\n✨ Subsystem Composition Demonstration Complete!";
        Info() << "🎯 Key Architectural Features Demonstrated:";
        Info() << "   ✅ Hierarchical system composition";
        Info() << "   ✅ System-level port assignment and exposure";
        Info() << "   ✅ Cross-system type-safe data connections";
        Info() << "   ✅ Multi-level real-time scheduling";
        Info() << "   ✅ Distributed performance monitoring";
        Info() << "   ✅ Modular subsystem interfaces";
        Info() << "   ✅ Scalable system-of-systems architecture";

        Info() << "\n🏆 AxonVex: Enabling Enterprise-Grade Modular System Architecture!";

    } catch (const std::exception& e) {
        Error() << "Subsystem demonstration failed: " << e.what();
        return 1;
    }

    return 0;
}
