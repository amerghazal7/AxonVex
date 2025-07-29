/**
 * @file deviceManager.hpp
 * @brief Device Manager for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <string>

namespace axonvex::io::devices {

class DeviceManager {
public:
    bool openDevice(const std::string& devicePath);
    void closeDevice();
    bool isDeviceOpen() const;
    
private:
    // Implementation placeholder
};

} // namespace axonvex::io::devices