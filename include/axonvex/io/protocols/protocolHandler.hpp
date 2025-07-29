/**
 * @file protocolHandler.hpp
 * @brief Protocol Handler for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <string>

namespace axonvex::io::protocols {

class ProtocolHandler {
public:
    bool initializeProtocol(const std::string& config);
    void cleanup();
    
private:
    // Implementation placeholder
};

} // namespace axonvex::io::protocols