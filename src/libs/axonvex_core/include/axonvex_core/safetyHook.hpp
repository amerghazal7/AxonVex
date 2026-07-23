/**
 * @file safetyHook.hpp
 * @brief Core-owned hook interface for safety components
 *
 * Upper-layer safety components (e.g. axonvex::safety::SafetyManager) implement
 * this interface so the core can react to emergency stops without depending on
 * the safety library — core depends on nothing internal (architecture rule #1).
 */

#pragma once

#include <functional>
#include <string>

namespace axonvex::core {

class SafetyHook {
  public:
    using EmergencyCallback = std::function<void(const std::string& reason)>;

    virtual ~SafetyHook() = default;

    /// True while an emergency stop is engaged.
    virtual bool isEmergencyStopped() const = 0;

    /// Register the callback fired when an emergency stop engages. Invoked at
    /// most once per engaged e-stop, from the thread that triggers it, with no
    /// implementation locks held. Passing an empty function clears it.
    virtual void setEmergencyCallback(EmergencyCallback callback) = 0;
};

} // namespace axonvex::core
