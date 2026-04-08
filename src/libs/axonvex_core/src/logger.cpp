/**
 * @file logger.cpp
 * @brief AxonVex Logger Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <axonvex_core/logger.hpp>

namespace axonvex::core {
constexpr size_t Logger::DEFAULT_QUEUE_SIZE;
constexpr size_t Logger::DEFAULT_POOL_SIZE;
constexpr size_t Logger::MIN_QUEUE_SIZE;
constexpr size_t Logger::MAX_QUEUE_SIZE;
} // namespace axonvex::core

// Static member definitions for FileLogger
namespace axonvex::Log {
std::shared_ptr<axonvex::core::FileOutput> FileLogger::file_output_;
std::once_flag FileLogger::init_flag_;
} // namespace axonvex::Log
