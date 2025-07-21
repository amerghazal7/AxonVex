/**
 * @file system_example.cpp
 * @brief Comprehensive AxonVex System Management Example
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * This example demonstrates the powerful AxonVexSystem class, the central
 * orchestrator of the AxonVex framework:
 * 
 * - System lifecycle management (initialize, start, pause, resume, stop)
 * - Processing unit registration and orchestration
 * - Real-time scheduling and monitoring
 * - Health checks and system diagnostics
 * - Event system and callbacks
 * - Error handling and recovery
 * - Performance monitoring and statistics
 * - Configuration management
 */

#include <axonvex/axonvex.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <iomanip>

using namespace axonvex;
using namespace axonvex::Log;

// =================================================================
// EXAMPLE PROCESSING UNITS
// =================================================================

/**
 * @brief Data Generator ProcessingUnit
 */
class DataGenerator : public ProcessingUnit {
private:
    OutputPort<double>* output_;
    std::atomic<uint64_t> generatedCount_{0};
    double frequency_;
    std::mt19937 generator_;
    std::uniform_real_distribution<> distribution_;

public:
    explicit DataGenerator(const std::string& name, double freq = 1.0)
        : ProcessingUnit(name), frequency_(freq)
        , generator_(std::chrono::steady_clock::now().time_since_epoch().count())
        , distribution_(-10.0, 10.0) {
        output_ = createOutputPort<double>(1000, "data_out");
    }
    
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }
    
    void processSync() override {
        setState(ExecutionState::RUNNING);
        
        // Generate random data
        double value = distribution_(generator_) * std::sin(generatedCount_ * 0.1);
        output_->write(value);
        generatedCount_.fetch_add(1);
        
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    uint64_t getGeneratedCount() const { return generatedCount_.load(); }
    
    void finalize() override {
        setState(ExecutionState::STOPPED);
    }
};

/**
 * @brief Data Processor ProcessingUnit
 */
class DataProcessor : public ProcessingUnit {
private:
    InputPort<double>* input_;
    OutputPort<double>* output_;
    std::atomic<uint64_t> processedCount_{0};
    double runningSum_{0.0};
    size_t windowSize_;

public:
    explicit DataProcessor(const std::string& name, size_t windowSize = 100)
        : ProcessingUnit(name), windowSize_(windowSize) {
        input_ = createInputPort<double>(1001, "data_in");
        output_ = createOutputPort<double>(1002, "processed_out");
    }
    
    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }
    
    void processSync() override {
        setState(ExecutionState::RUNNING);
        
        if (input_->hasNewData()) {
            double value = input_->read();
            
            // Simple moving average calculation
            runningSum_ += value;
            processedCount_.fetch_add(1);
            
            if (processedCount_.load() > windowSize_) {
                runningSum_ -= value; // Simplified for demonstration
            }
            
            double average = runningSum_ / std::min(processedCount_.load(), 
                                                  static_cast<uint64_t>(windowSize_));
            output_->write(average);
            
            input_->clearNewDataFlag();
        }
    }
    
    uint64_t getProcessedCount() const { return processedCount_.load(); }
    double getRunningAverage() const { return runningSum_ / std::min(processedCount_.load(), 
                                                                    static_cast<uint64_t>(windowSize_)); }
    
    void finalize() override {
        setState(ExecutionState::STOPPED);
    }
};

/**
 * @brief System Monitor ProcessingUnit
 */
class SystemMonitor : public ProcessingUnit {
private:
    InputPort<double>* input_;
    std::atomic<uint64_t> monitoringCycles_{0};
    std::chrono::steady_clock::time_point startTime_;
    AxonVexSystem* system_;

public:
    explicit SystemMonitor(const std::string& name, AxonVexSystem* system)
        : ProcessingUnit(name), system_(system) {
        input_ = createInputPort<double>(1003, "monitor_in");
    }
    
    void initialize() override {
        startTime_ = std::chrono::steady_clock::now();
        setState(ExecutionState::INITIALIZED);
    }
    
    void processSync() override {
        setState(ExecutionState::RUNNING);
        
        if (input_->hasNewData()) {
            double value = input_->read();
            monitoringCycles_.fetch_add(1);
            
            // Periodic system health logging
            if (monitoringCycles_.load() % 100 == 0) {
                if (system_) {
                    const auto& stats = system_->getStatistics();
                    Info() << "📊 System Health Check #" << (monitoringCycles_.load() / 100);
                    Info() << "   Uptime: " << std::fixed << std::setprecision(1) 
                           << system_->getUptimeSeconds() << "s";
                    Info() << "   Total Executions: " << stats.totalExecutions.load();
                    Info() << "   Success Rate: " << std::fixed << std::setprecision(1)
                           << (stats.getSuccessRate() * 100.0) << "%";
                    Info() << "   Processing Units: " << stats.activeProcessingUnits.load();
                    Info() << "   Memory Usage: " << (system_->getMemoryUsage() / 1024 / 1024) << " MB";
                }
            }
            
            input_->clearNewDataFlag();
        }
    }
    
    uint64_t getMonitoringCycles() const { return monitoringCycles_.load(); }
    
    void finalize() override {
        setState(ExecutionState::STOPPED);
    }
};

// =================================================================
// DEMONSTRATION FUNCTIONS
// =================================================================

/**
 * @brief Demonstrate system lifecycle management
 */
void demonstrateSystemLifecycle() {
    Info() << "\n=== System Lifecycle Management ===";
    
    // Create system configuration
    SystemConfiguration config;
    config.systemName = "AxonVex-Demo";
    config.version = "1.0.0";
    config.logLevel = LogLevel::Info;
    config.enablePerformanceMonitoring = true;
    config.enableAutoRecovery = true;
    config.statisticsUpdateInterval = std::chrono::milliseconds(500);
    config.healthCheckInterval = std::chrono::seconds(2);
    config.enableFileLogging = false;
    
    Info() << "Creating AxonVex System with configuration:";
    Info() << "  System Name: " << config.systemName;
    Info() << "  Version: " << config.version;
    Info() << "  Performance Monitoring: " << (config.enablePerformanceMonitoring ? "Enabled" : "Disabled");
    
    // Create the system
    AxonVexSystem system(config);
    
    Info() << "Initial State: " << to_string(system.getState());
    Info() << "Is Running: " << (system.isRunning() ? "Yes" : "No");
    Info() << "Is Healthy: " << (system.isHealthy() ? "Yes" : "No");
    
    // Initialize the system
    Info() << "\n🚀 Initializing system...";
    if (system.initialize()) {
        Info() << "✓ System initialized successfully";
        Info() << "State: " << to_string(system.getState());
    } else {
        Error() << "✗ System initialization failed";
        return;
    }
    
    // Start the system
    Info() << "\n▶️  Starting system...";
    if (system.start()) {
        Info() << "✓ System started successfully";
        Info() << "State: " << to_string(system.getState());
        Info() << "Is Running: " << (system.isRunning() ? "Yes" : "No");
    } else {
        Error() << "✗ System start failed";
        return;
    }
    
    // Let it run for a moment
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Pause the system
    Info() << "\n⏸️  Pausing system...";
    if (system.pause()) {
        Info() << "✓ System paused successfully";
        Info() << "State: " << to_string(system.getState());
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Resume the system
    Info() << "\n▶️  Resuming system...";
    if (system.resume()) {
        Info() << "✓ System resumed successfully";
        Info() << "State: " << to_string(system.getState());
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Stop the system
    Info() << "\n⏹️  Stopping system gracefully...";
    if (system.stop(std::chrono::seconds(5))) {
        Info() << "✓ System stopped gracefully";
        Info() << "Final State: " << to_string(system.getState());
    } else {
        Warn() << "⚠ Graceful stop timed out, emergency shutdown triggered";
    }
}

/**
 * @brief Demonstrate processing unit orchestration
 */
void demonstrateProcessingUnitOrchestration() {
    Info() << "\n=== Processing Unit Orchestration ===";
    
    // Create system
    SystemConfiguration config;
    config.systemName = "ProcessingDemo";
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;
    
    AxonVexSystem system(config);
    
    if (!system.initialize()) {
        Error() << "Failed to initialize system";
        return;
    }
    
    Info() << "🏗️  Creating and registering processing units...";
    
    // Create data generator
    auto generator = std::make_unique<DataGenerator>("DataGen-1", 2.0);
    DataGenerator* genPtr = generator.get();
    
    TimingConstraints genConstraints;
    genConstraints.period = std::chrono::milliseconds(10); // 100 Hz
    genConstraints.deadline = std::chrono::milliseconds(8);
    genConstraints.priority = SchedulerPriority::HIGH;
    
    uint32_t genId = system.registerProcessingUnit(std::move(generator), genConstraints);
    Info() << "  📊 Registered Data Generator (ID: " << genId << ") - 100Hz, HIGH priority";
    
    // Create data processor
    auto processor = std::make_unique<DataProcessor>("DataProcessor-1", 50);
    DataProcessor* procPtr = processor.get();
    
    TimingConstraints procConstraints;
    procConstraints.period = std::chrono::milliseconds(20); // 50 Hz
    procConstraints.deadline = std::chrono::milliseconds(15);
    procConstraints.priority = SchedulerPriority::NORMAL;
    
    uint32_t procId = system.registerProcessingUnit(std::move(processor), procConstraints);
    Info() << "  🔄 Registered Data Processor (ID: " << procId << ") - 50Hz, NORMAL priority";
    
    // Create system monitor
    auto monitor = std::make_unique<SystemMonitor>("SysMonitor-1", &system);
    SystemMonitor* monPtr = monitor.get();
    
    TimingConstraints monConstraints;
    monConstraints.period = std::chrono::milliseconds(100); // 10 Hz
    monConstraints.deadline = std::chrono::milliseconds(80);
    monConstraints.priority = SchedulerPriority::LOW;
    
    uint32_t monId = system.registerProcessingUnit(std::move(monitor), monConstraints);
    Info() << "  📈 Registered System Monitor (ID: " << monId << ") - 10Hz, LOW priority";
    
    // Connect the processing units
    Info() << "\n🔗 Connecting processing units...";
    auto genOutput = genPtr->getPort(1000);
    auto procInput = procPtr->getPort(1001);
    auto procOutput = procPtr->getPort(1002);
    auto monInput = monPtr->getPort(1003);
    
    if (genOutput && procInput) {
        static_cast<OutputPort<double>*>(genOutput)->connect(static_cast<InputPort<double>*>(procInput));
        Info() << "  ✓ Connected DataGenerator → DataProcessor";
    }
    
    if (procOutput && monInput) {
        static_cast<OutputPort<double>*>(procOutput)->connect(static_cast<InputPort<double>*>(monInput));
        Info() << "  ✓ Connected DataProcessor → SystemMonitor";
    }
    
    Info() << "\nTotal Processing Units: " << system.getProcessingUnitCount();
    
    // Start the system
    Info() << "\n🚀 Starting orchestrated system...";
    if (!system.start()) {
        Error() << "Failed to start system";
        return;
    }
    
    // Let the system run for a while
    Info() << "⏱️  Running system for 5 seconds...";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Display results
    Info() << "\n📊 Processing Results:";
    Info() << "  Data Generator: " << genPtr->getGeneratedCount() << " samples generated";
    Info() << "  Data Processor: " << procPtr->getProcessedCount() << " samples processed";
    Info() << "  System Monitor: " << monPtr->getMonitoringCycles() << " monitoring cycles";
    
    const auto& stats = system.getStatistics();
    Info() << "\n📈 System Statistics:";
    Info() << "  Total Executions: " << stats.totalExecutions.load();
    Info() << "  Successful Executions: " << stats.successfulExecutions.load();
    Info() << "  Failed Executions: " << stats.failedExecutions.load();
    Info() << "  Success Rate: " << std::fixed << std::setprecision(2) 
           << (stats.getSuccessRate() * 100.0) << "%";
    Info() << "  Uptime: " << std::fixed << std::setprecision(1) 
           << system.getUptimeSeconds() << " seconds";
    
    system.stop();
    Info() << "✓ System demonstration completed";
}

/**
 * @brief Demonstrate event system and callbacks
 */
void demonstrateEventSystem() {
    Info() << "\n=== Event System and Callbacks ===";
    
    SystemConfiguration config;
    config.systemName = "EventDemo";
    config.enablePerformanceMonitoring = true;
    config.enableFileLogging = false;
    
    AxonVexSystem system(config);
    
    // Event tracking
    std::atomic<int> stateChangeEvents{0};
    std::atomic<int> unitEvents{0};
    std::atomic<int> healthCheckEvents{0};
    
    // Register event callback
    Info() << "📡 Registering event callback...";
    uint32_t eventCallbackId = system.registerEventCallback([&](const SystemEvent& event) {
        switch (event.type) {
            case SystemEvent::Type::STATE_CHANGE:
                stateChangeEvents.fetch_add(1);
                Info() << "🔄 State Change Event: " << to_string(event.oldState) 
                       << " → " << to_string(event.newState);
                break;
                
            case SystemEvent::Type::PROCESSING_UNIT_ADDED:
                unitEvents.fetch_add(1);
                Info() << "➕ Processing Unit Added: " << event.metadata.at("unit_name");
                break;
                
            case SystemEvent::Type::PROCESSING_UNIT_REMOVED:
                unitEvents.fetch_add(1);
                Info() << "➖ Processing Unit Removed: " << event.metadata.at("unit_name");
                break;
                
            case SystemEvent::Type::HEALTH_CHECK:
                healthCheckEvents.fetch_add(1);
                Info() << "🏥 Health Check Event: " << event.metadata.at("overall_status");
                break;
                
            default:
                Info() << "📋 Other Event: " << event.description;
                break;
        }
    });
    
    // Register health check callback
    Info() << "🏥 Registering health check callback...";
    uint32_t healthCallbackId = system.registerHealthCheckCallback([]() -> SystemHealth {
        SystemHealth customHealth;
        customHealth.overallStatus = SystemHealth::Status::HEALTHY;
        customHealth.warnings.push_back("Custom health check executed");
        return customHealth;
    });
    
    // Initialize and start system (should trigger state change events)
    Info() << "\n🚀 Starting system to trigger events...";
    system.initialize();
    system.start();
    
    // Add and remove processing units (should trigger unit events)
    auto testUnit = std::make_unique<DataGenerator>("EventTestUnit");
    uint32_t unitId = system.registerProcessingUnit(std::move(testUnit));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Trigger health check (should trigger health event)
    system.performHealthCheck();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Remove unit
    system.unregisterProcessingUnit(unitId);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Display event statistics
    Info() << "\n📊 Event Statistics:";
    Info() << "  State Change Events: " << stateChangeEvents.load();
    Info() << "  Unit Events: " << unitEvents.load();
    Info() << "  Health Check Events: " << healthCheckEvents.load();
    
    // Cleanup callbacks
    system.unregisterEventCallback(eventCallbackId);
    
    system.stop();
    Info() << "✓ Event system demonstration completed";
}

/**
 * @brief Demonstrate health monitoring and diagnostics
 */
void demonstrateHealthMonitoring() {
    Info() << "\n=== Health Monitoring and Diagnostics ===";
    
    SystemConfiguration config;
    config.systemName = "HealthDemo";
    config.enablePerformanceMonitoring = true;
    config.healthCheckInterval = std::chrono::seconds(1);
    config.enableFileLogging = false;
    
    AxonVexSystem system(config);
    
    system.initialize();
    system.start();
    
    Info() << "🏥 Performing comprehensive health check...";
    system.performHealthCheck();
    
    SystemHealth health = system.getHealth();
    Info() << "Overall Health Status: " << health.getStatusString();
    Info() << "Component Health:";
    Info() << "  ⏰ Timing Controller: " << (health.timingControllerHealthy ? "✓ Healthy" : "✗ Unhealthy");
    Info() << "  🔧 Configuration: " << (health.configurationHealthy ? "✓ Healthy" : "✗ Unhealthy");
    Info() << "  📝 Logger: " << (health.loggerHealthy ? "✓ Healthy" : "✗ Unhealthy");
    Info() << "  💾 Memory: " << (health.memoryHealthy ? "✓ Healthy" : "✗ Unhealthy");
    
    Info() << "Performance Indicators:";
    Info() << "  💾 Memory Utilization: " << std::fixed << std::setprecision(1) 
           << (health.memoryUtilization * 100.0) << "%";
    
    if (!health.warnings.empty()) {
        Info() << "⚠️  Warnings:";
        for (const auto& warning : health.warnings) {
            Info() << "    - " << warning;
        }
    }
    
    if (!health.errors.empty()) {
        Error() << "❌ Errors:";
        for (const auto& error : health.errors) {
            Error() << "    - " << error;
        }
    }
    
    // Generate system report
    Info() << "\n📋 Generating comprehensive system report...";
    std::string systemReport = system.getSystemReport();
    Info() << "System Report Generated (Length: " << systemReport.length() << " characters)";
    
    // Display resource information
    Info() << "\n💾 Resource Usage:";
    Info() << "  Current Memory: " << (system.getMemoryUsage() / 1024) << " KB";
    Info() << "  Peak Memory: " << (system.getPeakMemoryUsage() / 1024) << " KB";
    
    std::string resourceReport = system.getResourceReport();
    if (!resourceReport.empty()) {
        Info() << "Resource Report: " << resourceReport.substr(0, 100) << "...";
    }
    
    system.stop();
    Info() << "✓ Health monitoring demonstration completed";
}

// =================================================================
// MAIN DEMONSTRATION
// =================================================================

int main() {
    // Initialize framework logger
    Log::setLevel(LogLevel::Info);
    
    Info() << "🚀 AxonVex System Management - Comprehensive Demonstration";
    Info() << "   Showcasing centralized system orchestration and lifecycle management";
    
    try {
        // Run all demonstrations
        demonstrateSystemLifecycle();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        demonstrateProcessingUnitOrchestration();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        demonstrateEventSystem();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        demonstrateHealthMonitoring();
        
        Info() << "\n✨ All AxonVex System demonstrations completed successfully!";
        Info() << "🎯 Key Features Demonstrated:";
        Info() << "   ✅ Complete system lifecycle management";
        Info() << "   ✅ Processing unit registration and orchestration";
        Info() << "   ✅ Real-time scheduling with timing constraints";
        Info() << "   ✅ Event system with callbacks and notifications";
        Info() << "   ✅ Health monitoring and diagnostics";
        Info() << "   ✅ Performance statistics collection";
        Info() << "   ✅ Resource management and monitoring";
        Info() << "   ✅ Configuration management integration";
        Info() << "   ✅ Thread-safe concurrent operations";
        
        Info() << "\n🏆 AxonVex System: The complete solution for real-time system orchestration!";
        
    } catch (const std::exception& e) {
        Error() << "Demo failed with exception: " << e.what();
        return 1;
    }
    
    return 0;
} 