/**
 * @file logger.cpp
 * @brief AxonVex Logger Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include <axonvex/core/logger.hpp>

// Static member definitions for FileLogger
namespace axonvex::Log {
    std::shared_ptr<axonvex::core::FileOutput> FileLogger::file_output_;
    std::once_flag FileLogger::init_flag_;
}
