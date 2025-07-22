/**
 * @file ports.cpp
 * @brief Advanced Port System Implementation
 * @author AxonVex Development Team
 * @version 2.0.0
 * @date 2025
 */

#include "axonvex/core/ports.hpp"
#include "axonvex/core/processingUnit.hpp"
#include <stdexcept>
#include <sstream>

namespace axonvex::core {

// BasePort implementation
BasePort::BasePort(int id, const std::string& name, PortType type, ProcessingUnit* owner)
    : id_(id), name_(name), type_(type), owner_(owner) {
    if (!owner) {
        throw std::invalid_argument("Port owner cannot be null");
    }
}

uint32_t BasePort::getPortUID() const noexcept {
    return portUID_;
}

} // namespace axonvex::core 