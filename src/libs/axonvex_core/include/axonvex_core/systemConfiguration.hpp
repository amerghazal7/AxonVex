/**
 * @file systemConfiguration.hpp
 * @brief SystemConfiguration struct, shared by AxonVexSystem and HealthMonitor
 * @author AxonVex Development Team
 *
 * Split out of system.hpp during Phase 2 core decomposition, migration step 5
 * (see docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.5): HealthMonitor's constructor takes `const SystemConfiguration&`
 * (statisticsUpdateInterval/healthCheckInterval/enableAutoRecovery/
 * maxMemoryPoolSize) but must not depend on system.hpp -- system.hpp is about
 * to depend on healthMonitor.hpp once AxonVexSystem holds a HealthMonitor
 * member, and a header cycle isn't an option. Same split rationale as
 * systemEvent.hpp (step 4). No behavior change: same struct, same fields,
 * same defaults -- system.hpp includes this instead of defining it inline.
 * validate()/toString() stay implemented in system.cpp (same file as before).
 */

#pragma once

#include <axonvex_core/logger.hpp>
#include <axonvex_core/timingController.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace axonvex::core {

/**
 * @brief System configuration structure
 */
struct SystemConfiguration {
    // Core settings
    std::string systemName{"AxonVex-System"};
    std::string version{"1.0.0"};
    LogLevel logLevel{LogLevel::Info};

    // Timing settings
    SchedulingPolicy defaultSchedulingPolicy{SchedulingPolicy::PRIORITY_BASED};
    std::chrono::microseconds systemTickRate{std::chrono::microseconds(100)};
    bool enableRealTimeScheduling{true};

    // Resource limits
    size_t maxProcessingUnits{1000};
    size_t maxMemoryPoolSize{64 * 1024 * 1024}; // 64MB
    size_t loggerQueueSize{16384};

    // Monitoring settings
    bool enablePerformanceMonitoring{true};
    std::chrono::seconds statisticsUpdateInterval{1};
    std::chrono::seconds healthCheckInterval{5};

    // Recovery settings
    bool enableAutoRecovery{true};
    uint32_t maxRecoveryAttempts{3};
    std::chrono::seconds recoveryTimeout{10};

    // File paths
    std::string configFilePath{"config/system.json"};
    std::string logFilePath{"logs/axonvex_system.log"};
    bool enableFileLogging{true};

    // Event system configuration
    size_t eventPoolSize{1024};
    size_t eventQueueSize{256};

    void validate() const;
    std::string toString() const;
};

} // namespace axonvex::core
