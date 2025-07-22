/**
 * @file enhancedPorts.cpp
 * @brief Enhanced Port System Implementation
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#include "axonvex/core/enhancedPorts.hpp"
#include "axonvex/core/enhancedProcessingUnit.hpp"
#include <stdexcept>
#include <sstream>

namespace axonvex::core {

// EnhancedBasePort implementation
EnhancedBasePort::EnhancedBasePort(int id, const std::string& name, PortType type, EnhancedProcessingUnit* owner)
    : id_(id), name_(name), type_(type), owner_(owner) {
    if (!owner) {
        throw std::invalid_argument("Port owner cannot be null");
    }
}

uint32_t EnhancedBasePort::getPortUID() const noexcept {
    return portUID_;
}

} // namespace axonvex::core 