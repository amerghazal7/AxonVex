# AxonVex C++ Style Guide

## Overview
This document establishes the coding standards and naming conventions for the AxonVex framework. These conventions ensure consistency, readability, and maintainability across the codebase.

## Naming Conventions

### Files
- **Header files**: `camelCase.hpp`
- **Source files**: `camelCase.cpp`
- **Example**: `precisionTimer.hpp`, `precisionTimer.cpp`

### Classes and Structs
- **Format**: `CamelCase` (PascalCase)
- **Example**: `PrecisionTimer`, `ThreadSafeQueue`, `ConfigurationManager`

### Functions and Methods
- **Format**: `camelCase`
- **Example**: `startTimer()`, `getElapsedTime()`, `calculatePercentile()`

### Variables
- **Local variables**: `snake_case`
- **Member variables**: `snake_case_` (trailing underscore)
- **Static variables**: `snake_case`
- **Example**: `int loop_count`, `double elapsed_time_`, `static bool is_initialized`

### Constants
- **Format**: `UPPER_CASE` with underscores
- **Example**: `MAX_BUFFER_SIZE`, `DEFAULT_TIMEOUT_MS`

### Namespaces
- **Format**: `snake_case`
- **Example**: `axonvex::core`, `axonvex::monitoring`

### Enums
- **Enum class name**: `CamelCase`
- **Enum values**: `CamelCase`
- **Example**: 
  ```cpp
  enum class ExecutionState {
      Idle,
      Running,
      Paused,
      Terminated
  };
  ```

### Macros
- **Format**: `UPPER_CASE` with underscores
- **Prefix with framework name**: `AXONVEX_`
- **Example**: `AXONVEX_ASSERT`, `AXONVEX_LIKELY`

## Code Formatting

### Indentation
- Use 4 spaces for indentation
- No tabs

### Braces
- Opening brace on same line (K&R style)
- Always use braces for single-statement blocks
```cpp
if (condition) {
    statement;
}
```

### Line Length
- Maximum 100 characters per line
- Break long lines logically

### Comments
- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Document all public APIs with Doxygen-style comments

### Header Guards
- Use `#pragma once` for header guards

## File Organization

### Header Files (.hpp)
1. Copyright notice
2. `#pragma once`
3. System includes
4. Third-party includes
5. Project includes
6. Forward declarations
7. Namespace opening
8. Class/struct definitions
9. Inline function definitions
10. Namespace closing

### Source Files (.cpp)
1. Copyright notice
2. Corresponding header include
3. System includes
4. Third-party includes
5. Project includes
6. Anonymous namespace (for internal linkage)
7. Namespace opening
8. Function implementations
9. Namespace closing

## Documentation

### Class Documentation
```cpp
/**
 * @brief Brief description of the class
 * 
 * Detailed description of the class purpose and usage.
 * 
 * @example
 * PrecisionTimer timer;
 * timer.startTimer();
 * // ... do work ...
 * auto elapsed = timer.getElapsedTime();
 */
class PrecisionTimer {
    // ...
};
```

### Function Documentation
```cpp
/**
 * @brief Brief description of the function
 * 
 * @param param_name Description of parameter
 * @return Description of return value
 * @throws ExceptionType When this exception is thrown
 */
double calculatePercentile(double percentile) const;
```

## Best Practices

### Memory Management
- Use RAII principles
- Prefer smart pointers over raw pointers
- Use `std::unique_ptr` for single ownership
- Use `std::shared_ptr` for shared ownership

### Error Handling
- Use exceptions for error conditions
- Use `std::optional` for optional values
- Use `std::expected` (C++23) or similar for error codes

### Performance
- Mark functions `noexcept` when appropriate
- Use `constexpr` for compile-time constants
- Use `inline` for small, frequently called functions
- Prefer move semantics over copy when possible

### Thread Safety
- Document thread safety guarantees
- Use atomic operations for lockless programming
- Prefer `std::mutex` over platform-specific locks
- Use lock-free data structures when appropriate

### Templates
- Use concepts (C++20) to constrain templates
- Provide clear error messages for template failures
- Separate template declarations and definitions appropriately

## Examples

### Complete Class Example
```cpp
#pragma once

#include <chrono>
#include <atomic>
#include <vector>

namespace axonvex::core {

/**
 * @brief High-precision timer for performance measurements
 * 
 * Provides nanosecond-precision timing with statistical analysis
 * capabilities. Thread-safe and suitable for real-time applications.
 */
class PrecisionTimer {
public:
    PrecisionTimer();
    ~PrecisionTimer() = default;
    
    void startTimer();
    double getElapsedTime() const;
    void recordMeasurement();
    double calculatePercentile(double percentile) const;
    
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    std::atomic<bool> is_running_;
    std::vector<double> measurements_;
    mutable std::mutex measurements_mutex_;
};

} // namespace axonvex::core
```

This style guide ensures consistency across the AxonVex codebase while maintaining readability and following modern C++ best practices. 