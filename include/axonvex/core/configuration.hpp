#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace axonvex::core {

// Forward declarations
class ConfigurationValidator;
class ConfigurationMonitor;
class TemplateManager;

/**
 * @brief Configuration value variant type supporting common types
 */
using ConfigValue = nlohmann::json;

/**
 * @brief Configuration update callback function type
 */
using ConfigurationCallback = std::function<void(
    const std::string& key, const ConfigValue& oldValue, const ConfigValue& newValue)>;

/**
 * @brief Validation error information
 */
struct ValidationError {
    std::string key;
    std::string message;
    std::string expectedType;
    std::string actualType;

    ValidationError(const std::string& k, const std::string& msg, const std::string& expected = "",
                    const std::string& actual = "")
        : key(k), message(msg), expectedType(expected), actualType(actual) {}
};

/**
 * @brief Configuration statistics for monitoring
 */
struct ConfigurationStatistics {
    std::atomic<uint64_t> total_loads{0};
    std::atomic<uint64_t> total_saves{0};
    std::atomic<uint64_t> total_updates{0};
    std::atomic<uint64_t> validation_failures{0};
    std::atomic<uint64_t> runtime_updates{0};
    std::atomic<uint64_t> callback_invocations{0};

    uint64_t getTotalLoads() const noexcept {
        return total_loads.load();
    }
    uint64_t getTotalSaves() const noexcept {
        return total_saves.load();
    }
    uint64_t getTotalUpdates() const noexcept {
        return total_updates.load();
    }
    uint64_t getValidationFailures() const noexcept {
        return validation_failures.load();
    }
    uint64_t getRuntimeUpdates() const noexcept {
        return runtime_updates.load();
    }
    uint64_t getCallbackInvocations() const noexcept {
        return callback_invocations.load();
    }

    void reset() noexcept {
        total_loads.store(0);
        total_saves.store(0);
        total_updates.store(0);
        validation_failures.store(0);
        runtime_updates.store(0);
        callback_invocations.store(0);
    }
};

/**
 * @brief Configuration schema for validation
 */
struct ConfigurationSchema {
    nlohmann::json schema;
    std::string version;
    std::string description;

    ConfigurationSchema() = default;
    ConfigurationSchema(const nlohmann::json& s, const std::string& v = "1.0",
                        const std::string& desc = "")
        : schema(s), version(v), description(desc) {}
};

/**
 * @brief Configuration template for presets
 */
struct ConfigurationTemplate {
    std::string name;
    std::string description;
    nlohmann::json config;
    std::vector<std::string> tags;

    ConfigurationTemplate() = default;
    ConfigurationTemplate(const std::string& n, const nlohmann::json& c,
                          const std::string& desc = "")
        : name(n), description(desc), config(c) {}
};

/**
 * @brief High-performance configuration management system for AxonVex Framework
 *
 * Features:
 * - JSON/YAML parsing with schema validation
 * - Runtime configuration updates with change notifications
 * - Configuration templates and presets
 * - Environment variable integration
 * - Hierarchical configuration merging
 * - Thread-safe operations with minimal locking
 * - Comprehensive validation and error reporting
 * - Configuration versioning and rollback
 * - File watching for automatic reloading
 *
 * Performance characteristics:
 * - Configuration lookup: O(1) average case
 * - Update operations: Thread-safe with minimal contention
 * - Memory usage: Efficient JSON storage with copy-on-write
 * - Validation: Fast schema validation with caching
 * - File operations: Asynchronous I/O with batching
 *
 * @example Basic usage:
 * @code
 * Configuration config;
 * config.loadFromFile("system.json");
 * config.setSchema(schema);
 *
 * auto frequency = config.get<double>("execution.frequency", 1000.0);
 * config.set("logging.level", "info");
 *
 * config.registerCallback("execution.*", [](const auto& key, const auto& old, const auto& new_val)
 * { Log::Info() << "Execution config changed: " << key;
 * });
 * @endcode
 */
class Configuration {
  public:
    static constexpr size_t DEFAULT_MAX_DEPTH = 32;
    static constexpr size_t DEFAULT_MAX_ARRAY_SIZE = 10000;

    /**
     * @brief Construct a new Configuration object
     *
     * @param enable_monitoring Enable configuration change monitoring
     * @param enable_validation Enable automatic validation
     */
    explicit Configuration(bool enable_monitoring = true, bool enable_validation = true);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~Configuration();

    // Non-copyable, but moveable for performance
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
    Configuration(Configuration&&) noexcept = default;
    Configuration& operator=(Configuration&&) noexcept = default;

    //==========================================================================
    // Configuration Loading and Saving
    //==========================================================================

    /**
     * @brief Load configuration from JSON file
     *
     * @param filename Path to JSON configuration file
     * @param merge_with_existing If true, merge with current config
     * @return true if loaded successfully
     */
    bool loadFromFile(const std::string& filename, bool merge_with_existing = false);

    /**
     * @brief Load configuration from JSON string
     *
     * @param json_string JSON configuration as string
     * @param merge_with_existing If true, merge with current config
     * @return true if loaded successfully
     */
    bool loadFromString(const std::string& json_string, bool merge_with_existing = false);

    /**
     * @brief Load configuration from environment variables
     *
     * @param prefix Environment variable prefix (e.g., "AXONVEX_")
     * @param merge_with_existing If true, merge with current config
     * @return true if loaded successfully
     */
    bool loadFromEnvironment(const std::string& prefix = "AXONVEX_",
                             bool merge_with_existing = true);

    /**
     * @brief Save configuration to JSON file
     *
     * @param filename Path to save configuration
     * @param pretty_print If true, format JSON for readability
     * @return true if saved successfully
     */
    bool saveToFile(const std::string& filename, bool pretty_print = true) const;

    /**
     * @brief Get configuration as JSON string
     *
     * @param pretty_print If true, format JSON for readability
     * @return JSON string representation
     */
    std::string toString(bool pretty_print = true) const;

    //==========================================================================
    // Configuration Access
    //==========================================================================

    /**
     * @brief Get configuration value with type safety
     *
     * @tparam T Type to convert value to
     * @param key Configuration key (supports dot notation)
     * @param default_value Default value if key not found
     * @return Configuration value or default
     */
    template <typename T>
    T get(const std::string& key, const T& default_value = T{}) const;

    /**
     * @brief Get optional configuration value
     *
     * @tparam T Type to convert value to
     * @param key Configuration key
     * @return Optional value (empty if not found)
     */
    template <typename T>
    std::optional<T> getOptional(const std::string& key) const;

    /**
     * @brief Set configuration value
     *
     * @tparam T Type of value to set
     * @param key Configuration key (supports dot notation)
     * @param value Value to set
     * @param validate If true, validate against schema
     * @return true if set successfully
     */
    template <typename T>
    bool set(const std::string& key, const T& value, bool validate = true);

    /**
     * @brief Check if configuration key exists
     *
     * @param key Configuration key
     * @return true if key exists
     */
    bool has(const std::string& key) const;

    /**
     * @brief Remove configuration key
     *
     * @param key Configuration key
     * @return true if removed successfully
     */
    bool remove(const std::string& key);

    /**
     * @brief Get all keys matching a pattern
     *
     * @param pattern Key pattern (supports wildcards)
     * @return Vector of matching keys
     */
    std::vector<std::string> getKeys(const std::string& pattern = "*") const;

    //==========================================================================
    // Schema Validation
    //==========================================================================

    /**
     * @brief Set configuration schema for validation
     *
     * @param schema JSON schema object
     * @return true if schema set successfully
     */
    bool setSchema(const ConfigurationSchema& schema);

    /**
     * @brief Load schema from file
     *
     * @param schema_file Path to JSON schema file
     * @return true if loaded successfully
     */
    bool loadSchema(const std::string& schema_file);

    /**
     * @brief Validate current configuration against schema
     *
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<ValidationError> validate() const;

    /**
     * @brief Validate a specific key against schema
     *
     * @param key Configuration key
     * @param value Value to validate
     * @return Vector of validation errors
     */
    std::vector<ValidationError> validateKey(const std::string& key,
                                             const ConfigValue& value) const;

    /**
     * @brief Enable or disable automatic validation
     *
     * @param enable If true, validate all changes automatically
     */
    void enableValidation(bool enable);

    /**
     * @brief Check if validation is enabled
     *
     * @return true if validation is enabled
     */
    bool isValidationEnabled() const noexcept;

    //==========================================================================
    // Runtime Updates and Callbacks
    //==========================================================================

    /**
     * @brief Register callback for configuration changes
     *
     * @param key_pattern Key pattern to watch (supports wildcards)
     * @param callback Function to call on changes
     * @return Callback ID for unregistering
     */
    size_t registerCallback(const std::string& key_pattern, ConfigurationCallback callback);

    /**
     * @brief Unregister configuration change callback
     *
     * @param callback_id ID returned from registerCallback
     */
    void unregisterCallback(size_t callback_id);

    /**
     * @brief Clear all callbacks
     */
    void clearCallbacks();

    /**
     * @brief Apply configuration update
     *
     * @param updates JSON object with updates to apply
     * @param validate If true, validate updates before applying
     * @return true if updates applied successfully
     */
    bool applyUpdates(const nlohmann::json& updates, bool validate = true);

    /**
     * @brief Enable file watching for automatic reloading
     *
     * @param enable If true, watch config file for changes
     */
    void enableFileWatching(bool enable);

    //==========================================================================
    // Configuration Templates
    //==========================================================================

    /**
     * @brief Load configuration template
     *
     * @param template_name Name of template to load
     * @return true if loaded successfully
     */
    bool loadTemplate(const std::string& template_name);

    /**
     * @brief Save current configuration as template
     *
     * @param template_name Name for the template
     * @param description Optional description
     * @return true if saved successfully
     */
    bool saveTemplate(const std::string& template_name, const std::string& description = "");

    /**
     * @brief Get available template names
     *
     * @return Vector of available template names
     */
    std::vector<std::string> getAvailableTemplates() const;

    /**
     * @brief Get template information
     *
     * @param template_name Name of template
     * @return Optional template object
     */
    std::optional<ConfigurationTemplate> getTemplate(const std::string& template_name) const;

    //==========================================================================
    // Versioning and Rollback
    //==========================================================================

    /**
     * @brief Create configuration snapshot
     *
     * @param name Optional name for snapshot
     * @return Snapshot ID
     */
    std::string createSnapshot(const std::string& name = "");

    /**
     * @brief Rollback to configuration snapshot
     *
     * @param snapshot_id ID of snapshot to rollback to
     * @return true if rollback successful
     */
    bool rollbackToSnapshot(const std::string& snapshot_id);

    /**
     * @brief Get available snapshots
     *
     * @return Vector of snapshot IDs
     */
    std::vector<std::string> getAvailableSnapshots() const;

    /**
     * @brief Remove configuration snapshot
     *
     * @param snapshot_id ID of snapshot to remove
     */
    void removeSnapshot(const std::string& snapshot_id);

    //==========================================================================
    // Status and Statistics
    //==========================================================================

    /**
     * @brief Get configuration statistics
     *
     * @return Reference to statistics object
     */
    const ConfigurationStatistics& getStatistics() const noexcept;

    /**
     * @brief Reset configuration statistics
     */
    void resetStatistics() noexcept;

    /**
     * @brief Get configuration size in bytes
     *
     * @return Approximate memory usage
     */
    size_t getMemoryUsage() const;

    /**
     * @brief Get number of configuration keys
     *
     * @return Total number of keys
     */
    size_t getKeyCount() const;

    /**
     * @brief Check if configuration is empty
     *
     * @return true if no configuration loaded
     */
    bool isEmpty() const noexcept;

    /**
     * @brief Clear all configuration data
     */
    void clear();

    /**
     * @brief Get configuration performance metrics
     *
     * @return Performance metrics string
     */
    std::string getPerformanceMetrics() const;

  private:
    // Core configuration data
    nlohmann::json config_data_;
    mutable std::shared_mutex config_mutex_;

    // Schema and validation
    std::unique_ptr<ConfigurationSchema> schema_;
    std::atomic<bool> validation_enabled_;

    // Callbacks and monitoring
    std::unordered_map<size_t, std::pair<std::string, ConfigurationCallback>> callbacks_;
    std::mutex callbacks_mutex_;
    std::atomic<size_t> next_callback_id_;
    std::atomic<bool> monitoring_enabled_;

    // Templates and snapshots
    std::unordered_map<std::string, ConfigurationTemplate> templates_;
    std::unordered_map<std::string, nlohmann::json> snapshots_;
    std::mutex templates_mutex_;
    std::mutex snapshots_mutex_;

    // File watching
    std::atomic<bool> file_watching_enabled_;
    std::string watched_file_;
    std::filesystem::file_time_type last_write_time_;

    // Statistics
    mutable ConfigurationStatistics stats_;

    // Memory ordering constants
    static constexpr std::memory_order relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order acquire = std::memory_order_acquire;
    static constexpr std::memory_order release = std::memory_order_release;

    // Helper methods
    bool loadFromJson(const nlohmann::json& json_data, bool merge_with_existing);
    std::vector<ValidationError> validateInternal(const nlohmann::json& data) const;
    void notifyCallbacks(const std::string& key, const ConfigValue& old_value,
                         const ConfigValue& new_value);
    std::vector<std::string> expandKeyPattern(const std::string& pattern) const;
    bool matchesPattern(const std::string& key, const std::string& pattern) const;
    std::string generateSnapshotId() const;
    nlohmann::json* getJsonPointer(const std::string& key, bool create_if_missing = false);
    const nlohmann::json* getJsonPointer(const std::string& key) const;
    std::vector<std::string> splitKey(const std::string& key) const;
    void updateFileWatcher();
};

//==============================================================================
// Template Implementations
//==============================================================================

template <typename T>
inline T Configuration::get(const std::string& key, const T& default_value) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);

    try {
        const auto* json_ptr = getJsonPointer(key);
        if (json_ptr == nullptr) {
            return default_value;
        }

        if constexpr (std::is_same_v<T, std::string>) {
            if (json_ptr->is_string()) {
                return json_ptr->get<T>();
            } else if (json_ptr->is_number() || json_ptr->is_boolean()) {
                return std::to_string(json_ptr->get<double>());
            }
        } else {
            return json_ptr->get<T>();
        }

        return default_value;
    } catch (const std::exception&) { return default_value; }
}

template <typename T>
inline std::optional<T> Configuration::getOptional(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);

    try {
        const auto* json_ptr = getJsonPointer(key);
        if (json_ptr == nullptr) {
            return std::nullopt;
        }

        return json_ptr->get<T>();
    } catch (const std::exception&) { return std::nullopt; }
}

template <typename T>
inline bool Configuration::set(const std::string& key, const T& value, bool validate) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);

    try {
        // Get old value for callback
        ConfigValue old_value;
        const auto* old_json_ptr = getJsonPointer(key);
        if (old_json_ptr != nullptr) {
            old_value = *old_json_ptr;
        }

        // Set new value
        auto* json_ptr = getJsonPointer(key, true);
        if (json_ptr == nullptr) {
            return false;
        }

        ConfigValue new_value = value;

        // Validate if enabled
        if (validate && validation_enabled_.load(relaxed)) {
            auto errors = validateKey(key, new_value);
            if (!errors.empty()) {
                stats_.validation_failures.fetch_add(1, relaxed);
                return false;
            }
        }

        *json_ptr = new_value;

        // Update statistics
        stats_.total_updates.fetch_add(1, relaxed);
        if (monitoring_enabled_.load(relaxed)) {
            stats_.runtime_updates.fetch_add(1, relaxed);
        }

        // Release lock before calling callbacks
        lock.unlock();

        // Notify callbacks
        if (monitoring_enabled_.load(relaxed)) {
            notifyCallbacks(key, old_value, new_value);
        }

        return true;
    } catch (const std::exception&) { return false; }
}

} // namespace axonvex::core
