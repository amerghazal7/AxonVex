/**
 * @file callerKeyed.hpp
 * @brief Key-based caller implementation for the AxonVex callback system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * Provides a key-based caller that can manage and trigger callbacks
 * organized by keys for sophisticated event routing in the AxonVex framework.
 */

#pragma once

#include "callback.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

namespace axonvex::core {

/**
 * @brief Key-based caller class for managing callbacks organized by keys
 *
 * The CallerKeyed class maintains callbacks organized by keys, allowing
 * selective triggering of callbacks based on specific keys. This enables
 * more sophisticated event routing and filtering.
 *
 * @tparam KeyType The type used for keys (must be comparable)
 * @tparam DataType The type of data passed to callbacks
 *
 * @example
 * ```cpp
 * axonvex::core::CallerKeyed<std::string, int> eventCaller;
 *
 * // Register callbacks with specific keys
 * eventCaller.registerKeyedCallback("sensor_data", &sensorProcessor);
 * eventCaller.registerKeyedCallback("user_input", &inputHandler);
 * eventCaller.registerKeyedCallback("sensor_data", &dataLogger);
 *
 * // Trigger callbacks for specific keys
 * eventCaller.callCallbacksByKey("sensor_data", 42);
 * eventCaller.callCallbacksByKey("user_input", 123);
 *
 * // Trigger all callbacks regardless of key
 * eventCaller.callAllCallbacks(999);
 * ```
 */
template <typename KeyType, typename DataType>
class CallerKeyed {
  private:
    std::multimap<KeyType, Callback<DataType>*> keyed_callbacks_;

    /**
     * @brief Helper function to get all callbacks for a specific key
     *
     * @param key The key to search for
     * @return Vector of callbacks associated with the key
     */
    std::vector<Callback<DataType>*> getCallbacksForKey(const KeyType& key) const {
        std::vector<Callback<DataType>*> result;
        auto range = keyed_callbacks_.equal_range(key);
        for (auto it = range.first; it != range.second; ++it) {
            result.push_back(it->second);
        }
        return result;
    }

  public:
    /**
     * @brief Default constructor
     */
    CallerKeyed() = default;

    /**
     * @brief Destructor
     */
    ~CallerKeyed() = default;

    // Non-copyable but movable
    CallerKeyed(const CallerKeyed&) = delete;
    CallerKeyed& operator=(const CallerKeyed&) = delete;
    CallerKeyed(CallerKeyed&&) = default;
    CallerKeyed& operator=(CallerKeyed&&) = default;

    /**
     * @brief Register a callback with a specific key
     *
     * @param key The key to associate with the callback
     * @param callback Pointer to a Callback<DataType> instance. The caller does not
     *                 take ownership of the callback - the caller must ensure
     *                 the callback remains valid for the lifetime of the caller.
     *
     * @note Multiple callbacks can be registered with the same key.
     *       The same callback can be registered multiple times with the same
     *       or different keys.
     */
    void registerKeyedCallback(const KeyType& key, Callback<DataType>* callback) {
        if (callback) {
            keyed_callbacks_.insert({key, callback});
        }
    }

    /**
     * @brief Unregister a specific callback from a specific key
     *
     * Removes the first occurrence of the callback associated with the key.
     *
     * @param key The key to search under
     * @param callback Pointer to the callback to remove
     * @return true if the callback was found and removed, false otherwise
     */
    bool unregisterKeyedCallback(const KeyType& key, Callback<DataType>* callback) {
        auto range = keyed_callbacks_.equal_range(key);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == callback) {
                keyed_callbacks_.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Unregister all callbacks associated with a specific key
     *
     * @param key The key whose callbacks should be removed
     * @return Number of callbacks removed
     */
    size_t unregisterAllCallbacksForKey(const KeyType& key) {
        auto range = keyed_callbacks_.equal_range(key);
        size_t count = std::distance(range.first, range.second);
        keyed_callbacks_.erase(range.first, range.second);
        return count;
    }

    /**
     * @brief Unregister all occurrences of a specific callback from all keys
     *
     * @param callback Pointer to the callback to remove
     * @return Number of callbacks removed
     */
    size_t unregisterAllCallback(Callback<DataType>* callback) {
        size_t removed = 0;
        auto it = keyed_callbacks_.begin();
        while (it != keyed_callbacks_.end()) {
            if (it->second == callback) {
                it = keyed_callbacks_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    /**
     * @brief Call all callbacks associated with a specific key
     *
     * @param key The key whose callbacks should be triggered
     * @param data The data to pass to the callbacks
     *
     * @note If any callback throws an exception, it will propagate and
     *       prevent subsequent callbacks from being called. Consider using
     *       callCallbacksByKeySafe for exception-safe calling.
     */
    /**
     * @brief Copy out the callbacks registered under @p key without calling them
     *
     * For callers that must not hold their registry lock across user code (C34):
     * take the snapshot under the lock, release it, then invoke. Note the result
     * holds raw pointers this class does not own — the caller is responsible for
     * ensuring they stay alive across the dispatch (see `detail::DispatchBarrier`
     * in the interfaces layer).
     */
    std::vector<Callback<DataType>*> snapshotCallbacksForKey(const KeyType& key) const {
        return getCallbacksForKey(key);
    }

    void callCallbacksByKey(const KeyType& key, const DataType& data) {
        auto callbacks = getCallbacksForKey(key);
        for (Callback<DataType>* callback : callbacks) {
            if (callback) {
                callback->callbackPerform(data);
            }
        }
    }

    /**
     * @brief Call all callbacks associated with a specific key with exception safety
     *
     * Similar to callCallbacksByKey, but catches exceptions from individual
     * callbacks to ensure all callbacks are called even if some throw.
     *
     * @param key The key whose callbacks should be triggered
     * @param data The data to pass to the callbacks
     * @return Number of callbacks that threw exceptions
     */
    size_t callCallbacksByKeySafe(const KeyType& key, const DataType& data) noexcept {
        auto callbacks = getCallbacksForKey(key);
        size_t exceptions_count = 0;
        for (Callback<DataType>* callback : callbacks) {
            if (callback) {
                try {
                    callback->callbackPerform(data);
                } catch (...) {
                    ++exceptions_count;
                    // Continue with the next callback
                }
            }
        }
        return exceptions_count;
    }

    /**
     * @brief Call all registered callbacks regardless of their keys
     *
     * @param data The data to pass to all callbacks
     *
     * @note If any callback throws an exception, it will propagate and
     *       prevent subsequent callbacks from being called. Consider using
     *       callAllCallbacksSafe for exception-safe calling.
     */
    void callAllCallbacks(const DataType& data) {
        for (const auto& pr : keyed_callbacks_) {
            if (pr.second) {
                pr.second->callbackPerform(data);
            }
        }
    }

    /**
     * @brief Call all registered callbacks with exception safety
     *
     * Similar to callAllCallbacks, but catches exceptions from individual
     * callbacks to ensure all callbacks are called even if some throw.
     *
     * @param data The data to pass to all callbacks
     * @return Number of callbacks that threw exceptions
     */
    size_t callAllCallbacksSafe(const DataType& data) noexcept {
        size_t exceptions_count = 0;
        for (const auto& pr : keyed_callbacks_) {
            if (pr.second) {
                try {
                    pr.second->callbackPerform(data);
                } catch (...) {
                    ++exceptions_count;
                    // Continue with the next callback
                }
            }
        }
        return exceptions_count;
    }

    /**
     * @brief Check if a specific key has any registered callbacks
     *
     * @param key The key to check
     * @return true if the key has registered callbacks, false otherwise
     */
    bool hasCallbacksForKey(const KeyType& key) const noexcept {
        return keyed_callbacks_.find(key) != keyed_callbacks_.end();
    }

    /**
     * @brief Get the number of callbacks registered for a specific key
     *
     * @param key The key to count callbacks for
     * @return Number of callbacks registered for the key
     */
    size_t getCallbackCountForKey(const KeyType& key) const noexcept {
        return keyed_callbacks_.count(key);
    }

    /**
     * @brief Get the total number of registered callbacks across all keys
     *
     * @return Total number of registered callbacks
     */
    size_t getTotalCallbackCount() const noexcept {
        return keyed_callbacks_.size();
    }

    /**
     * @brief Get all keys that have registered callbacks
     *
     * @return Vector of keys that have at least one callback
     */
    std::vector<KeyType> getAllKeys() const {
        std::vector<KeyType> keys;
        KeyType current_key{};
        bool first = true;

        for (const auto& pr : keyed_callbacks_) {
            if (first || pr.first != current_key) {
                keys.push_back(pr.first);
                current_key = pr.first;
                first = false;
            }
        }
        return keys;
    }

    /**
     * @brief Clear all registered callbacks
     */
    void clear() noexcept {
        keyed_callbacks_.clear();
    }

    /**
     * @brief Check if any callbacks are registered
     *
     * @return true if there are registered callbacks, false otherwise
     */
    bool empty() const noexcept {
        return keyed_callbacks_.empty();
    }
};

} // namespace axonvex::core
