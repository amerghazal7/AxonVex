#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <memory>

namespace axonvex::core {

/**
 * @brief Error severity levels
 */
enum class ErrorSeverity {
    DEBUG = 0,      ///< Debug information
    INFO = 1,       ///< Informational
    WARNING = 2,    ///< Warning condition
    ERROR = 3,      ///< Error condition
    CRITICAL = 4,   ///< Critical error
    FATAL = 5       ///< Fatal error (system cannot continue)
};

/**
 * @brief Error categories for better classification
 */
enum class ErrorCategory {
    NONE = 0,
    CONFIGURATION,
    INITIALIZATION,
    RUNTIME,
    MEMORY,
    TIMING,
    COMMUNICATION,
    RESOURCE,
    VALIDATION,
    SYSTEM,
    UNKNOWN
};

/**
 * @brief Detailed error information structure
 */
struct ErrorInfo {
    ErrorSeverity severity{ErrorSeverity::ERROR};
    ErrorCategory category{ErrorCategory::UNKNOWN};
    std::string message;
    std::string component_name;
    std::string source_file;
    int source_line{0};
    std::string source_function;
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
    
    // Additional context data
    std::string stack_trace;
    std::string user_data;
    int error_code{0};
    
    ErrorInfo() = default;
    
    ErrorInfo(ErrorSeverity sev, ErrorCategory cat, const std::string& msg, 
              const std::string& comp = "", const std::string& file = "", 
              int line = 0, const std::string& func = "")
        : severity(sev), category(cat), message(msg), component_name(comp),
          source_file(file), source_line(line), source_function(func) {}
    
    /**
     * @brief Get formatted error message
     */
    std::string getFormattedMessage() const;
    
    /**
     * @brief Get severity as string
     */
    std::string getSeverityString() const;
    
    /**
     * @brief Get category as string
     */
    std::string getCategoryString() const;
    
    /**
     * @brief Check if this is a recoverable error
     */
    bool isRecoverable() const noexcept {
        return severity < ErrorSeverity::FATAL;
    }
    
    /**
     * @brief Check if this is a critical or fatal error
     */
    bool isCritical() const noexcept {
        return severity >= ErrorSeverity::CRITICAL;
    }
};

/**
 * @brief Error handling interface for components
 */
class IErrorHandler {
public:
    using ErrorCallback = std::function<void(const ErrorInfo&)>;
    using RecoveryCallback = std::function<bool(const ErrorInfo&)>;
    
    virtual ~IErrorHandler() = default;
    
    /**
     * @brief Report an error
     */
    virtual void reportError(const ErrorInfo& error) = 0;
    
    /**
     * @brief Check if component has errors
     */
    virtual bool hasErrors() const noexcept = 0;
    
    /**
     * @brief Check if component has critical errors
     */
    virtual bool hasCriticalErrors() const noexcept = 0;
    
    /**
     * @brief Get last error information
     */
    virtual ErrorInfo getLastError() const = 0;
    
    /**
     * @brief Clear all errors
     */
    virtual void clearErrors() = 0;
    
    /**
     * @brief Get error count
     */
    virtual size_t getErrorCount() const noexcept = 0;
    
    /**
     * @brief Set error callback
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    /**
     * @brief Set recovery callback
     */
    virtual void setRecoveryCallback(RecoveryCallback callback) = 0;
};

/**
 * @brief Common error handler implementation
 */
class ErrorHandler : public IErrorHandler {
public:
    static constexpr size_t DEFAULT_MAX_ERRORS = 1000;
    
    explicit ErrorHandler(const std::string& component_name = "Unknown", 
                         size_t max_errors = DEFAULT_MAX_ERRORS);
    
    ~ErrorHandler() override = default;
    
    // Non-copyable but movable
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;
    ErrorHandler(ErrorHandler&&) noexcept = default;
    ErrorHandler& operator=(ErrorHandler&&) noexcept = default;
    
    /**
     * @brief Report an error with full information
     */
    void reportError(const ErrorInfo& error) override;
    
    /**
     * @brief Convenience method to report error with minimal info
     */
    void reportError(ErrorSeverity severity, const std::string& message,
                     ErrorCategory category = ErrorCategory::RUNTIME);
    
    /**
     * @brief Report error with source location information
     */
    void reportError(ErrorSeverity severity, const std::string& message, 
                     ErrorCategory category, const std::string& file, 
                     int line, const std::string& function);
    
    bool hasErrors() const noexcept override;
    bool hasCriticalErrors() const noexcept override;
    ErrorInfo getLastError() const override;
    void clearErrors() override;
    size_t getErrorCount() const noexcept override;
    
    void setErrorCallback(ErrorCallback callback) override;
    void setRecoveryCallback(RecoveryCallback callback) override;
    
    /**
     * @brief Get all errors (up to max_errors)
     */
    std::vector<ErrorInfo> getAllErrors() const;
    
    /**
     * @brief Get errors by severity
     */
    std::vector<ErrorInfo> getErrorsBySeverity(ErrorSeverity min_severity) const;
    
    /**
     * @brief Get errors by category
     */
    std::vector<ErrorInfo> getErrorsByCategory(ErrorCategory category) const;
    
    /**
     * @brief Get error statistics
     */
    struct ErrorStatistics {
        size_t total_errors{0};
        size_t warning_count{0};
        size_t error_count{0};
        size_t critical_count{0};
        size_t fatal_count{0};
        std::chrono::steady_clock::time_point first_error_time;
        std::chrono::steady_clock::time_point last_error_time;
    };
    
    ErrorStatistics getStatistics() const;
    
    /**
     * @brief Enable/disable automatic recovery attempts
     */
    void setAutoRecoveryEnabled(bool enabled) noexcept {
        auto_recovery_enabled_.store(enabled);
    }
    
    /**
     * @brief Check if auto recovery is enabled
     */
    bool isAutoRecoveryEnabled() const noexcept {
        return auto_recovery_enabled_.load();
    }
    
    /**
     * @brief Set maximum recovery attempts
     */
    void setMaxRecoveryAttempts(uint32_t max_attempts) noexcept {
        max_recovery_attempts_.store(max_attempts);
    }
    
    /**
     * @brief Get error summary report
     */
    std::string getErrorReport() const;
    
private:
    std::string component_name_;
    size_t max_errors_;
    
    mutable std::mutex errors_mutex_;
    std::vector<ErrorInfo> errors_;
    std::atomic<size_t> error_count_{0};
    std::atomic<size_t> critical_error_count_{0};
    
    ErrorCallback error_callback_;
    RecoveryCallback recovery_callback_;
    
    std::atomic<bool> auto_recovery_enabled_{false};
    std::atomic<uint32_t> max_recovery_attempts_{3};
    std::atomic<uint32_t> recovery_attempts_{0};
    
    void attemptRecovery(const ErrorInfo& error);
    void trimErrorsIfNeeded();
};

/**
 * @brief RAII error context helper for automatic error reporting
 */
class ErrorContext {
public:
    ErrorContext(ErrorHandler& handler, const std::string& operation_name);
    ~ErrorContext();
    
    /**
     * @brief Mark operation as successful (prevents error on destruction)
     */
    void markSuccess() noexcept { success_ = true; }
    
    /**
     * @brief Add context information
     */
    void addContext(const std::string& key, const std::string& value);
    
    /**
     * @brief Report error within this context
     */
    void reportError(ErrorSeverity severity, const std::string& message,
                     ErrorCategory category = ErrorCategory::RUNTIME);

private:
    ErrorHandler& handler_;
    std::string operation_name_;
    std::chrono::steady_clock::time_point start_time_;
    bool success_{false};
    std::string context_data_;
};

// Convenience macros for error reporting with source location
#define AXONVEX_REPORT_ERROR(handler, severity, message, category) \
    (handler).reportError((severity), (message), (category), __FILE__, __LINE__, __FUNCTION__)

#define AXONVEX_REPORT_WARNING(handler, message) \
    AXONVEX_REPORT_ERROR(handler, axonvex::core::ErrorSeverity::WARNING, message, axonvex::core::ErrorCategory::RUNTIME)

#define AXONVEX_REPORT_ERROR_MSG(handler, message) \
    AXONVEX_REPORT_ERROR(handler, axonvex::core::ErrorSeverity::ERROR, message, axonvex::core::ErrorCategory::RUNTIME)

#define AXONVEX_REPORT_CRITICAL(handler, message) \
    AXONVEX_REPORT_ERROR(handler, axonvex::core::ErrorSeverity::CRITICAL, message, axonvex::core::ErrorCategory::RUNTIME)

#define AXONVEX_REPORT_FATAL(handler, message) \
    AXONVEX_REPORT_ERROR(handler, axonvex::core::ErrorSeverity::FATAL, message, axonvex::core::ErrorCategory::RUNTIME)

// Utility functions
std::string to_string(ErrorSeverity severity);
std::string to_string(ErrorCategory category);

} // namespace axonvex::core 