#include <atomic>
#include <axonvex/axonvex.hpp>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <thread>
#include <vector>

using namespace axonvex;
using namespace axonvex::core;
using namespace axonvex::Log; // Use framework's Logger

// Example Processing Units for demonstration
class SineWaveGenerator : public ProcessingUnit {
  private:
    std::atomic<uint64_t> sampleCount_{0};
    double frequency_{1.0}; // Hz
    double amplitude_{1.0};
    axonvex::core::OutputPort<double>* output_;

  public:
    explicit SineWaveGenerator(const std::string& name, double frequency = 1.0)
        : ProcessingUnit(name), frequency_(frequency) {
        output_ = createOutputPort<double>(100, "sine_output");
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        auto count = sampleCount_.fetch_add(1);
        double time = static_cast<double>(count) * 0.001; // 1ms time steps
        double value = amplitude_ * std::sin(2.0 * M_PI * frequency_ * time);

        output_->write(value);
    }

    void processAsync() override {
        processSync(); // Simple implementation
    }

    void reset() override {
        sampleCount_.store(0);
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "SineWaveGenerator";
    }

    double getLastValue() const {
        // For demonstration - in real implementation would read from output port
        auto count = sampleCount_.load();
        double time = static_cast<double>(count - 1) * 0.001;
        return amplitude_ * std::sin(2.0 * M_PI * frequency_ * time);
    }
};

class DataProcessor : public ProcessingUnit {
  private:
    InputPort<double>* input_;
    OutputPort<double>* output_;
    std::atomic<uint64_t> processedSamples_{0};
    std::atomic<double> runningSum_{0.0};

  public:
    explicit DataProcessor(const std::string& name) : ProcessingUnit(name) {
        input_ = createInputPort<double>(200, "data_input");
        output_ = createOutputPort<double>(201, "processed_output");
        initialize();
    }

    void initialize() override {
        setState(ExecutionState::INITIALIZED);
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);

        if (input_->hasNewData()) {
            double value = input_->read();

            // Simple processing: running average
            auto count = processedSamples_.fetch_add(1) + 1;
            double sum = runningSum_.load();
            sum = (sum * (count - 1) + value) / count;
            runningSum_.store(sum);

            output_->write(sum);
            input_->clearNewDataFlag();
        }
    }

    void processAsync() override {
        processSync();
    }

    void reset() override {
        processedSamples_.store(0);
        runningSum_.store(0.0);
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "DataProcessor";
    }

    uint64_t getProcessedSamples() const {
        return processedSamples_.load();
    }

    double getRunningAverage() const {
        return runningSum_.load();
    }
};

class PerformanceMonitor : public ProcessingUnit {
  private:
    std::atomic<uint64_t> monitoringCycles_{0};
    std::chrono::steady_clock::time_point startTime_;

  public:
    explicit PerformanceMonitor(const std::string& name) : ProcessingUnit(name) {
        initialize();
    }

    void initialize() override {
        startTime_ = std::chrono::steady_clock::now();
        setState(ExecutionState::INITIALIZED);
    }

    void processSync() override {
        setState(ExecutionState::RUNNING);
        monitoringCycles_.fetch_add(1);

        // In a real implementation, this would collect system metrics
        // For demo, we just count cycles
    }

    void processAsync() override {
        processSync();
    }

    void reset() override {
        monitoringCycles_.store(0);
        startTime_ = std::chrono::steady_clock::now();
        setState(ExecutionState::INITIALIZED);
    }

    std::string getTypeDescription() override {
        return "PerformanceMonitor";
    }

    uint64_t getMonitoringCycles() const {
        return monitoringCycles_.load();
    }

    double getRunTimeSeconds() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
        return duration.count() / 1000.0;
    }
};

void printHeader(const std::string& title) {
    Info() << "\n=== " << title << " ===";
}

void printSuccess(const std::string& message) {
    Info() << "✓ " << message;
}

void printInfo(const std::string& message) {
    Info() << "ℹ " << message;
}

void printWarning(const std::string& message) {
    Warn() << "⚠ " << message;
}

void printError(const std::string& message) {
    Error() << "✗ " << message;
}

void demonstrateBasicScheduling() {
    printHeader("Basic Real-Time Scheduling");

    // Create timing controller
    TimingController controller(SchedulingPolicy::PRIORITY_BASED);

    // Create processing units
    auto sineGen = std::make_unique<SineWaveGenerator>("SineGen1", 2.0);
    auto processor = std::make_unique<DataProcessor>("DataProcessor1");
    // Note: PerformanceMonitor is a forward declaration only, creating a simple data processor
    // instead
    auto monitor = std::make_unique<DataProcessor>("PerfMonitor1");

    // Connect sine generator to processor (using different port IDs)
    auto* sineOutput = sineGen->createOutputPort<double>(10, "sine_out");
    auto* procInput = processor->createInputPort<double>(11, "proc_in");
    sineOutput->connect(procInput);

    printInfo("Created processing units and connections");

    // Define timing constraints
    TimingConstraints highPriorityConstraints;
    highPriorityConstraints.period = std::chrono::milliseconds(10);  // 100 Hz
    highPriorityConstraints.deadline = std::chrono::milliseconds(8); // 8ms deadline
    highPriorityConstraints.wcet = std::chrono::milliseconds(2);     // 2ms WCET
    highPriorityConstraints.priority = SchedulerPriority::HIGH;
    highPriorityConstraints.isRealTime = true;

    TimingConstraints mediumPriorityConstraints;
    mediumPriorityConstraints.period = std::chrono::milliseconds(20); // 50 Hz
    mediumPriorityConstraints.deadline = std::chrono::milliseconds(18);
    mediumPriorityConstraints.wcet = std::chrono::milliseconds(3);
    mediumPriorityConstraints.priority = SchedulerPriority::NORMAL;
    mediumPriorityConstraints.isRealTime = true;

    TimingConstraints lowPriorityConstraints;
    lowPriorityConstraints.period = std::chrono::milliseconds(100); // 10 Hz
    lowPriorityConstraints.deadline = std::chrono::milliseconds(90);
    lowPriorityConstraints.wcet = std::chrono::milliseconds(5);
    lowPriorityConstraints.priority = SchedulerPriority::LOW;
    lowPriorityConstraints.isRealTime = false;

    // Schedule processing units
    uint32_t sineTaskId = controller.scheduleProcessingUnit(sineGen.get(), highPriorityConstraints);
    uint32_t procTaskId =
        controller.scheduleProcessingUnit(processor.get(), mediumPriorityConstraints);
    uint32_t monitorTaskId =
        controller.scheduleProcessingUnit(monitor.get(), lowPriorityConstraints);

    printInfo("Scheduled processing units with different priorities");
    Info() << "  📊 Sine Generator: Task ID " << sineTaskId << " (HIGH priority, 100 Hz)";
    Info() << "  🔄 Data Processor: Task ID " << procTaskId << " (NORMAL priority, 50 Hz)";
    Info() << "  📈 Performance Monitor: Task ID " << monitorTaskId << " (LOW priority, 10 Hz)";

    // Validate schedulability
    if (controller.validateSchedulability()) {
        printSuccess("System is schedulable according to Liu & Layland bound");
    } else {
        printWarning("System may not be schedulable - check timing constraints");
    }

    // Enable real-time mode and start scheduling
    controller.enableRealTimeMode(true);
    controller.setTimerResolution(std::chrono::microseconds(100)); // 100μs resolution
    controller.start();

    printInfo("Real-time scheduler started with 100μs resolution");

    // Run for demonstration period
    printWarning("Running real-time scheduler for 2 seconds...");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // Get performance metrics
    auto stats = controller.getPerformanceMetrics();

    Info() << "\n📊 Performance Statistics:";
    Info() << "  Total Executions: " << stats.totalExecutions;
    Info() << "  Successful Executions: " << stats.successfulExecutions;
    Info() << "  Failed Executions: " << stats.failedExecutions;
    Info() << "  Missed Deadlines: " << stats.missedDeadlines;
    Info() << "  Average Execution Time: " << stats.averageExecutionTime.count() << " μs";
    Info() << "  Max Execution Time: " << stats.maxExecutionTime.count() << " μs";
    Info() << "  Min Execution Time: " << stats.minExecutionTime.count() << " μs";

    // Show processing unit results
    Info() << "\n🎯 Processing Unit Results:";
    Info() << "  Sine Generator: Last value = " << std::fixed << std::setprecision(3)
           << sineGen->getLastValue() << " (Generated samples)";
    Info() << "  Data Processor: Processed " << processor->getProcessedSamples() << " samples";
    Info() << "  Performance Monitor: monitoring cycles completed";

    // Stop scheduler
    controller.stop();
    printSuccess("Scheduler stopped successfully");

    // Calculate success rate
    if (stats.totalExecutions > 0) {
        double successRate = (static_cast<double>(stats.successfulExecutions) /
                              static_cast<double>(stats.totalExecutions)) *
                             100.0;
        Info() << "\n🎉 Success Rate: " << std::fixed << std::setprecision(1) << successRate
               << "% (" << stats.successfulExecutions << "/" << stats.totalExecutions << ")";

        if (stats.missedDeadlines == 0) {
            printSuccess("Perfect timing - no missed deadlines!");
        } else {
            double missRate = (double)stats.missedDeadlines / stats.totalExecutions * 100.0;
            printWarning("Deadline miss rate: " + std::to_string(missRate) + "%");
        }
    }
}

void demonstrateSchedulingPolicies() {
    printHeader("Different Scheduling Policies");

    std::vector<SchedulingPolicy> policies = {
        SchedulingPolicy::PRIORITY_BASED, SchedulingPolicy::EARLIEST_DEADLINE_FIRST,
        SchedulingPolicy::RATE_MONOTONIC, SchedulingPolicy::ROUND_ROBIN};

    std::vector<std::string> policyNames = {"Priority-Based", "Earliest Deadline First",
                                            "Rate Monotonic", "Round Robin"};

    for (size_t i = 0; i < policies.size(); ++i) {
        Info() << "\nTesting " << policyNames[i] << " Scheduling:";

        TimingController controller(policies[i]);

        // Create simple test units
        auto unit1 = std::make_unique<SineWaveGenerator>("Unit1", 1.0);
        auto unit2 = std::make_unique<SineWaveGenerator>("Unit2", 2.0);

        // Different timing constraints for comparison
        TimingConstraints fast;
        fast.period = std::chrono::milliseconds(10);
        fast.deadline = std::chrono::milliseconds(8);
        fast.priority = SchedulerPriority::HIGH;

        TimingConstraints slow;
        slow.period = std::chrono::milliseconds(50);
        slow.deadline = std::chrono::milliseconds(40);
        slow.priority = SchedulerPriority::LOW;

        controller.scheduleProcessingUnit(unit1.get(), fast);
        controller.scheduleProcessingUnit(unit2.get(), slow);

        controller.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stats = controller.getPerformanceMetrics();
        controller.stop();

        Info() << "  Executions: " << stats.totalExecutions
               << ", Success: " << stats.successfulExecutions
               << ", Missed Deadlines: " << stats.missedDeadlines;
    }
}

void demonstrateCustomScheduler() {
    printHeader("Custom Scheduling Algorithm");

    TimingController controller(SchedulingPolicy::CUSTOM);

    // Set custom scheduler that alternates between tasks
    static size_t lastIndex = 0;
    controller.setCustomScheduler([](const std::vector<core::SchedulerTask>& tasks) -> uint32_t {
        if (tasks.empty())
            return 0;

        auto now = std::chrono::steady_clock::now();

        // Find tasks ready to execute
        std::vector<uint32_t> readyTasks;
        for (const auto& task : tasks) {
            if (task.active.load() && !task.executing.load() && now >= task.nextExecution) {
                readyTasks.push_back(task.taskId);
            }
        }

        if (readyTasks.empty())
            return 0;

        // Custom logic: round-robin through ready tasks
        lastIndex = (lastIndex + 1) % readyTasks.size();
        return readyTasks[lastIndex];
    });

    printInfo("Custom scheduler set up with round-robin ready task selection");

    // Create test units (using DataProcessor since PerformanceMonitor doesn't exist)
    auto unit1 = std::make_unique<DataProcessor>("Custom1");
    auto unit2 = std::make_unique<DataProcessor>("Custom2");
    auto unit3 = std::make_unique<DataProcessor>("Custom3");

    TimingConstraints constraints;
    constraints.period = std::chrono::milliseconds(20);
    constraints.deadline = std::chrono::milliseconds(15);

    controller.scheduleProcessingUnit(unit1.get(), constraints);
    controller.scheduleProcessingUnit(unit2.get(), constraints);
    controller.scheduleProcessingUnit(unit3.get(), constraints);

    controller.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto stats = controller.getPerformanceMetrics();
    controller.stop();

    Info() << "Custom scheduler results:";
    Info() << "  Total executions: " << stats.totalExecutions;
    Info() << "  Unit1 status: " << (unit1->isRunning() ? "Running" : "Stopped");
    Info() << "  Unit2 status: " << (unit2->isRunning() ? "Running" : "Stopped");
    Info() << "  Unit3 status: " << (unit3->isRunning() ? "Running" : "Stopped");

    printSuccess("Custom scheduler demonstration completed");
}

int main() {
    // Initialize framework logger
    Log::setLevel(LogLevel::Info);

    printWelcome();

    Info() << "\n🚀 AxonVex TimingController Real-Time Scheduling Demo";
    Info() << "    Demonstrating microsecond-precision task scheduling with priority management";

    try {
        printHeader("Basic Real-Time Scheduling");
        demonstrateBasicScheduling();

        printHeader("Scheduling Policy Comparison");
        demonstrateSchedulingPolicies();

        printHeader("Custom Scheduling Algorithm");
        demonstrateCustomScheduler();

        Info() << "\n✨ All demonstrations completed successfully!";
        Info() << "📈 The TimingController provides deterministic, real-time task scheduling";
        Info() << "🎯 Perfect for robotics, control systems, and real-time applications";

    } catch (const std::exception& e) {
        Error() << "Demo failed with exception: " << e.what();
        return 1;
    }

    return 0;
}
