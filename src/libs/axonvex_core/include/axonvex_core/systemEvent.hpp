/**
 * @file systemEvent.hpp
 * @brief SystemState enum + SystemEvent struct, shared by AxonVexSystem and EventBus
 * @author AxonVex Development Team
 *
 * Split out of system.hpp during Phase 2 core decomposition, migration step 4
 * (see docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.4): EventBus needs SystemEvent (which carries a SystemState) but
 * must not depend on system.hpp -- system.hpp is about to depend on
 * eventBus.hpp once AxonVexSystem holds an EventBus member, and a header
 * cycle isn't an option. No behavior change: same enum, same struct, same
 * values, same field order -- system.hpp includes this instead of defining
 * them inline.
 */

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace axonvex::core {

/**
 * @brief System state enumeration
 */
enum class SystemState {
    UNINITIALIZED = 0, ///< System not yet initialized
    INITIALIZING,      ///< System currently initializing
    INITIALIZED,       ///< System initialized but not started
    STARTING,          ///< System currently starting
    RUNNING,           ///< System running normally
    PAUSING,           ///< System currently pausing
    PAUSED,            ///< System paused
    RESUMING,          ///< System resuming from pause
    STOPPING,          ///< System currently stopping
    STOPPED,           ///< System stopped
    ERROR,             ///< System in error state
    FATAL_ERROR        ///< System in unrecoverable error state
};

/**
 * @brief System event structure for callbacks
 */
struct SystemEvent {
    enum class Type {
        STATE_CHANGE,
        PROCESSING_UNIT_ADDED,
        PROCESSING_UNIT_REMOVED,
        PROCESSING_UNIT_ERROR,
        PERFORMANCE_ALERT,
        CONFIGURATION_CHANGED,
        RECOVERY_STARTED,
        RECOVERY_COMPLETED,
        HEALTH_CHECK,
        SHUTDOWN_REQUESTED
    };

    Type type;
    SystemState oldState;
    SystemState newState;
    std::string description;
    std::chrono::steady_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
};

} // namespace axonvex::core
