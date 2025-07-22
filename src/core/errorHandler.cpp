#include <algorithm>
#include <axonvex/core/errorHandler.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace axonvex::core {

// ErrorInfo implementations
std::string ErrorInfo::getFormattedMessage() const {
    std::ostringstream oss;
    oss << "[" << getSeverityString() << "] ";
    oss << "[" << component_name << "] ";
    oss << message;

    if (!source_file.empty()) {
        oss << " (" << source_file << ":" << source_line;
        if (!source_function.empty()) {
            oss << " in " << source_function << "()";
        }
        oss << ")";
    }

    return oss.str();
}

std::string ErrorInfo::getSeverityString() const {
    switch (severity) {
        case ErrorSeverity::DEBUG_LEVEL:
            return "DEBUG";
        case ErrorSeverity::INFO:
            return "INFO";
        case ErrorSeverity::WARNING:
            return "WARNING";
        case ErrorSeverity::ERROR:
            return "ERROR";
        case ErrorSeverity::CRITICAL:
            return "CRITICAL";
        case ErrorSeverity::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

std::string ErrorInfo::getCategoryString() const {
    switch (category) {
        case ErrorCategory::NONE:
            return "NONE";
        case ErrorCategory::CONFIGURATION:
            return "CONFIGURATION";
        case ErrorCategory::INITIALIZATION:
            return "INITIALIZATION";
        case ErrorCategory::RUNTIME:
            return "RUNTIME";
        case ErrorCategory::MEMORY:
            return "MEMORY";
        case ErrorCategory::TIMING:
            return "TIMING";
        case ErrorCategory::COMMUNICATION:
            return "COMMUNICATION";
        case ErrorCategory::RESOURCE:
            return "RESOURCE";
        case ErrorCategory::VALIDATION:
            return "VALIDATION";
        case ErrorCategory::SYSTEM:
            return "SYSTEM";
        case ErrorCategory::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
    }
}

// ErrorHandler implementations
ErrorHandler::ErrorHandler(const std::string& component_name, size_t max_errors)
    : component_name_(component_name), max_errors_(max_errors) {}

void ErrorHandler::reportError(const ErrorInfo& error) {
    std::lock_guard<std::mutex> lock(errors_mutex_);

    error_count_++;
    if (error.isCritical()) {
        critical_error_count_++;
    }

    errors_.push_back(error);
    trimErrorsIfNeeded();

    if (error_callback_) {
        error_callback_(error);
    }
}

void ErrorHandler::reportError(ErrorSeverity severity, const std::string& message,
                               ErrorCategory category, const std::string& source_file,
                               int line_number, const std::string& function_name) {
    ErrorInfo error;
    error.severity = severity;
    error.category = category;
    error.message = message;
    error.component_name = component_name_;
    error.source_file = source_file;
    error.source_line = line_number;
    error.source_function = function_name;
    error.timestamp = std::chrono::steady_clock::now();
    reportError(error);
}

void ErrorHandler::clearErrors() {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    errors_.clear();
}

bool ErrorHandler::hasErrors() const noexcept {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    return !errors_.empty();
}

bool ErrorHandler::hasCriticalErrors() const noexcept {
    return critical_error_count_.load() > 0;
}

size_t ErrorHandler::getErrorCount() const noexcept {
    return error_count_.load();
}

std::string ErrorHandler::getErrorReport() const {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    std::ostringstream oss;

    oss << "=== Error Report for " << component_name_ << " ===\n";
    oss << "Total Errors: " << error_count_.load() << "\n";
    oss << "Critical Errors: " << critical_error_count_.load() << "\n";

    if (!errors_.empty()) {
        oss << "\nRecent Errors:\n";
        size_t count = 0;
        for (auto it = errors_.rbegin(); it != errors_.rend() && count < 3; ++it, ++count) {
            oss << "  " << (count + 1) << ". " << it->getFormattedMessage() << "\n";
        }
    }

    return oss.str();
}

void ErrorHandler::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    error_callback_ = callback;
}

void ErrorHandler::setRecoveryCallback(RecoveryCallback callback) {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    recovery_callback_ = callback;
}

ErrorHandler::ErrorStatistics ErrorHandler::getStatistics() const {
    ErrorStatistics stats;
    stats.total_errors = error_count_.load();
    stats.critical_count = critical_error_count_.load();
    return stats;
}

void ErrorHandler::trimErrorsIfNeeded() {
    if (errors_.size() > max_errors_) {
        errors_.erase(errors_.begin(), errors_.begin() + (errors_.size() - max_errors_));
    }
}

void ErrorHandler::attemptRecovery(const ErrorInfo& error) {
    if (recovery_callback_) {
        recovery_callback_(error);
    }
}

ErrorInfo ErrorHandler::getLastError() const {
    std::lock_guard<std::mutex> lock(errors_mutex_);
    if (!errors_.empty()) {
        return errors_.back();
    }
    return ErrorInfo();
}

// ErrorContext implementations
ErrorContext::ErrorContext(ErrorHandler& handler, const std::string& operation_name)
    : handler_(handler), operation_name_(operation_name),
      start_time_(std::chrono::steady_clock::now()) {}

ErrorContext::~ErrorContext() {
    // Destructor implementation - could add auto error reporting here if needed
}

void ErrorContext::addContext(const std::string& key, const std::string& value) {
    if (!context_data_.empty()) {
        context_data_ += ", ";
    }
    context_data_ += key + "=" + value;
}

void ErrorContext::reportError(ErrorSeverity severity, const std::string& message,
                               ErrorCategory category) {
    ErrorInfo error;
    error.severity = severity;
    error.category = category;
    error.message = operation_name_ + ": " + message;
    error.timestamp = std::chrono::steady_clock::now();
    handler_.reportError(error);
}

} // namespace axonvex::core
