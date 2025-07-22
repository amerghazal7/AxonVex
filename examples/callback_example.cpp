/**
 * @file callback_example.cpp
 * @brief Comprehensive example demonstrating AxonVex callback system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * This example demonstrates how to use the AxonVex callback system for
 * event-driven programming. It shows both basic Caller and advanced
 * CallerKeyed functionality.
 */

#include <axonvex/axonvex.hpp>
#include <iostream>
#include <string>
#include <memory>

// Example data structures for callbacks
struct SensorData {
    int sensor_id;
    double value;
    std::string timestamp;

    SensorData(int id, double val, const std::string& time)
        : sensor_id(id), value(val), timestamp(time) {}
};

struct LogMessage {
    std::string level;
    std::string message;

    LogMessage(const std::string& lvl, const std::string& msg)
        : level(lvl), message(msg) {}
};

// Example callback implementations
class SensorProcessor : public axonvex::core::Callback<SensorData> {
public:
    void callbackPerform(const SensorData data) override {
        std::cout << "[SensorProcessor] Processing sensor " << data.sensor_id
                  << " with value " << data.value
                  << " at " << data.timestamp << std::endl;

        // Simulate processing
        if (data.value > 100.0) {
            std::cout << "[SensorProcessor] WARNING: High sensor reading!" << std::endl;
        }
    }
};

class DataLogger : public axonvex::core::Callback<SensorData> {
public:
    void callbackPerform(const SensorData data) override {
        std::cout << "[DataLogger] Logging: " << data.sensor_id
                  << "," << data.value
                  << "," << data.timestamp << std::endl;
    }
};

class SimpleLogger : public axonvex::core::Callback<LogMessage> {
public:
    void callbackPerform(const LogMessage data) override {
        std::cout << "[" << data.level << "] " << data.message << std::endl;
    }
};

class FileLogger : public axonvex::core::Callback<LogMessage> {
public:
    void callbackPerform(const LogMessage data) override {
        std::cout << "[FileLogger] Writing to file: [" << data.level << "] " << data.message << std::endl;
    }
};

// Example of a callback that might throw an exception
class UnstableProcessor : public axonvex::core::Callback<int> {
public:
    void callbackPerform(const int data) override {
        std::cout << "[UnstableProcessor] Processing: " << data << std::endl;
        if (data == 42) {
            throw std::runtime_error("The answer to everything causes problems!");
        }
        std::cout << "[UnstableProcessor] Successfully processed: " << data << std::endl;
    }
};

class ReliableProcessor : public axonvex::core::Callback<int> {
public:
    void callbackPerform(const int data) override {
        std::cout << "[ReliableProcessor] Reliably processing: " << data << std::endl;
    }
};

void demonstrateBasicCaller() {
    std::cout << "\n=== Basic Caller Demonstration ===" << std::endl;

    // Create callback instances
    SensorProcessor processor;
    DataLogger logger;

    // Create caller for SensorData
    axonvex::core::Caller<SensorData> sensorCaller;

    // Register callbacks
    sensorCaller.registerCallback(&processor);
    sensorCaller.registerCallback(&logger);

    std::cout << "Registered " << sensorCaller.getCallbackCount() << " callbacks" << std::endl;

    // Trigger callbacks with sample data
    SensorData sample1(101, 85.5, "2025-01-20T10:30:00");
    SensorData sample2(102, 125.7, "2025-01-20T10:30:01");

    std::cout << "\nTriggering callbacks with normal reading:" << std::endl;
    sensorCaller.callCallbacks(sample1);

    std::cout << "\nTriggering callbacks with high reading:" << std::endl;
    sensorCaller.callCallbacks(sample2);

    // Demonstrate unregistration
    std::cout << "\nUnregistering processor..." << std::endl;
    sensorCaller.unregisterCallback(&processor);
    std::cout << "Remaining callbacks: " << sensorCaller.getCallbackCount() << std::endl;

    std::cout << "\nTriggering callbacks after unregistration:" << std::endl;
    sensorCaller.callCallbacks(sample1);
}

void demonstrateKeyedCaller() {
    std::cout << "\n=== Keyed Caller Demonstration ===" << std::endl;

    // Create callback instances
    SimpleLogger simpleLogger;
    FileLogger fileLogger;

    // Create keyed caller for LogMessage with string keys
    axonvex::core::CallerKeyed<std::string, LogMessage> logCaller;

    // Register callbacks with different keys
    logCaller.registerKeyedCallback("INFO", &simpleLogger);
    logCaller.registerKeyedCallback("ERROR", &simpleLogger);
    logCaller.registerKeyedCallback("ERROR", &fileLogger);  // Multiple callbacks for ERROR
    logCaller.registerKeyedCallback("DEBUG", &simpleLogger);

    std::cout << "Total registered callbacks: " << logCaller.getTotalCallbackCount() << std::endl;
    std::cout << "ERROR callbacks: " << logCaller.getCallbackCountForKey("ERROR") << std::endl;
    std::cout << "INFO callbacks: " << logCaller.getCallbackCountForKey("INFO") << std::endl;

    // Trigger callbacks for specific keys
    LogMessage infoMsg("INFO", "System started successfully");
    LogMessage errorMsg("ERROR", "Connection failed");
    LogMessage debugMsg("DEBUG", "Variable x = 42");

    std::cout << "\nTriggering INFO callbacks:" << std::endl;
    logCaller.callCallbacksByKey("INFO", infoMsg);

    std::cout << "\nTriggering ERROR callbacks (should call both loggers):" << std::endl;
    logCaller.callCallbacksByKey("ERROR", errorMsg);

    std::cout << "\nTriggering DEBUG callbacks:" << std::endl;
    logCaller.callCallbacksByKey("DEBUG", debugMsg);

    // Demonstrate calling all callbacks
    LogMessage broadcastMsg("BROADCAST", "System shutdown initiated");
    std::cout << "\nCalling all callbacks with broadcast message:" << std::endl;
    logCaller.callAllCallbacks(broadcastMsg);

    // Show all registered keys
    auto keys = logCaller.getAllKeys();
    std::cout << "\nRegistered keys: ";
    for (const auto& key : keys) {
        std::cout << key << " ";
    }
    std::cout << std::endl;
}

void demonstrateExceptionSafety() {
    std::cout << "\n=== Exception Safety Demonstration ===" << std::endl;

    UnstableProcessor unstable;
    ReliableProcessor reliable1;
    ReliableProcessor reliable2;

    axonvex::core::Caller<int> intCaller;
    intCaller.registerCallback(&reliable1);
    intCaller.registerCallback(&unstable);
    intCaller.registerCallback(&reliable2);

    std::cout << "Testing normal operation (no exceptions):" << std::endl;
    size_t exceptions = intCaller.callCallbacksSafe(10);
    std::cout << "Exceptions caught: " << exceptions << std::endl;

    std::cout << "\nTesting with exception-causing data:" << std::endl;
    exceptions = intCaller.callCallbacksSafe(42);
    std::cout << "Exceptions caught: " << exceptions << std::endl;

    std::cout << "\nCompare with unsafe calling (this would normally crash):" << std::endl;
    std::cout << "Using callCallbacksSafe ensures all callbacks run even if some throw." << std::endl;
}

// String callback for numeric keyed demo
class StringCallbackForDemo : public axonvex::core::Callback<std::string> {
public:
    void callbackPerform(const std::string data) override {
        std::cout << "[StringCallback] Received: " << data << std::endl;
    }
};

void demonstrateAdvancedKeyedFeatures() {
    std::cout << "\n=== Advanced Keyed Caller Features ===" << std::endl;

    StringCallbackForDemo stringLogger;
    axonvex::core::CallerKeyed<int, std::string> numericKeyCaller;

    // Register callbacks with numeric keys
    numericKeyCaller.registerKeyedCallback(100, &stringLogger);
    numericKeyCaller.registerKeyedCallback(200, &stringLogger);
    numericKeyCaller.registerKeyedCallback(100, &stringLogger);  // Duplicate key

    std::cout << "Registered callbacks for key 100: "
              << numericKeyCaller.getCallbackCountForKey(100) << std::endl;
    std::cout << "Registered callbacks for key 200: "
              << numericKeyCaller.getCallbackCountForKey(200) << std::endl;

    // Test key existence
    std::cout << "Has callbacks for key 100: "
              << (numericKeyCaller.hasCallbacksForKey(100) ? "Yes" : "No") << std::endl;
    std::cout << "Has callbacks for key 300: "
              << (numericKeyCaller.hasCallbacksForKey(300) ? "Yes" : "No") << std::endl;

    // Call callbacks for specific keys
    std::cout << "\nCalling callbacks for key 100:" << std::endl;
    numericKeyCaller.callCallbacksByKey(100, "Message for key 100");

    std::cout << "\nCalling callbacks for key 200:" << std::endl;
    numericKeyCaller.callCallbacksByKey(200, "Message for key 200");

    // Demonstrate key cleanup
    std::cout << "\nRemoving all callbacks for key 100..." << std::endl;
    size_t removed = numericKeyCaller.unregisterAllCallbacksForKey(100);
    std::cout << "Removed " << removed << " callbacks" << std::endl;

    std::cout << "Remaining total callbacks: "
              << numericKeyCaller.getTotalCallbackCount() << std::endl;
}

int main() {
    std::cout << "AxonVex Callback System Demonstration" << std::endl;
    std::cout << "====================================" << std::endl;

    try {
        demonstrateBasicCaller();
        demonstrateKeyedCaller();
        demonstrateExceptionSafety();
        demonstrateAdvancedKeyedFeatures();

        std::cout << "\n=== Demonstration Complete ===" << std::endl;
        std::cout << "The callback system provides powerful event-driven capabilities:" << std::endl;
        std::cout << "• Basic Caller for simple publisher-subscriber patterns" << std::endl;
        std::cout << "• CallerKeyed for sophisticated event routing" << std::endl;
        std::cout << "• Exception safety with *Safe methods" << std::endl;
        std::cout << "• Flexible registration/unregistration" << std::endl;
        std::cout << "• Support for any data types and key types" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
