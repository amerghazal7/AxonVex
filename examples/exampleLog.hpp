#pragma once

// ponytail: local stdout shim shared by the examples; replaces the deleted
// axonvex::Log stream layer (plan §7). Examples print synchronously to stdout.

#include <iostream>
#include <sstream>

namespace examplelog {

class LogLine {
  public:
    LogLine() = default;
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine& operator=(LogLine&&) = delete;

    // C++14 has no guaranteed copy elision: Info()/Warn()/Error() returning by
    // value needs a move constructor. The moved-from line's stream is empty, so
    // its destructor prints nothing.
    LogLine(LogLine&& other) noexcept : oss_(std::move(other.oss_)) {}

    ~LogLine() {
        if (oss_.tellp() > 0) {
            std::cout << oss_.str() << std::endl;
        }
    }

    template <typename T>
    LogLine& operator<<(const T& value) {
        oss_ << value;
        return *this;
    }

    LogLine& operator<<(std::ostream& (*manip)(std::ostream&)) {
        oss_ << manip;
        return *this;
    }

  private:
    std::ostringstream oss_;
};

inline LogLine Info() {
    return LogLine{};
}
inline LogLine Warn() {
    return LogLine{};
}
inline LogLine Error() {
    return LogLine{};
}

} // namespace examplelog
