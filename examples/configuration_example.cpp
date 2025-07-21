/**
 * @file configuration_example.cpp
 * @brief Comprehensive Configuration Class Example
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * This example demonstrates the comprehensive capabilities of the AxonVex Configuration class:
 * - JSON configuration loading and saving
 * - Schema validation and type safety
 * - Runtime configuration updates with callbacks
 * - Configuration templates and presets
 * - Environment variable integration
 * - Snapshots and rollback functionality
 * - Performance monitoring and optimization
 * - Thread-safe operations
 * 
 * Compile and run:
 *   cd build && make configuration_example && ./examples/configuration_example
 */

#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <cstdlib>

// Include the full AxonVex framework with Logger
#include "../include/axonvex/axonvex.hpp"

using namespace axonvex::core;
using namespace axonvex::Log;  // Use framework's Logger

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

// Sample configuration data
const std::string SAMPLE_CONFIG = R"({
    "system": {
        "name": "AxonVex Example System",
        "version": "1.0.0",
        "execution": {
            "frequency": 1000.0,
            "enabled": true,
            "realtime_mode": true,
            "priority": "high"
        },
        "logging": {
            "level": "info",
            "file": "/tmp/axonvex.log",
            "rotation": {
                "enabled": true,
                "max_size": "100MB",
                "max_files": 5
            }
        }
    },
    "network": {
        "interfaces": [
            {
                "type": "websocket",
                "port": 8080,
                "compression": true
            },
            {
                "type": "rest",
                "port": 8081,
                "authentication": true
            }
        ],
        "timeout": 5000,
        "retry_attempts": 3
    },
    "performance": {
        "monitoring": {
            "enabled": true,
            "update_rate": 100,
            "metrics": ["cpu", "memory", "latency"]
        },
        "optimization": {
            "zero_copy": true,
            "lock_free": true,
            "thread_affinity": "auto"
        }
    }
})";

// Sample JSON schema
const std::string SAMPLE_SCHEMA = R"({
    "type": "object",
    "properties": {
        "system": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "version": {"type": "string"},
                "execution": {
                    "type": "object",
                    "properties": {
                        "frequency": {"type": "number", "minimum": 1.0, "maximum": 10000.0},
                        "enabled": {"type": "boolean"},
                        "realtime_mode": {"type": "boolean"},
                        "priority": {"type": "string", "enum": ["low", "normal", "high", "realtime"]}
                    },
                    "required": ["frequency", "enabled"]
                },
                "logging": {
                    "type": "object",
                    "properties": {
                        "level": {"type": "string", "enum": ["debug", "info", "warn", "error"]},
                        "file": {"type": "string"}
                    }
                }
            },
            "required": ["name", "execution"]
        },
        "network": {
            "type": "object",
            "properties": {
                "timeout": {"type": "number", "minimum": 100},
                "retry_attempts": {"type": "number", "minimum": 1, "maximum": 10}
            }
        }
    },
    "required": ["system"]
})";

void demonstrateBasicUsage() {
    printHeader("Basic Configuration Usage");
    
    // Create configuration instance
    Configuration config(true, true);  // Enable monitoring and validation
    
    // Load configuration from string
    if (config.loadFromString(SAMPLE_CONFIG)) {
        printSuccess("Configuration loaded from JSON string");
    } else {
        printError("Failed to load configuration");
        return;
    }
    
    // Access configuration values with type safety
    auto system_name = config.get<std::string>("system.name");
    auto execution_frequency = config.get<double>("system.execution.frequency");
    auto logging_enabled = config.get<bool>("system.execution.enabled");
    auto network_timeout = config.get<int>("network.timeout");
    
    std::cout << "\nConfiguration Values:" << std::endl;
    std::cout << "  System Name: " << system_name << std::endl;
    std::cout << "  Execution Frequency: " << execution_frequency << " Hz" << std::endl;
    std::cout << "  Logging Enabled: " << (logging_enabled ? "Yes" : "No") << std::endl;
    std::cout << "  Network Timeout: " << network_timeout << " ms" << std::endl;
    
    // Test default values
    auto missing_value = config.get<std::string>("nonexistent.key", "default_value");
    auto missing_number = config.get<int>("nonexistent.number", 42);
    
    std::cout << "\nDefault Values:" << std::endl;
    std::cout << "  Missing String: " << missing_value << std::endl;
    std::cout << "  Missing Number: " << missing_number << std::endl;
    
    // Set new values
    if (config.set("runtime.timestamp", std::chrono::system_clock::now().time_since_epoch().count())) {
        printSuccess("Runtime timestamp set");
    }
    
    if (config.set("runtime.performance.cpu_usage", 45.6)) {
        printSuccess("CPU usage metric set");
    }
    
    // Check if keys exist
    std::cout << "\nKey Existence:" << std::endl;
    std::cout << "  'system.name' exists: " << (config.has("system.name") ? "Yes" : "No") << std::endl;
    std::cout << "  'nonexistent.key' exists: " << (config.has("nonexistent.key") ? "Yes" : "No") << std::endl;
    
    // Get configuration statistics
    const auto& stats = config.getStatistics();
    std::cout << "\nConfiguration Statistics:" << std::endl;
    std::cout << "  Total loads: " << stats.getTotalLoads() << std::endl;
    std::cout << "  Total updates: " << stats.getTotalUpdates() << std::endl;
    std::cout << "  Memory usage: " << config.getMemoryUsage() << " bytes" << std::endl;
    std::cout << "  Key count: " << config.getKeyCount() << std::endl;
}

void demonstrateSchemaValidation() {
    printHeader("Schema Validation");
    
    Configuration config(true, true);
    
    // Load configuration
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    // Set up schema
    nlohmann::json schema_json = nlohmann::json::parse(SAMPLE_SCHEMA);
    ConfigurationSchema schema(schema_json, "1.0", "AxonVex System Schema");
    
    if (config.setSchema(schema)) {
        printSuccess("Schema loaded and applied");
    } else {
        printError("Failed to set schema");
        return;
    }
    
    // Validate current configuration
    auto errors = config.validate();
    if (errors.empty()) {
        printSuccess("Configuration is valid according to schema");
    } else {
        printWarning("Configuration validation found " + std::to_string(errors.size()) + " errors:");
        for (const auto& error : errors) {
            std::cout << "  - " << error.key << ": " << error.message << std::endl;
        }
    }
    
    // Test validation with invalid values (this might fail with basic validation)
    printInfo("Testing validation with invalid values...");
    
    // Try to set invalid frequency (should fail validation)
    if (!config.set("system.execution.frequency", -100.0)) {
        printSuccess("Validation correctly rejected negative frequency");
    } else {
        printWarning("Validation allowed invalid negative frequency");
    }
    
    // Try to set invalid priority
    if (!config.set("system.execution.priority", std::string("invalid_priority"))) {
        printSuccess("Validation correctly rejected invalid priority");
    } else {
        printWarning("Validation allowed invalid priority value");
    }
    
    // Disable validation and try again
    config.enableValidation(false);
    printInfo("Validation disabled");
    
    if (config.set("system.execution.frequency", -100.0)) {
        printInfo("Without validation: Set negative frequency");
    }
    
    // Re-enable validation
    config.enableValidation(true);
    printInfo("Validation re-enabled");
}

void demonstrateCallbacks() {
    printHeader("Runtime Updates and Callbacks");
    
    Configuration config(true, true);
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    // Set up callback tracking
    std::vector<std::string> callback_log;
    
    // Register callback for system configuration changes
    auto system_callback = [&](const std::string& key, const ConfigValue& old_val, const ConfigValue& new_val) {
        std::string log_entry = "System config changed: " + key + 
                               " (old: " + (old_val.is_null() ? "null" : old_val.dump()) + 
                               ", new: " + new_val.dump() + ")";
        callback_log.push_back(log_entry);
        std::cout << "  📢 " << log_entry << std::endl;
    };
    
    // Register callback for performance metrics
    auto perf_callback = [&](const std::string& key, const ConfigValue& old_val, const ConfigValue& new_val) {
        std::string log_entry = "Performance metric updated: " + key + " = " + new_val.dump();
        callback_log.push_back(log_entry);
        std::cout << "  📊 " << log_entry << std::endl;
    };
    
    size_t system_callback_id = config.registerCallback("system.*", system_callback);
    size_t perf_callback_id = config.registerCallback("performance.*", perf_callback);
    
    printSuccess("Callbacks registered for 'system.*' and 'performance.*' patterns");
    
    // Trigger callbacks with configuration changes
    std::cout << "\nTriggering configuration changes..." << std::endl;
    
    config.set("system.execution.frequency", 2000.0);
    config.set("system.logging.level", std::string("debug"));
    config.set("performance.monitoring.update_rate", 50);
    config.set("performance.optimization.zero_copy", false);
    config.set("other.setting", std::string("should_not_trigger_callbacks"));
    
    // Allow time for callback processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "\nCallback Summary:" << std::endl;
    std::cout << "  Total callbacks triggered: " << callback_log.size() << std::endl;
    
    // Unregister callbacks
    config.unregisterCallback(system_callback_id);
    config.unregisterCallback(perf_callback_id);
    
    printSuccess("Callbacks unregistered");
    
    // Test bulk updates
    printInfo("Testing bulk configuration updates...");
    
    nlohmann::json updates = nlohmann::json::parse(R"({
        "system": {
            "execution": {
                "frequency": 3000.0,
                "priority": "realtime"
            }
        },
        "network": {
            "timeout": 10000,
            "retry_attempts": 5
        }
    })");
    
    if (config.applyUpdates(updates)) {
        printSuccess("Bulk updates applied successfully");
        
        // Verify updates
        std::cout << "  New frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
        std::cout << "  New priority: " << config.get<std::string>("system.execution.priority") << std::endl;
        std::cout << "  New timeout: " << config.get<int>("network.timeout") << " ms" << std::endl;
    }
}

void demonstrateTemplates() {
    printHeader("Configuration Templates");
    
    Configuration config(true, true);
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    // Save current configuration as template
    if (config.saveTemplate("production_template", "Production-ready AxonVex configuration")) {
        printSuccess("Configuration saved as 'production_template'");
    }
    
    // Create a development configuration
    config.set("system.logging.level", std::string("debug"));
    config.set("system.execution.frequency", 500.0);
    config.set("performance.monitoring.update_rate", 10);
    
    if (config.saveTemplate("development_template", "Development configuration with debug logging")) {
        printSuccess("Configuration saved as 'development_template'");
    }
    
    // Create a high-performance configuration
    config.set("system.execution.frequency", 5000.0);
    config.set("system.execution.priority", std::string("realtime"));
    config.set("performance.optimization.zero_copy", true);
    config.set("performance.optimization.lock_free", true);
    
    if (config.saveTemplate("high_performance_template", "High-performance real-time configuration")) {
        printSuccess("Configuration saved as 'high_performance_template'");
    }
    
    // List available templates
    auto templates = config.getAvailableTemplates();
    std::cout << "\nAvailable Templates:" << std::endl;
    for (const auto& name : templates) {
        auto template_info = config.getTemplate(name);
        if (template_info) {
            std::cout << "  📄 " << name << ": " << template_info->description << std::endl;
        }
    }
    
    // Demonstrate template loading
    printInfo("Switching to development template...");
    config.clear();
    
    if (config.loadTemplate("development_template")) {
        printSuccess("Development template loaded");
        std::cout << "  Logging level: " << config.get<std::string>("system.logging.level") << std::endl;
        std::cout << "  Execution frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
    }
    
    printInfo("Switching to production template...");
    if (config.loadTemplate("production_template")) {
        printSuccess("Production template loaded");
        std::cout << "  Logging level: " << config.get<std::string>("system.logging.level") << std::endl;
        std::cout << "  Execution frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
    }
}

void demonstrateSnapshotsAndRollback() {
    printHeader("Snapshots and Rollback");
    
    Configuration config(true, true);
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    // Create initial snapshot
    std::string initial_snapshot = config.createSnapshot("initial_state");
    printSuccess("Created snapshot: " + initial_snapshot);
    
    // Make some changes
    config.set("system.execution.frequency", 2000.0);
    config.set("system.logging.level", std::string("debug"));
    config.set("experimental.feature.enabled", true);
    
    std::cout << "\nAfter modifications:" << std::endl;
    std::cout << "  Frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
    std::cout << "  Log level: " << config.get<std::string>("system.logging.level") << std::endl;
    std::cout << "  Experimental feature: " << config.get<bool>("experimental.feature.enabled") << std::endl;
    
    // Create snapshot after changes
    std::string modified_snapshot = config.createSnapshot("modified_state");
    printSuccess("Created snapshot: " + modified_snapshot);
    
    // Make more changes
    config.set("system.execution.frequency", 5000.0);
    config.set("system.name", std::string("Modified System"));
    
    std::cout << "\nAfter more modifications:" << std::endl;
    std::cout << "  Frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
    std::cout << "  System name: " << config.get<std::string>("system.name") << std::endl;
    
    // List available snapshots
    auto snapshots = config.getAvailableSnapshots();
    std::cout << "\nAvailable snapshots:" << std::endl;
    for (const auto& snapshot_id : snapshots) {
        std::cout << "  📸 " << snapshot_id << std::endl;
    }
    
    // Rollback to initial state
    printInfo("Rolling back to initial state...");
    if (config.rollbackToSnapshot(initial_snapshot)) {
        printSuccess("Rollback successful");
        
        std::cout << "\nAfter rollback to initial state:" << std::endl;
        std::cout << "  Frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
        std::cout << "  Log level: " << config.get<std::string>("system.logging.level") << std::endl;
        std::cout << "  System name: " << config.get<std::string>("system.name") << std::endl;
        std::cout << "  Experimental feature exists: " << (config.has("experimental.feature.enabled") ? "Yes" : "No") << std::endl;
    }
    
    // Rollback to modified state
    printInfo("Rolling back to modified state...");
    if (config.rollbackToSnapshot(modified_snapshot)) {
        printSuccess("Rollback to modified state successful");
        
        std::cout << "\nAfter rollback to modified state:" << std::endl;
        std::cout << "  Frequency: " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
        std::cout << "  Log level: " << config.get<std::string>("system.logging.level") << std::endl;
        std::cout << "  Experimental feature: " << config.get<bool>("experimental.feature.enabled") << std::endl;
    }
    
    // Clean up snapshots
    config.removeSnapshot(initial_snapshot);
    config.removeSnapshot(modified_snapshot);
    printInfo("Snapshots cleaned up");
}

void demonstratePerformance() {
    printHeader("Performance Benchmarking");
    
    Configuration config(true, false);  // Disable validation for performance testing
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    const int num_operations = 100000;
    
    printInfo("Running performance benchmarks with " + std::to_string(num_operations) + " operations...");
    
    // Benchmark configuration reads
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        volatile auto value = config.get<std::string>("system.name");
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    
    // Benchmark configuration writes
    start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        config.set("benchmark.iteration" + std::to_string(i % 1000), i);
    }
    
    end_time = std::chrono::high_resolution_clock::now();
    auto write_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    
    // Calculate performance metrics
    double avg_read_time_ns = static_cast<double>(read_duration.count()) / num_operations;
    double avg_write_time_ns = static_cast<double>(write_duration.count()) / num_operations;
    
    double read_throughput = 1e9 / avg_read_time_ns;
    double write_throughput = 1e9 / avg_write_time_ns;
    
    std::cout << "\n" << "Performance Results:" << std::endl;
    std::cout << "  📖 Average read time:   " << std::fixed << std::setprecision(2) << avg_read_time_ns << " ns" << std::endl;
    std::cout << "  ✏️  Average write time:  " << std::fixed << std::setprecision(2) << avg_write_time_ns << " ns" << std::endl;
    std::cout << "  🚀 Read throughput:     " << std::fixed << std::setprecision(0) << read_throughput << " ops/sec" << std::endl;
    std::cout << "  🚀 Write throughput:    " << std::fixed << std::setprecision(0) << write_throughput << " ops/sec" << std::endl;
    
    // Performance targets
    const double read_target_ns = 1000.0;   // Target: <1μs read time
    const double write_target_ns = 10000.0; // Target: <10μs write time
    
    if (avg_read_time_ns < read_target_ns) {
        printSuccess("Read performance meets target (<" + std::to_string((int)read_target_ns) + "ns)");
    } else {
        printWarning("Read performance above target (" + std::to_string(avg_read_time_ns) + "ns > " + std::to_string((int)read_target_ns) + "ns)");
    }
    
    if (avg_write_time_ns < write_target_ns) {
        printSuccess("Write performance meets target (<" + std::to_string((int)write_target_ns) + "ns)");
    } else {
        printWarning("Write performance above target (" + std::to_string(avg_write_time_ns) + "ns > " + std::to_string((int)write_target_ns) + "ns)");
    }
    
    // Display comprehensive statistics
    std::cout << "\n" << config.getPerformanceMetrics() << std::endl;
}

void demonstrateFileOperations() {
    printHeader("File Operations");
    
    Configuration config(true, true);
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    const std::string config_file = "example_config.json";
    const std::string backup_file = "example_config_backup.json";
    
    // Save configuration to file
    if (config.saveToFile(config_file, true)) {
        printSuccess("Configuration saved to " + config_file);
    } else {
        printError("Failed to save configuration to file");
        return;
    }
    
    // Create a new configuration instance and load from file
    Configuration config2(true, true);
    
    if (config2.loadFromFile(config_file)) {
        printSuccess("Configuration loaded from " + config_file);
        
        // Verify loaded values
        std::cout << "  Loaded system name: " << config2.get<std::string>("system.name") << std::endl;
        std::cout << "  Loaded frequency: " << config2.get<double>("system.execution.frequency") << " Hz" << std::endl;
    } else {
        printError("Failed to load configuration from file");
    }
    
    // Modify and save as backup
    config2.set("backup.timestamp", std::chrono::system_clock::now().time_since_epoch().count());
    config2.set("backup.reason", std::string("Example backup"));
    
    if (config2.saveToFile(backup_file, true)) {
        printSuccess("Backup configuration saved to " + backup_file);
    }
    
    // Clean up files
    std::remove(config_file.c_str());
    std::remove(backup_file.c_str());
    printInfo("Temporary files cleaned up");
}

void demonstrateEnvironmentVariables() {
    printHeader("Environment Variable Integration");
    
    // Set some test environment variables
    setenv("AXONVEX_SYSTEM_NAME", "Environment System", 1);
    setenv("AXONVEX_SYSTEM_EXECUTION_FREQUENCY", "1500.0", 1);
    setenv("AXONVEX_SYSTEM_EXECUTION_ENABLED", "true", 1);
    setenv("AXONVEX_NETWORK_TIMEOUT", "8000", 1);
    setenv("AXONVEX_PERFORMANCE_MONITORING_ENABLED", "false", 1);
    
    printInfo("Environment variables set with AXONVEX_ prefix");
    
    Configuration config(true, true);
    
    // Load from environment variables
    if (config.loadFromEnvironment("AXONVEX_")) {
        printSuccess("Configuration loaded from environment variables");
        
        std::cout << "\nValues from environment:" << std::endl;
        std::cout << "  System Name: " << config.get<std::string>("system.name", "not found") << std::endl;
        std::cout << "  Execution Frequency: " << config.get<double>("system.execution.frequency", 0.0) << " Hz" << std::endl;
        std::cout << "  Execution Enabled: " << (config.get<bool>("system.execution.enabled", false) ? "Yes" : "No") << std::endl;
        std::cout << "  Network Timeout: " << config.get<int>("network.timeout", 0) << " ms" << std::endl;
        std::cout << "  Monitoring Enabled: " << (config.get<bool>("performance.monitoring.enabled", true) ? "Yes" : "No") << std::endl;
    } else {
        printError("Failed to load configuration from environment variables");
    }
    
    // Load base configuration and merge with environment
    printInfo("Loading base configuration and merging with environment variables...");
    
    if (config.loadFromString(SAMPLE_CONFIG)) {
        printSuccess("Base configuration loaded");
    }
    
    if (config.loadFromEnvironment("AXONVEX_", true)) {  // merge = true
        printSuccess("Environment variables merged with base configuration");
        
        std::cout << "\nAfter merging:" << std::endl;
        std::cout << "  System Name (from env): " << config.get<std::string>("system.name") << std::endl;
        std::cout << "  System Version (from base): " << config.get<std::string>("system.version") << std::endl;
        std::cout << "  Frequency (from env): " << config.get<double>("system.execution.frequency") << " Hz" << std::endl;
        std::cout << "  Priority (from base): " << config.get<std::string>("system.execution.priority") << std::endl;
    }
    
    // Clean up environment variables
    unsetenv("AXONVEX_SYSTEM_NAME");
    unsetenv("AXONVEX_SYSTEM_EXECUTION_FREQUENCY");
    unsetenv("AXONVEX_SYSTEM_EXECUTION_ENABLED");
    unsetenv("AXONVEX_NETWORK_TIMEOUT");
    unsetenv("AXONVEX_PERFORMANCE_MONITORING_ENABLED");
    
    printInfo("Environment variables cleaned up");
}

void demonstrateThreadSafety() {
    printHeader("Thread Safety Demonstration");
    
    Configuration config(true, true);
    
    if (!config.loadFromString(SAMPLE_CONFIG)) {
        printError("Failed to load configuration");
        return;
    }
    
    const int num_threads = 8;
    const int operations_per_thread = 1000;
    
    printInfo("Testing thread safety with " + std::to_string(num_threads) + " threads, " + 
              std::to_string(operations_per_thread) + " operations each");
    
    std::vector<std::thread> threads;
    std::atomic<int> total_operations{0};
    std::atomic<int> successful_operations{0};
    std::atomic<int> read_operations{0};
    std::atomic<int> write_operations{0};
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Launch worker threads
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                total_operations.fetch_add(1);
                
                try {
                    if (i % 3 == 0) {
                        // Read operation
                        auto value = config.get<std::string>("system.name");
                        if (!value.empty()) {
                            successful_operations.fetch_add(1);
                            read_operations.fetch_add(1);
                        }
                    } else if (i % 3 == 1) {
                        // Write operation
                        std::string key = "thread" + std::to_string(t) + ".operation" + std::to_string(i);
                        if (config.set(key, i * t)) {
                            successful_operations.fetch_add(1);
                            write_operations.fetch_add(1);
                        }
                    } else {
                        // Check operation
                        std::string key = "system.execution.frequency";
                        if (config.has(key)) {
                            successful_operations.fetch_add(1);
                            read_operations.fetch_add(1);
                        }
                    }
                } catch (const std::exception& e) {
                    // Count as failed operation
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate results
    int total_ops = total_operations.load();
    int successful_ops = successful_operations.load();
    int read_ops = read_operations.load();
    int write_ops = write_operations.load();
    
    double success_rate = (static_cast<double>(successful_ops) / total_ops) * 100.0;
    double throughput = static_cast<double>(total_ops) / total_duration.count() * 1000.0; // ops/sec
    
    std::cout << "\n" << "Thread Safety Results:" << std::endl;
    std::cout << "  📊 Total operations: " << total_ops << std::endl;
    std::cout << "  ✅ Successful operations: " << successful_ops << std::endl;
    std::cout << "  📖 Read operations: " << read_ops << std::endl;
    std::cout << "  ✏️  Write operations: " << write_ops << std::endl;
    std::cout << "  📈 Success rate: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
    std::cout << "  🚀 Throughput: " << std::fixed << std::setprecision(0) << throughput << " ops/sec" << std::endl;
    std::cout << "  ⏱️  Total time: " << total_duration.count() << " ms" << std::endl;
    
    if (success_rate > 99.0) {
        printSuccess("Thread safety test passed with " + std::to_string(success_rate) + "% success rate");
    } else {
        printWarning("Thread safety test completed with " + std::to_string(success_rate) + "% success rate");
    }
    
    // Verify data integrity
    auto final_system_name = config.get<std::string>("system.name");
    auto final_frequency = config.get<double>("system.execution.frequency");
    
    if (final_system_name == "AxonVex Example System" && final_frequency == 1000.0) {
        printSuccess("Core configuration data remained intact during concurrent operations");
    } else {
        printError("Core configuration data was corrupted during concurrent operations");
    }
}

int main() {
    std::cout << "🚀 AxonVex Configuration Class - Comprehensive Example" << std::endl;
    std::cout << "   High-Performance Configuration Management for Real-Time Systems" << std::endl;
    
    try {
        // Run all demonstrations
        demonstrateBasicUsage();
        demonstrateSchemaValidation();
        demonstrateCallbacks();
        demonstrateTemplates();
        demonstrateSnapshotsAndRollback();
        demonstrateFileOperations();
        demonstrateEnvironmentVariables();
        demonstratePerformance();
        demonstrateThreadSafety();
        
        printHeader("Example Complete");
        printSuccess("All configuration features demonstrated successfully!");
        
        std::cout << "\nKey Features Demonstrated:" << std::endl;
        std::cout << "  ✅ JSON configuration loading and saving" << std::endl;
        std::cout << "  ✅ Type-safe configuration access" << std::endl;
        std::cout << "  ✅ Schema validation and type checking" << std::endl;
        std::cout << "  ✅ Runtime configuration updates" << std::endl;
        std::cout << "  ✅ Change notification callbacks" << std::endl;
        std::cout << "  ✅ Configuration templates and presets" << std::endl;
        std::cout << "  ✅ Snapshots and rollback functionality" << std::endl;
        std::cout << "  ✅ Environment variable integration" << std::endl;
        std::cout << "  ✅ High-performance operations" << std::endl;
        std::cout << "  ✅ Thread-safe concurrent access" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        printError("Exception occurred: " + std::string(e.what()));
        return 1;
    } catch (...) {
        printError("Unknown exception occurred");
        return 1;
    }
} 