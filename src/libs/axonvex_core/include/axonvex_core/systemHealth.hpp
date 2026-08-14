/**
 * @file systemHealth.hpp
 * @brief SystemHealth struct, shared by AxonVexSystem and HealthMonitor
 * @author AxonVex Development Team
 *
 * Split out of system.hpp during Phase 2 core decomposition, migration step 5
 * (see docs/superpowers/specs/2026-08-06-phase2-core-decomposition-design.md
 * section 1.5): HealthMonitor::check()/performHealthCheck() and the
 * HealthCheckCallback alias return/consume SystemHealth by value, but
 * healthMonitor.hpp must not depend on system.hpp -- system.hpp is about to
 * depend on healthMonitor.hpp once AxonVexSystem holds a HealthMonitor
 * member, and a header cycle isn't an option. Same split rationale as
 * systemEvent.hpp (step 4). No behavior change: same struct, same fields --
 * system.hpp includes this instead of defining it inline. getStatusString()
 * stays implemented in system.cpp (same file as before).
 */

#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace axonvex::core {

/**
 * @brief System health information
 */
struct SystemHealth {
    enum class Status { HEALTHY, WARNING, CRITICAL, FAILURE };

    Status overallStatus{Status::HEALTHY};
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::chrono::steady_clock::time_point lastCheckTime;

    // Component health
    bool timingControllerHealthy{true};
    bool configurationHealthy{true};
    bool loggerHealthy{true};
    bool memoryHealthy{true};

    // Performance indicators
    double cpuUtilization{0.0};
    double memoryUtilization{0.0};
    double averageExecutionTime{0.0};
    double missedDeadlineRatio{0.0};

    std::string getStatusString() const;
    bool isHealthy() const {
        return overallStatus == Status::HEALTHY;
    }
};

} // namespace axonvex::core
