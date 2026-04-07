/**
 * @file callback.hpp
 * @brief Base callback interface for the AxonVex callback system
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 *
 * @copyright Copyright (c) 2025 AxonVex Framework. All rights reserved.
 *
 * Provides the base interface for implementing callback functionality
 * in the AxonVex real-time framework.
 */

#pragma once

namespace axonvex::core {

/**
 * @brief Base callback interface template
 *
 * Classes should inherit from this interface and implement the callbackPerform
 * method to define specific callback behavior. The callback system allows for
 * decoupled event-driven communication between components.
 *
 * @tparam T The type of data passed to the callback
 *
 * @example
 * ```cpp
 * class MyDataProcessor : public axonvex::core::Callback<int> {
 * public:
 *     void callbackPerform(const int data) override {
 *         std::cout << "Received data: " << data << std::endl;
 *         // Process the data...
 *     }
 * };
 * ```
 */
template <typename T>
class Callback {
  public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~Callback() = default;

    /**
     * @brief Pure virtual callback method to be implemented by derived classes
     *
     * This method will be called when the callback is triggered by a Caller
     * or CallerKeyed instance.
     *
     * @param data The data to be processed by the callback
     */
    virtual void callbackPerform(const T data) = 0;
};

} // namespace axonvex::core
