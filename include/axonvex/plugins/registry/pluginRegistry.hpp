/**
 * @file pluginRegistry.hpp
 * @brief Plugin Registry for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <string>
#include <vector>

namespace axonvex::plugins::registry {

/**
 * @brief Plugin registry for discovering and managing available plugins
 * @note This is a placeholder for future implementation
 */
class PluginRegistry {
public:
    // Plugin discovery
    std::vector<std::string> discoverPlugins(const std::string& directory);
    std::vector<std::string> getAvailablePlugins() const;
    
    // Plugin metadata
    bool hasPlugin(const std::string& pluginName) const;
    std::string getPluginPath(const std::string& pluginName) const;
    
private:
    // Implementation to be added in future version
};

} // namespace axonvex::plugins::registry