#pragma once

#include <chrono>
#include <string>

namespace axonvex::safety {

enum class SafetyLevel {
    NOMINAL = 0,
    ADVISORY,
    CAUTION,
    WARNING,
    CRITICAL,
    EMERGENCY
};

inline const char* to_string(SafetyLevel level) {
    switch (level) {
        case SafetyLevel::NOMINAL:   return "NOMINAL";
        case SafetyLevel::ADVISORY:  return "ADVISORY";
        case SafetyLevel::CAUTION:   return "CAUTION";
        case SafetyLevel::WARNING:   return "WARNING";
        case SafetyLevel::CRITICAL:  return "CRITICAL";
        case SafetyLevel::EMERGENCY: return "EMERGENCY";
        default:                     return "UNKNOWN";
    }
}

struct SafetyEvent {
    SafetyLevel level{SafetyLevel::NOMINAL};
    std::string source;
    std::string description;
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
};

struct PolicyResult {
    SafetyLevel level{SafetyLevel::NOMINAL};
    std::string description;
};

/**
 * @brief Abstract base for safety policies evaluated by SafetyManager
 *
 * Derived classes implement domain-specific safety checks (e.g.
 * geofence, battery threshold, communication timeout). The manager
 * calls evaluate() periodically and reacts to the returned level.
 */
class SafetyPolicy {
  public:
    explicit SafetyPolicy(std::string name) : name_(std::move(name)) {}
    virtual ~SafetyPolicy() = default;

    SafetyPolicy(const SafetyPolicy&) = delete;
    SafetyPolicy& operator=(const SafetyPolicy&) = delete;
    SafetyPolicy(SafetyPolicy&&) = default;
    SafetyPolicy& operator=(SafetyPolicy&&) = default;

    virtual PolicyResult evaluate() = 0;

    const std::string& getName() const { return name_; }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

  private:
    std::string name_;
    bool enabled_{true};
};

} // namespace axonvex::safety
