/**
 * @file configuration.cpp
 * @brief Configuration Management Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * This file implements the Configuration class for comprehensive configuration
 * management with JSON/YAML parsing, schema validation, and runtime updates.
 */

#include <algorithm>
#include <axonvex_core/configuration.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

extern char** environ; // Environment variables declaration

namespace axonvex::core {

Configuration::Configuration(bool enable_monitoring, bool enable_validation)
    : validation_enabled_(enable_validation), next_callback_id_(1),
      monitoring_enabled_(enable_monitoring), file_watching_enabled_(false) {}

Configuration::~Configuration() {
    clearCallbacks();
}

//==============================================================================
// Configuration Loading and Saving
//==============================================================================

bool Configuration::loadFromFile(const std::string& filename, bool merge_with_existing) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json_data;
        file >> json_data;

        if (loadFromJson(json_data, merge_with_existing)) {
            watched_file_ = filename;
            if (std::filesystem::exists(filename)) {
                last_write_time_ = std::filesystem::last_write_time(filename);
            }
            stats_.total_loads.fetch_add(1, relaxed);
            return true;
        }

        return false;
    } catch (const std::exception&) { return false; }
}

bool Configuration::loadFromString(const std::string& json_string, bool merge_with_existing) {
    try {
        nlohmann::json json_data = nlohmann::json::parse(json_string);

        if (loadFromJson(json_data, merge_with_existing)) {
            stats_.total_loads.fetch_add(1, relaxed);
            return true;
        }

        return false;
    } catch (const std::exception&) { return false; }
}

bool Configuration::loadFromEnvironment(const std::string& prefix, bool merge_with_existing) {
    try {
        nlohmann::json env_config;
        auto setNested = [](nlohmann::json& root, const std::string& dotted_key,
                            const nlohmann::json& value) {
            nlohmann::json* current = &root;
            size_t start = 0;

            while (start < dotted_key.size()) {
                size_t dot = dotted_key.find('.', start);
                std::string part =
                    dot == std::string::npos ? dotted_key.substr(start)
                                             : dotted_key.substr(start, dot - start);

                if (dot == std::string::npos) {
                    (*current)[part] = value;
                    return;
                }

                current = &(*current)[part];
                start = dot + 1;
            }
        };

        // Get all environment variables with the specified prefix
        for (char** env = environ; *env != nullptr; ++env) {
            std::string env_var(*env);
            size_t equals_pos = env_var.find('=');
            if (equals_pos == std::string::npos)
                continue;

            std::string key = env_var.substr(0, equals_pos);
            std::string value = env_var.substr(equals_pos + 1);

            if (key.substr(0, prefix.length()) == prefix) {
                // Remove prefix and convert to lowercase
                key = key.substr(prefix.length());
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);

                // Replace underscores with dots for hierarchical keys
                std::replace(key.begin(), key.end(), '_', '.');

                nlohmann::json parsed_value;
                try {
                    parsed_value = nlohmann::json::parse(value);
                } catch (...) {
                    // Treat as string if not valid JSON.
                    parsed_value = value;
                }

                // Build nested object shape so dotted keys are queryable via get("a.b").
                setNested(env_config, key, parsed_value);
            }
        }

        if (loadFromJson(env_config, merge_with_existing)) {
            stats_.total_loads.fetch_add(1, relaxed);
            return true;
        }

        return false;
    } catch (const std::exception&) { return false; }
}

bool Configuration::saveToFile(const std::string& filename, bool pretty_print) const {
    try {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);

        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        if (pretty_print) {
            file << config_data_.dump(4);
        } else {
            file << config_data_.dump();
        }

        stats_.total_saves.fetch_add(1, relaxed);
        return true;
    } catch (const std::exception&) { return false; }
}

std::string Configuration::toString(bool pretty_print) const {
    try {
        std::shared_lock<std::shared_mutex> lock(config_mutex_);

        if (pretty_print) {
            return config_data_.dump(4);
        } else {
            return config_data_.dump();
        }
    } catch (const std::exception&) { return "{}"; }
}

//==============================================================================
// Configuration Access
//==============================================================================

bool Configuration::has(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    const auto* json_ptr = getJsonPointer(key);
    return json_ptr != nullptr;
}

bool Configuration::remove(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);

    try {
        auto keys = splitKey(key);
        if (keys.empty())
            return false;

        nlohmann::json* current = &config_data_;

        // Navigate to parent of target key
        for (size_t i = 0; i < keys.size() - 1; ++i) {
            if (!current->contains(keys[i])) {
                return false;
            }
            current = &(*current)[keys[i]];
        }

        // Remove the final key
        if (current->contains(keys.back())) {
            current->erase(keys.back());
            stats_.total_updates.fetch_add(1, relaxed);
            return true;
        }

        return false;
    } catch (const std::exception&) { return false; }
}

std::vector<std::string> Configuration::getKeys(const std::string& pattern) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);

    std::vector<std::string> result;
    std::function<void(const nlohmann::json&, const std::string&)> traverse =
        [&](const nlohmann::json& obj, const std::string& prefix) {
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                std::string full_key = prefix.empty() ? it.key() : prefix + "." + it.key();

                if (matchesPattern(full_key, pattern)) {
                    result.push_back(full_key);
                }

                if (it->is_object()) {
                    traverse(*it, full_key);
                }
            }
        };

    traverse(config_data_, "");
    return result;
}

//==============================================================================
// Schema Validation
//==============================================================================

bool Configuration::setSchema(const ConfigurationSchema& schema) {
    try {
        schema_ = std::make_unique<ConfigurationSchema>(schema);

        // Validate current configuration against new schema
        if (validation_enabled_.load(relaxed)) {
            auto errors = validate();
            if (!errors.empty()) {
                // Schema validation failed - don't apply schema
                schema_.reset();
                stats_.validation_failures.fetch_add(1, relaxed);
                return false;
            }
        }

        return true;
    } catch (const std::exception&) { return false; }
}

bool Configuration::loadSchema(const std::string& schema_file) {
    try {
        std::ifstream file(schema_file);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json schema_json;
        file >> schema_json;

        ConfigurationSchema schema(schema_json, "1.0", "Loaded from " + schema_file);
        return setSchema(schema);
    } catch (const std::exception&) { return false; }
}

std::vector<ValidationError> Configuration::validate() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return validateInternal(config_data_);
}

std::vector<ValidationError> Configuration::validateKey(const std::string& key,
                                                        const ConfigValue& value) const {
    // For now, implement basic validation
    // In a full implementation, this would use a JSON schema validator
    std::vector<ValidationError> errors;

    if (!schema_) {
        return errors; // No schema to validate against
    }

    // Basic type validation example
    if (key.find("frequency") != std::string::npos && !value.is_number()) {
        errors.emplace_back(key, "Frequency values must be numeric", "number", value.type_name());
    }

    if (key.find("enabled") != std::string::npos && !value.is_boolean()) {
        errors.emplace_back(key, "Enabled flags must be boolean", "boolean", value.type_name());
    }

    return errors;
}

void Configuration::enableValidation(bool enable) {
    validation_enabled_.store(enable, relaxed);
}

bool Configuration::isValidationEnabled() const noexcept {
    return validation_enabled_.load(acquire);
}

//==============================================================================
// Runtime Updates and Callbacks
//==============================================================================

size_t Configuration::registerCallback(const std::string& key_pattern,
                                       ConfigurationCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    size_t callback_id = next_callback_id_.fetch_add(1, relaxed);
    callbacks_[callback_id] = std::make_pair(key_pattern, std::move(callback));

    return callback_id;
}

void Configuration::unregisterCallback(size_t callback_id) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.erase(callback_id);
}

void Configuration::clearCallbacks() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.clear();
}

bool Configuration::applyUpdates(const nlohmann::json& updates, bool validate) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);

    try {
        // Validate updates if requested
        if (validate && validation_enabled_.load(relaxed)) {
            auto errors = validateInternal(updates);
            if (!errors.empty()) {
                stats_.validation_failures.fetch_add(1, relaxed);
                return false;
            }
        }

        // Apply updates recursively
        std::function<void(nlohmann::json&, const nlohmann::json&, const std::string&)> merge =
            [&](nlohmann::json& target, const nlohmann::json& source, const std::string& prefix) {
                for (auto it = source.begin(); it != source.end(); ++it) {
                    std::string full_key = prefix.empty() ? it.key() : prefix + "." + it.key();

                    ConfigValue old_value;
                    if (target.contains(it.key())) {
                        old_value = target[it.key()];
                    }

                    if (it->is_object() && target.contains(it.key()) &&
                        target[it.key()].is_object()) {
                        merge(target[it.key()], *it, full_key);
                    } else {
                        target[it.key()] = *it;

                        // Notify callbacks for this key
                        if (monitoring_enabled_.load(relaxed)) {
                            lock.unlock();
                            notifyCallbacks(full_key, old_value, *it);
                            lock.lock();
                        }
                    }
                }
            };

        merge(config_data_, updates, "");
        stats_.total_updates.fetch_add(1, relaxed);
        stats_.runtime_updates.fetch_add(1, relaxed);

        return true;
    } catch (const std::exception&) { return false; }
}

void Configuration::enableFileWatching(bool enable) {
    file_watching_enabled_.store(enable, relaxed);
    if (enable && !watched_file_.empty()) {
        updateFileWatcher();
    }
}

//==============================================================================
// Configuration Templates
//==============================================================================

bool Configuration::loadTemplate(const std::string& template_name) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(templates_mutex_));

    auto it = templates_.find(template_name);
    if (it == templates_.end()) {
        return false;
    }

    return loadFromJson(it->second.config, false);
}

bool Configuration::saveTemplate(const std::string& template_name, const std::string& description) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(templates_mutex_));
    std::shared_lock<std::shared_mutex> config_lock(config_mutex_);

    ConfigurationTemplate template_obj(template_name, config_data_, description);
    templates_[template_name] = template_obj;

    return true;
}

std::vector<std::string> Configuration::getAvailableTemplates() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(templates_mutex_));

    std::vector<std::string> result;
    for (const auto& pair : templates_) {
        result.push_back(pair.first);
    }
    return result;
}

std::optional<ConfigurationTemplate> Configuration::getTemplate(
    const std::string& template_name) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(templates_mutex_));

    auto it = templates_.find(template_name);
    if (it != templates_.end()) {
        return it->second;
    }
    return std::nullopt;
}

//==============================================================================
// Versioning and Rollback
//==============================================================================

std::string Configuration::createSnapshot(const std::string& name) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(snapshots_mutex_));
    std::shared_lock<std::shared_mutex> config_lock(config_mutex_);

    std::string snapshot_id = name.empty() ? generateSnapshotId() : name;
    snapshots_[snapshot_id] = config_data_;

    return snapshot_id;
}

bool Configuration::rollbackToSnapshot(const std::string& snapshot_id) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(snapshots_mutex_));

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return false;
    }

    return loadFromJson(it->second, false);
}

std::vector<std::string> Configuration::getAvailableSnapshots() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(snapshots_mutex_));

    std::vector<std::string> result;
    for (const auto& pair : snapshots_) {
        result.push_back(pair.first);
    }
    return result;
}

void Configuration::removeSnapshot(const std::string& snapshot_id) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(snapshots_mutex_));
    snapshots_.erase(snapshot_id);
}

//==============================================================================
// Status and Statistics
//==============================================================================

const ConfigurationStatistics& Configuration::getStatistics() const noexcept {
    return stats_;
}

void Configuration::resetStatistics() noexcept {
    stats_.reset();
}

size_t Configuration::getMemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);

    // Approximate memory usage calculation
    std::string json_str = config_data_.dump();
    return json_str.size() + sizeof(Configuration);
}

size_t Configuration::getKeyCount() const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);

    size_t count = 0;
    std::function<void(const nlohmann::json&)> traverse = [&](const nlohmann::json& obj) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            count++;
            if (it->is_object()) {
                traverse(*it);
            }
        }
    };

    traverse(config_data_);
    return count;
}

bool Configuration::isEmpty() const noexcept {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return config_data_.empty();
}

void Configuration::clear() {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_.clear();
    stats_.total_updates.fetch_add(1, relaxed);
}

std::string Configuration::getPerformanceMetrics() const {
    const auto& stats = getStatistics();

    std::stringstream ss;
    ss << "Configuration Performance Metrics:\n";
    ss << "  Total loads: " << stats.getTotalLoads() << "\n";
    ss << "  Total saves: " << stats.getTotalSaves() << "\n";
    ss << "  Total updates: " << stats.getTotalUpdates() << "\n";
    ss << "  Runtime updates: " << stats.getRuntimeUpdates() << "\n";
    ss << "  Validation failures: " << stats.getValidationFailures() << "\n";
    ss << "  Callback invocations: " << stats.getCallbackInvocations() << "\n";
    ss << "  Memory usage: " << getMemoryUsage() << " bytes\n";
    ss << "  Key count: " << getKeyCount() << "\n";

    return ss.str();
}

//==============================================================================
// Helper Methods
//==============================================================================

bool Configuration::loadFromJson(const nlohmann::json& json_data, bool merge_with_existing) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);

    try {
        if (validation_enabled_.load(relaxed)) {
            auto errors = validateInternal(json_data);
            if (!errors.empty()) {
                stats_.validation_failures.fetch_add(1, relaxed);
                return false;
            }
        }

        if (merge_with_existing) {
            config_data_.merge_patch(json_data);
        } else {
            config_data_ = json_data;
        }

        return true;
    } catch (const std::exception&) { return false; }
}

std::vector<ValidationError> Configuration::validateInternal(const nlohmann::json& data) const {
    // Basic validation - in a full implementation, this would use JSON schema validation
    std::vector<ValidationError> errors;

    if (!schema_) {
        return errors; // No schema to validate against
    }

    // For now, just check that it's valid JSON
    if (!data.is_object() && !data.is_array() && !data.is_primitive()) {
        errors.emplace_back("root", "Invalid JSON data", "object/array/primitive", "unknown");
    }

    return errors;
}

void Configuration::notifyCallbacks(const std::string& key, const ConfigValue& old_value,
                                    const ConfigValue& new_value) {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(callbacks_mutex_));

    for (const auto& pair : callbacks_) {
        const std::string& pattern = pair.second.first;
        const ConfigurationCallback& callback = pair.second.second;

        if (matchesPattern(key, pattern)) {
            try {
                callback(key, old_value, new_value);
                stats_.callback_invocations.fetch_add(1, relaxed);
            } catch (const std::exception&) {
                // Ignore callback exceptions
            }
        }
    }
}

std::vector<std::string> Configuration::expandKeyPattern(const std::string& pattern) const {
    // Simple pattern expansion - in a full implementation, this would support complex patterns
    return {pattern};
}

bool Configuration::matchesPattern(const std::string& key, const std::string& pattern) const {
    if (pattern == "*") {
        return true;
    }

    if (pattern.find('*') != std::string::npos) {
        // Convert glob pattern to regex
        std::string regex_pattern = pattern;
        std::replace(regex_pattern.begin(), regex_pattern.end(), '*', '.');
        regex_pattern = ".*" + regex_pattern + ".*";

        try {
            std::regex regex(regex_pattern);
            return std::regex_match(key, regex);
        } catch (const std::exception&) { return false; }
    }

    return key == pattern;
}

std::string Configuration::generateSnapshotId() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "snapshot_" << time_t;
    return ss.str();
}

nlohmann::json* Configuration::getJsonPointer(const std::string& key, bool create_if_missing) {
    auto keys = splitKey(key);
    if (keys.empty())
        return nullptr;

    nlohmann::json* current = &config_data_;

    for (const auto& k : keys) {
        if (!current->contains(k)) {
            if (create_if_missing) {
                (*current)[k] = nlohmann::json::object();
            } else {
                return nullptr;
            }
        }
        current = &(*current)[k];
    }

    return current;
}

const nlohmann::json* Configuration::getJsonPointer(const std::string& key) const {
    auto keys = splitKey(key);
    if (keys.empty())
        return nullptr;

    const nlohmann::json* current = &config_data_;

    for (const auto& k : keys) {
        if (!current->contains(k)) {
            return nullptr;
        }
        current = &(*current)[k];
    }

    return current;
}

std::vector<std::string> Configuration::splitKey(const std::string& key) const {
    std::vector<std::string> result;
    std::stringstream ss(key);
    std::string part;

    while (std::getline(ss, part, '.')) {
        if (!part.empty()) {
            result.push_back(part);
        }
    }

    return result;
}

void Configuration::updateFileWatcher() {
    if (!file_watching_enabled_.load(relaxed) || watched_file_.empty()) {
        return;
    }

    try {
        if (std::filesystem::exists(watched_file_)) {
            auto current_write_time = std::filesystem::last_write_time(watched_file_);
            if (current_write_time != last_write_time_) {
                // File has been modified - reload it
                loadFromFile(watched_file_, false);
                last_write_time_ = current_write_time;
            }
        }
    } catch (const std::exception&) {
        // File watching failed - disable it
        file_watching_enabled_.store(false, relaxed);
    }
}

} // namespace axonvex::core
