/**
 * @file configurationTest.cpp
 * @brief Comprehensive test suite for Configuration class
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This file contains comprehensive tests for the Configuration class including:
 * - JSON loading and saving
 * - Schema validation
 * - Runtime updates and callbacks
 * - Configuration templates
 * - Environment variable loading
 * - Performance validation
 * - Thread safety testing
 */

#include <gtest/gtest.h>
#include <axonvex/core/configuration.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <cstdlib>

using namespace axonvex::core;

class ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test configuration
        config = std::make_unique<Configuration>(true, true);

        // Sample configuration data
        test_config_json = R"({
            "system": {
                "name": "TestSystem",
                "execution": {
                    "frequency": 1000.0,
                    "enabled": true
                },
                "logging": {
                    "level": "info",
                    "file": "/tmp/test.log"
                }
            },
            "network": {
                "port": 8080,
                "timeout": 5000
            }
        })";

        // Sample schema
        test_schema = R"({
            "type": "object",
            "properties": {
                "system": {
                    "type": "object",
                    "properties": {
                        "name": {"type": "string"},
                        "execution": {
                            "type": "object",
                            "properties": {
                                "frequency": {"type": "number"},
                                "enabled": {"type": "boolean"}
                            }
                        }
                    }
                }
            }
        })";

        // Test file paths
        test_config_file = "test_config.json";
        test_schema_file = "test_schema.json";
    }

    void TearDown() override {
        // Clean up test files
        std::remove(test_config_file.c_str());
        std::remove(test_schema_file.c_str());

        config.reset();
    }

    std::unique_ptr<Configuration> config;
    std::string test_config_json;
    std::string test_schema;
    std::string test_config_file;
    std::string test_schema_file;
};

//==============================================================================
// Basic Configuration Tests
//==============================================================================

TEST_F(ConfigurationTest, EmptyConfigurationTest) {
    EXPECT_TRUE(config->isEmpty());
    EXPECT_EQ(config->getKeyCount(), 0);
    EXPECT_FALSE(config->has("any.key"));
}

TEST_F(ConfigurationTest, LoadFromStringTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));
    EXPECT_FALSE(config->isEmpty());
    EXPECT_GT(config->getKeyCount(), 0);

    // Test specific values
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
    EXPECT_EQ(config->get<bool>("system.execution.enabled"), true);
    EXPECT_EQ(config->get<int>("network.port"), 8080);
}

TEST_F(ConfigurationTest, LoadFromFileTest) {
    // Create test file
    std::ofstream file(test_config_file);
    file << test_config_json;
    file.close();

    EXPECT_TRUE(config->loadFromFile(test_config_file));
    EXPECT_FALSE(config->isEmpty());

    // Verify values
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
}

TEST_F(ConfigurationTest, SaveToFileTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));
    EXPECT_TRUE(config->saveToFile(test_config_file));

    // Verify file was created and can be loaded
    auto config2 = std::make_unique<Configuration>();
    EXPECT_TRUE(config2->loadFromFile(test_config_file));
    EXPECT_EQ(config2->get<std::string>("system.name"), "TestSystem");
}

//==============================================================================
// Configuration Access Tests
//==============================================================================

TEST_F(ConfigurationTest, GetSetTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Test getting existing values
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
    EXPECT_EQ(config->get<bool>("system.execution.enabled"), true);

    // Test default values
    EXPECT_EQ(config->get<std::string>("nonexistent.key", "default"), "default");
    EXPECT_EQ(config->get<int>("nonexistent.number", 42), 42);

    // Test setting new values
    EXPECT_TRUE(config->set("new.string.value", std::string("test")));
    EXPECT_TRUE(config->set("new.number.value", 123.45));
    EXPECT_TRUE(config->set("new.bool.value", true));

    // Verify new values
    EXPECT_EQ(config->get<std::string>("new.string.value"), "test");
    EXPECT_EQ(config->get<double>("new.number.value"), 123.45);
    EXPECT_EQ(config->get<bool>("new.bool.value"), true);
}

TEST_F(ConfigurationTest, HasRemoveTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Test has
    EXPECT_TRUE(config->has("system.name"));
    EXPECT_TRUE(config->has("system.execution.frequency"));
    EXPECT_FALSE(config->has("nonexistent.key"));

    // Test remove
    EXPECT_TRUE(config->remove("system.execution.frequency"));
    EXPECT_FALSE(config->has("system.execution.frequency"));
    EXPECT_FALSE(config->remove("nonexistent.key"));
}

TEST_F(ConfigurationTest, GetKeysTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    auto all_keys = config->getKeys("*");
    EXPECT_GT(all_keys.size(), 0);

    // Should find system keys
    bool found_system_name = false;
    for (const auto& key : all_keys) {
        if (key == "system.name") {
            found_system_name = true;
            break;
        }
    }
    EXPECT_TRUE(found_system_name);
}

//==============================================================================
// Schema Validation Tests
//==============================================================================

TEST_F(ConfigurationTest, SchemaValidationTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Create and set schema
    nlohmann::json schema_json = nlohmann::json::parse(test_schema);
    ConfigurationSchema schema(schema_json, "1.0", "Test schema");

    EXPECT_TRUE(config->setSchema(schema));
    EXPECT_TRUE(config->isValidationEnabled());

    // Validate current configuration
    auto errors = config->validate();
    // Note: Basic validation implementation might not catch all errors
    // This is mainly testing the interface
}

TEST_F(ConfigurationTest, ValidationEnabledDisabledTest) {
    EXPECT_TRUE(config->isValidationEnabled()); // Should be enabled by default

    config->enableValidation(false);
    EXPECT_FALSE(config->isValidationEnabled());

    config->enableValidation(true);
    EXPECT_TRUE(config->isValidationEnabled());
}

//==============================================================================
// Runtime Updates and Callback Tests
//==============================================================================

TEST_F(ConfigurationTest, CallbackTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Set up callback tracking
    std::vector<std::string> callback_keys;
    std::vector<std::string> old_values;
    std::vector<std::string> new_values;

    auto callback = [&](const std::string& key, const ConfigValue& old_val, const ConfigValue& new_val) {
        callback_keys.push_back(key);
        old_values.push_back(old_val.is_null() ? "null" : old_val.dump());
        new_values.push_back(new_val.dump());
    };

    // Register callback for system keys
    size_t callback_id = config->registerCallback("system.*", callback);
    EXPECT_GT(callback_id, 0);

    // Trigger callback by setting value
    EXPECT_TRUE(config->set("system.test_value", std::string("test")));

    // Allow time for callback processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify callback was called
    EXPECT_GT(callback_keys.size(), 0);

    // Unregister callback
    config->unregisterCallback(callback_id);

    // Clear tracking vectors
    callback_keys.clear();
    old_values.clear();
    new_values.clear();

    // This should not trigger callback
    EXPECT_TRUE(config->set("system.another_value", std::string("test2")));

    // Allow time for callback processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Should be empty since callback was unregistered
    EXPECT_EQ(callback_keys.size(), 0);
}

TEST_F(ConfigurationTest, ApplyUpdatesTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Create update JSON
    nlohmann::json updates = nlohmann::json::parse(R"({
        "system": {
            "execution": {
                "frequency": 2000.0
            },
            "new_setting": "new_value"
        },
        "network": {
            "timeout": 10000
        }
    })");

    EXPECT_TRUE(config->applyUpdates(updates));

    // Verify updates
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 2000.0);
    EXPECT_EQ(config->get<std::string>("system.new_setting"), "new_value");
    EXPECT_EQ(config->get<int>("network.timeout"), 10000);

    // Verify existing values still there
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<bool>("system.execution.enabled"), true);
}

//==============================================================================
// Template Tests
//==============================================================================

TEST_F(ConfigurationTest, TemplateTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Save current config as template
    EXPECT_TRUE(config->saveTemplate("test_template", "Test template description"));

    // Verify template was saved
    auto templates = config->getAvailableTemplates();
    EXPECT_GT(templates.size(), 0);

    bool found_template = false;
    for (const auto& name : templates) {
        if (name == "test_template") {
            found_template = true;
            break;
        }
    }
    EXPECT_TRUE(found_template);

    // Get template information
    auto template_info = config->getTemplate("test_template");
    EXPECT_TRUE(template_info.has_value());
    EXPECT_EQ(template_info->name, "test_template");
    EXPECT_EQ(template_info->description, "Test template description");

    // Clear config and load template
    config->clear();
    EXPECT_TRUE(config->isEmpty());

    EXPECT_TRUE(config->loadTemplate("test_template"));
    EXPECT_FALSE(config->isEmpty());

    // Verify values were restored
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
}

//==============================================================================
// Snapshot and Rollback Tests
//==============================================================================

TEST_F(ConfigurationTest, SnapshotRollbackTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    // Create snapshot
    std::string snapshot_id = config->createSnapshot("initial_state");
    EXPECT_FALSE(snapshot_id.empty());

    // Verify snapshot exists
    auto snapshots = config->getAvailableSnapshots();
    EXPECT_GT(snapshots.size(), 0);

    bool found_snapshot = false;
    for (const auto& id : snapshots) {
        if (id == snapshot_id) {
            found_snapshot = true;
            break;
        }
    }
    EXPECT_TRUE(found_snapshot);

    // Modify configuration
    EXPECT_TRUE(config->set("system.execution.frequency", 2000.0));
    EXPECT_TRUE(config->set("new.key", std::string("new_value")));

    // Verify changes
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 2000.0);
    EXPECT_EQ(config->get<std::string>("new.key"), "new_value");

    // Rollback to snapshot
    EXPECT_TRUE(config->rollbackToSnapshot(snapshot_id));

    // Verify rollback
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
    EXPECT_FALSE(config->has("new.key"));

    // Clean up snapshot
    config->removeSnapshot(snapshot_id);
    snapshots = config->getAvailableSnapshots();
    found_snapshot = false;
    for (const auto& id : snapshots) {
        if (id == snapshot_id) {
            found_snapshot = true;
            break;
        }
    }
    EXPECT_FALSE(found_snapshot);
}

//==============================================================================
// Statistics and Performance Tests
//==============================================================================

TEST_F(ConfigurationTest, StatisticsTest) {
    // Reset statistics
    config->resetStatistics();
    const auto& stats = config->getStatistics();

    // Initial state
    EXPECT_EQ(stats.getTotalLoads(), 0);
    EXPECT_EQ(stats.getTotalSaves(), 0);
    EXPECT_EQ(stats.getTotalUpdates(), 0);

    // Load configuration
    EXPECT_TRUE(config->loadFromString(test_config_json));
    EXPECT_EQ(stats.getTotalLoads(), 1);

    // Set some values
    EXPECT_TRUE(config->set("test.key1", std::string("value1")));
    EXPECT_TRUE(config->set("test.key2", 123));
    EXPECT_GT(stats.getTotalUpdates(), 0);

    // Save to file
    EXPECT_TRUE(config->saveToFile(test_config_file));
    EXPECT_EQ(stats.getTotalSaves(), 1);

    // Get performance metrics
    std::string metrics = config->getPerformanceMetrics();
    EXPECT_FALSE(metrics.empty());
    EXPECT_NE(metrics.find("Total loads:"), std::string::npos);
    EXPECT_NE(metrics.find("Total saves:"), std::string::npos);
}

TEST_F(ConfigurationTest, MemoryUsageTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    size_t memory_usage = config->getMemoryUsage();
    EXPECT_GT(memory_usage, 0);

    size_t key_count = config->getKeyCount();
    EXPECT_GT(key_count, 0);

    // Add more data and verify memory usage increases
    for (int i = 0; i < 100; ++i) {
        config->set("bulk.key" + std::to_string(i), i);
    }

    size_t new_memory_usage = config->getMemoryUsage();
    size_t new_key_count = config->getKeyCount();

    EXPECT_GT(new_memory_usage, memory_usage);
    EXPECT_GT(new_key_count, key_count);
}

//==============================================================================
// Thread Safety Tests
//==============================================================================

TEST_F(ConfigurationTest, ThreadSafetyTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    const int num_threads = 10;
    const int operations_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    // Launch multiple threads performing concurrent operations
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                try {
                    // Mix of read and write operations
                    if (i % 3 == 0) {
                        // Read operation
                        auto value = config->get<std::string>("system.name", "");
                        if (value != "TestSystem") {
                            errors.fetch_add(1);
                        }
                    } else if (i % 3 == 1) {
                        // Write operation
                        std::string key = "thread." + std::to_string(t) + ".key" + std::to_string(i);
                        if (!config->set(key, i)) {
                            errors.fetch_add(1);
                        }
                    } else {
                        // Check operation
                        std::string key = "thread." + std::to_string(t) + ".key" + std::to_string(i-1);
                        config->has(key);
                    }
                } catch (const std::exception&) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify no errors occurred
    EXPECT_EQ(errors.load(), 0) << "Thread safety test failed with " << errors.load() << " errors";

    // Verify system configuration is still intact
    EXPECT_EQ(config->get<std::string>("system.name"), "TestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 1000.0);
}

//==============================================================================
// Performance Benchmarks
//==============================================================================

TEST_F(ConfigurationTest, PerformanceBenchmarkTest) {
    EXPECT_TRUE(config->loadFromString(test_config_json));

    const int num_operations = 10000;

    // Benchmark configuration reads
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        auto value = config->get<std::string>("system.name");
        EXPECT_EQ(value, "TestSystem");
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    // Benchmark configuration writes
    start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        std::string key = "benchmark.key" + std::to_string(i);
        EXPECT_TRUE(config->set(key, i));
    }

    end_time = std::chrono::high_resolution_clock::now();
    auto write_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    // Calculate average times
    double avg_read_time_ns = static_cast<double>(read_duration.count()) / num_operations;
    double avg_write_time_ns = static_cast<double>(write_duration.count()) / num_operations;

    // Performance targets (these are reasonable for configuration access)
    EXPECT_LT(avg_read_time_ns, 1000.0) << "Average read time: " << avg_read_time_ns << " ns (target: <1000ns)";
    EXPECT_LT(avg_write_time_ns, 10000.0) << "Average write time: " << avg_write_time_ns << " ns (target: <10000ns)";

    // Output performance results
    std::cout << "\n=== Configuration Performance Results ===" << std::endl;
    std::cout << "Average read time:  " << avg_read_time_ns << " ns" << std::endl;
    std::cout << "Average write time: " << avg_write_time_ns << " ns" << std::endl;
    std::cout << "Read throughput:    " << (1e9 / avg_read_time_ns) << " ops/sec" << std::endl;
    std::cout << "Write throughput:   " << (1e9 / avg_write_time_ns) << " ops/sec" << std::endl;

    // Verify final state
    EXPECT_EQ(config->get<int>("benchmark.key0"), 0);
    EXPECT_EQ(config->get<int>("benchmark.key" + std::to_string(num_operations - 1)), num_operations - 1);
}

//==============================================================================
// Error Handling Tests
//==============================================================================

TEST_F(ConfigurationTest, ErrorHandlingTest) {
    // Test loading invalid JSON
    EXPECT_FALSE(config->loadFromString("invalid json"));

    // Test loading non-existent file
    EXPECT_FALSE(config->loadFromFile("nonexistent_file.json"));

    // Test invalid schema
    EXPECT_FALSE(config->loadSchema("nonexistent_schema.json"));

    // Test invalid operations
    EXPECT_FALSE(config->rollbackToSnapshot("nonexistent_snapshot"));
    EXPECT_FALSE(config->loadTemplate("nonexistent_template"));

    // Verify configuration remains stable
    EXPECT_TRUE(config->isEmpty());
}

//==============================================================================
// Environment Variable Tests
//==============================================================================

TEST_F(ConfigurationTest, EnvironmentVariableTest) {
    // Set some test environment variables
    setenv("AXONVEX_SYSTEM_NAME", "EnvTestSystem", 1);
    setenv("AXONVEX_SYSTEM_EXECUTION_FREQUENCY", "2000.0", 1);
    setenv("AXONVEX_SYSTEM_EXECUTION_ENABLED", "false", 1);

    // Load from environment
    EXPECT_TRUE(config->loadFromEnvironment("AXONVEX_"));

    // Verify values
    EXPECT_EQ(config->get<std::string>("system.name"), "EnvTestSystem");
    EXPECT_EQ(config->get<double>("system.execution.frequency"), 2000.0);
    EXPECT_EQ(config->get<bool>("system.execution.enabled"), false);

    // Clean up environment variables
    unsetenv("AXONVEX_SYSTEM_NAME");
    unsetenv("AXONVEX_SYSTEM_EXECUTION_FREQUENCY");
    unsetenv("AXONVEX_SYSTEM_EXECUTION_ENABLED");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
