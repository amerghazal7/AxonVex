/**
 * @file caller.hpp
 * @brief Basic caller implementation for the AxonVex callback system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 * 
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 * 
 * Provides a caller that can manage and trigger multiple callbacks
 * for event-driven programming in the AxonVex framework.
 */

#pragma once

#include <vector>
#include <memory>
#include <algorithm>

#include "callback.hpp"

namespace axonvex::core {

/**
 * @brief Basic caller class for managing and triggering callbacks
 * 
 * The Caller class maintains a list of callbacks and provides methods to
 * register new callbacks and trigger all registered callbacks with data.
 * This enables a publisher-subscriber pattern for decoupled communication.
 * 
 * @tparam T The type of data passed to callbacks
 * 
 * @example
 * ```cpp
 * axonvex::core::Caller<int> dataCaller;
 * 
 * // Register callbacks
 * dataCaller.registerCallback(&myProcessor);
 * dataCaller.registerCallback(&myLogger);
 * 
 * // Trigger all callbacks
 * dataCaller.callCallbacks(42);
 * ```
 */
template <typename T>
class Caller {
private:
    std::vector<Callback<T>*> callbacks_;
    
public:
    /**
     * @brief Default constructor
     */
    Caller() = default;
    
    /**
     * @brief Destructor
     */
    ~Caller() = default;
    
    // Non-copyable but movable
    Caller(const Caller&) = delete;
    Caller& operator=(const Caller&) = delete;
    Caller(Caller&&) = default;
    Caller& operator=(Caller&&) = default;
    
    /**
     * @brief Register a callback to be triggered when callCallbacks is called
     * 
     * @param callback Pointer to a Callback<T> instance. The caller does not
     *                 take ownership of the callback - the caller must ensure
     *                 the callback remains valid for the lifetime of the caller.
     * 
     * @note The same callback can be registered multiple times and will be
     *       called multiple times when triggered.
     */
    void registerCallback(Callback<T>* callback) {
        if (callback) {
            callbacks_.push_back(callback);
        }
    }
    
    /**
     * @brief Unregister a specific callback
     * 
     * Removes the first occurrence of the specified callback from the list.
     * If the callback was registered multiple times, only the first occurrence
     * is removed.
     * 
     * @param callback Pointer to the callback to remove
     * @return true if the callback was found and removed, false otherwise
     */
    bool unregisterCallback(Callback<T>* callback) {
        auto it = std::find(callbacks_.begin(), callbacks_.end(), callback);
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Unregister all occurrences of a specific callback
     * 
     * Removes all occurrences of the specified callback from the list.
     * 
     * @param callback Pointer to the callback to remove
     * @return Number of callbacks removed
     */
    size_t unregisterAllCallback(Callback<T>* callback) {
        size_t removed = 0;
        auto it = callbacks_.begin();
        while (it != callbacks_.end()) {
            if (*it == callback) {
                it = callbacks_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }
    
    /**
     * @brief Call all registered callbacks with the provided data
     * 
     * Iterates through all registered callbacks and calls their
     * callbackPerform method with the provided data.
     * 
     * @param data The data to pass to all callbacks
     * 
     * @note If any callback throws an exception, it will propagate and
     *       prevent subsequent callbacks from being called. Consider using
     *       callCallbacksSafe for exception-safe calling.
     */
    void callCallbacks(const T& data) {
        for (Callback<T>* callback : callbacks_) {
            if (callback) {
                callback->callbackPerform(data);
            }
        }
    }
    
    /**
     * @brief Call all registered callbacks with exception safety
     * 
     * Similar to callCallbacks, but catches exceptions from individual
     * callbacks to ensure all callbacks are called even if some throw.
     * 
     * @param data The data to pass to all callbacks
     * @return Number of callbacks that threw exceptions
     */
    size_t callCallbacksSafe(const T& data) noexcept {
        size_t exceptions_count = 0;
        for (Callback<T>* callback : callbacks_) {
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
     * @brief Get the number of registered callbacks
     * 
     * @return Number of registered callbacks
     */
    size_t getCallbackCount() const noexcept {
        return callbacks_.size();
    }
    
    /**
     * @brief Clear all registered callbacks
     */
    void clear() noexcept {
        callbacks_.clear();
    }
    
    /**
     * @brief Check if any callbacks are registered
     * 
     * @return true if there are registered callbacks, false otherwise
     */
    bool empty() const noexcept {
        return callbacks_.empty();
    }
};

} // namespace axonvex::core 