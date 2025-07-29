#pragma once

#include <axonvex/core/callback.hpp>
#include <axonvex/core/caller.hpp>
#include <axonvex/core/callerKeyed.hpp>
#include <axonvex/core/errorHandler.hpp>
#include <axonvex/core/unifiedStatistics.hpp>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

namespace axonvex::core {

/**
 * @brief Unified callback and error management system
 * Consolidates all callback registration patterns and error handling across the codebase
 */
class UnifiedCallbackSystem {
public:
    // Unified callback types
    template<typename T>
    using UnifiedCallback = std::function<void(const T&)>;
    
    template<typename T>
    using SafeCallback = std::function<bool(const T&)>; // Returns false on error
    
    using ErrorCallback = std::function<void(const ErrorInfo&)>;
    using RecoveryCallback = std::function<bool(const ErrorInfo&)>;
    using ValidationCallback = std::function<bool(const std::string&)>;

    /**
     * @brief Constructor with component identification
     */
    explicit UnifiedCallbackSystem(const std::string& component_name = "UnifiedSystem")
        : component_name_(component_name), 
          error_handler_(component_name),
          stats_(std::make_unique<UnifiedStatistics>()) {
        stats_->setComponentName(component_name + "_Callbacks");
    }

    // =================================================================
    // UNIFIED CALLBACK REGISTRATION
    // =================================================================

    /**
     * @brief Register a simple callback for a data type
     */
    template<typename T>
    uint32_t registerCallback(UnifiedCallback<T> callback, const std::string& category = "default") {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        uint32_t id = next_callback_id_.fetch_add(1);
        
        auto& category_map = typed_callbacks_[typeid(T).name()];
        auto& callback_list = category_map[category];
        callback_list.emplace_back(id, [callback](const void* data) {
            callback(*static_cast<const T*>(data));
        });
        
        callback_ids_[id] = {typeid(T).name(), category};
        stats_->recordSuccessfulOperation();
        return id;
    }

    /**
     * @brief Register a keyed callback (replaces CallerKeyed functionality)
     */
    template<typename KeyType, typename DataType>
    uint32_t registerKeyedCallback(const KeyType& key, UnifiedCallback<DataType> callback) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        uint32_t id = next_callback_id_.fetch_add(1);
        
        std::string key_str = keyToString(key);
        std::string type_name = std::string(typeid(DataType).name()) + "_keyed";
        
        auto& category_map = typed_callbacks_[type_name];
        auto& callback_list = category_map[key_str];
        callback_list.emplace_back(id, [callback](const void* data) {
            callback(*static_cast<const DataType*>(data));
        });
        
        callback_ids_[id] = {type_name, key_str};
        stats_->recordSuccessfulOperation();
        return id;
    }

    /**
     * @brief Register safe callback that can report errors
     */
    template<typename T>
    uint32_t registerSafeCallback(SafeCallback<T> callback, const std::string& category = "default") {
        return registerCallback<T>([this, callback](const T& data) {
            try {
                if (!callback(data)) {
                    error_handler_.reportError(ErrorSeverity::WARNING, 
                                             "Safe callback returned false", 
                                             ErrorCategory::RUNTIME);
                    stats_->recordFailedOperation();
                }
            } catch (const std::exception& e) {
                error_handler_.reportError(ErrorSeverity::ERROR, 
                                         "Safe callback threw exception: " + std::string(e.what()), 
                                         ErrorCategory::RUNTIME);
                stats_->recordFailedOperation();
            }
        }, category);
    }

    /**
     * @brief Unregister callback by ID
     */
    bool unregisterCallback(uint32_t callback_id) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        
        auto it = callback_ids_.find(callback_id);
        if (it == callback_ids_.end()) {
            return false;
        }
        
        const auto& [type_name, category] = it->second;
        auto& category_map = typed_callbacks_[type_name];
        auto& callback_list = category_map[category];
        
        callback_list.erase(
            std::remove_if(callback_list.begin(), callback_list.end(),
                          [callback_id](const auto& pair) { return pair.first == callback_id; }),
            callback_list.end());
        
        callback_ids_.erase(it);
        return true;
    }

    // =================================================================
    // UNIFIED CALLBACK INVOCATION
    // =================================================================

    /**
     * @brief Call all callbacks for a data type and category
     */
    template<typename T>
    size_t callCallbacks(const T& data, const std::string& category = "default", bool safe_mode = true) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        
        size_t called = 0;
        size_t exceptions = 0;
        
        auto type_it = typed_callbacks_.find(typeid(T).name());
        if (type_it != typed_callbacks_.end()) {
            auto cat_it = type_it->second.find(category);
            if (cat_it != type_it->second.end()) {
                for (const auto& [id, callback] : cat_it->second) {
                    try {
                        callback(&data);
                        called++;
                        stats_->recordSuccessfulOperation();
                    } catch (const std::exception& e) {
                        exceptions++;
                        stats_->recordFailedOperation();
                        
                        if (safe_mode) {
                            error_handler_.reportError(ErrorSeverity::WARNING,
                                                     "Callback exception: " + std::string(e.what()),
                                                     ErrorCategory::RUNTIME);
                            continue; // Continue with other callbacks
                        } else {
                            throw; // Re-throw in unsafe mode
                        }
                    }
                }
            }
        }
        
        return called;
    }

    /**
     * @brief Call keyed callbacks (replaces CallerKeyed::callCallbacksByKey)
     */
    template<typename KeyType, typename DataType>
    size_t callKeyedCallbacks(const KeyType& key, const DataType& data, bool safe_mode = true) {
        std::string key_str = keyToString(key);
        std::string type_name = std::string(typeid(DataType).name()) + "_keyed";
        
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        
        size_t called = 0;
        auto type_it = typed_callbacks_.find(type_name);
        if (type_it != typed_callbacks_.end()) {
            auto cat_it = type_it->second.find(key_str);
            if (cat_it != type_it->second.end()) {
                for (const auto& [id, callback] : cat_it->second) {
                    try {
                        callback(&data);
                        called++;
                        stats_->recordSuccessfulOperation();
                    } catch (const std::exception& e) {
                        stats_->recordFailedOperation();
                        
                        if (safe_mode) {
                            error_handler_.reportError(ErrorSeverity::WARNING,
                                                     "Keyed callback exception: " + std::string(e.what()),
                                                     ErrorCategory::RUNTIME);
                            continue;
                        } else {
                            throw;
                        }
                    }
                }
            }
        }
        
        return called;
    }

    /**
     * @brief Call all callbacks regardless of category/key
     */
    template<typename T>
    size_t callAllCallbacks(const T& data, bool safe_mode = true) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        
        size_t total_called = 0;
        auto type_it = typed_callbacks_.find(typeid(T).name());
        if (type_it != typed_callbacks_.end()) {
            for (const auto& [category, callback_list] : type_it->second) {
                for (const auto& [id, callback] : callback_list) {
                    try {
                        callback(&data);
                        total_called++;
                        stats_->recordSuccessfulOperation();
                    } catch (const std::exception& e) {
                        stats_->recordFailedOperation();
                        
                        if (safe_mode) {
                            error_handler_.reportError(ErrorSeverity::WARNING,
                                                     "Callback exception: " + std::string(e.what()),
                                                     ErrorCategory::RUNTIME);
                            continue;
                        } else {
                            throw;
                        }
                    }
                }
            }
        }
        
        return total_called;
    }

    // =================================================================
    // ERROR HANDLING INTEGRATION
    // =================================================================

    /**
     * @brief Register error callback
     */
    void setErrorCallback(ErrorCallback callback) {
        error_handler_.setErrorCallback(std::move(callback));
    }

    /**
     * @brief Register recovery callback
     */
    void setRecoveryCallback(RecoveryCallback callback) {
        error_handler_.setRecoveryCallback(std::move(callback));
    }

    /**
     * @brief Report error through unified system
     */
    void reportError(ErrorSeverity severity, const std::string& message, 
                     ErrorCategory category = ErrorCategory::RUNTIME) {
        error_handler_.reportError(severity, message, category);
        stats_->safeIncrement(stats_->error_count);
    }

    /**
     * @brief Get error handler for advanced usage
     */
    ErrorHandler& getErrorHandler() { return error_handler_; }
    const ErrorHandler& getErrorHandler() const { return error_handler_; }

    // =================================================================
    // STATISTICS AND REPORTING
    // =================================================================

    /**
     * @brief Get callback statistics
     */
    const UnifiedStatistics& getStatistics() const { return *stats_; }

    /**
     * @brief Get comprehensive report
     */
    std::string getReport() const {
        std::string report = "=== " + component_name_ + " Unified Callback System ===\n";
        report += stats_->getReport() + "\n";
        
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        report += "Registered Callback Types:\n";
        for (const auto& [type_name, categories] : typed_callbacks_) {
            size_t total_callbacks = 0;
            for (const auto& [category, callbacks] : categories) {
                total_callbacks += callbacks.size();
            }
            report += "  " + type_name + ": " + std::to_string(total_callbacks) + " callbacks\n";
        }
        
        if (error_handler_.hasErrors()) {
            report += "\nError Report:\n" + error_handler_.getErrorReport();
        }
        
        return report;
    }

    /**
     * @brief Clear all callbacks and reset statistics
     */
    void clear() {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        typed_callbacks_.clear();
        callback_ids_.clear();
        error_handler_.clearErrors();
        stats_->reset();
        next_callback_id_.store(1);
    }

    /**
     * @brief Get callback count
     */
    size_t getCallbackCount() const {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        return callback_ids_.size();
    }

private:
    std::string component_name_;
    ErrorHandler error_handler_;
    std::unique_ptr<UnifiedStatistics> stats_;
    
    // Thread-safe callback storage
    mutable std::mutex callbacks_mutex_;
    
    // Type-erased callback storage: [type_name][category] -> [(id, callback)]
    std::unordered_map<std::string, 
                      std::unordered_map<std::string, 
                                        std::vector<std::pair<uint32_t, std::function<void(const void*)>>>>> typed_callbacks_;
    
    // Reverse lookup: callback_id -> (type_name, category)
    std::unordered_map<uint32_t, std::pair<std::string, std::string>> callback_ids_;
    
    std::atomic<uint32_t> next_callback_id_{1};

    /**
     * @brief Convert key to string for storage
     */
    template<typename KeyType>
    std::string keyToString(const KeyType& key) {
        if constexpr (std::is_same_v<KeyType, std::string>) {
            return key;
        } else if constexpr (std::is_arithmetic_v<KeyType>) {
            return std::to_string(key);
        } else {
            // For complex types, use address as string
            return std::to_string(reinterpret_cast<uintptr_t>(&key));
        }
    }
};

/**
 * @brief Legacy type aliases for backward compatibility
 */
template<typename T>
using LegacyCaller = UnifiedCallbackSystem;

template<typename KeyType, typename DataType>
using LegacyCallerKeyed = UnifiedCallbackSystem;

/**
 * @brief Global unified callback registry
 */
class GlobalCallbackRegistry {
public:
    static GlobalCallbackRegistry& getInstance() {
        static GlobalCallbackRegistry instance;
        return instance;
    }

    /**
     * @brief Get or create callback system for a component
     */
    UnifiedCallbackSystem& getCallbackSystem(const std::string& component_name) {
        std::lock_guard<std::mutex> lock(systems_mutex_);
        auto it = systems_.find(component_name);
        if (it == systems_.end()) {
            it = systems_.emplace(component_name, 
                                 std::make_unique<UnifiedCallbackSystem>(component_name)).first;
        }
        return *it->second;
    }

    /**
     * @brief Get global report of all callback systems
     */
    std::string getGlobalReport() const {
        std::lock_guard<std::mutex> lock(systems_mutex_);
        std::string report = "=== Global Callback Systems Report ===\n";
        
        for (const auto& [name, system] : systems_) {
            report += system->getReport() + "\n";
        }
        
        return report;
    }

    /**
     * @brief Clear all callback systems
     */
    void clearAll() {
        std::lock_guard<std::mutex> lock(systems_mutex_);
        for (auto& [name, system] : systems_) {
            system->clear();
        }
    }

private:
    mutable std::mutex systems_mutex_;
    std::unordered_map<std::string, std::unique_ptr<UnifiedCallbackSystem>> systems_;
};

/**
 * @brief Convenience macros for callback registration
 */
#define AXONVEX_REGISTER_CALLBACK(system, type, callback) \
    (system).registerCallback<type>([](const type& data) { callback(data); })

#define AXONVEX_REGISTER_SAFE_CALLBACK(system, type, callback) \
    (system).registerSafeCallback<type>([](const type& data) -> bool { return callback(data); })

#define AXONVEX_CALL_CALLBACKS(system, data) \
    (system).callCallbacks(data, "default", true)

} // namespace axonvex::core