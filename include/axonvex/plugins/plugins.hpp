/**
 * @file plugins.hpp
 * @brief AxonVex Plugins Module - Plugin system for framework extensibility
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

// Plugin API
#include <axonvex/plugins/api/pluginInterface.hpp>

// Plugin Management
#include <axonvex/plugins/lifecycle/pluginManager.hpp>

// Plugin Registry (placeholder for future implementation)
// #include <axonvex/plugins/registry/pluginRegistry.hpp>

// Plugin Loader (placeholder for future implementation)  
// #include <axonvex/plugins/loader/pluginLoader.hpp>

namespace axonvex::plugins {
    
    // Re-export commonly used types for convenience
    using PluginInterface = api::PluginInterface;
    using PluginMetadata = api::PluginMetadata;
    using PluginManager = lifecycle::PluginManager;
    
    // Module version information
    constexpr int PLUGINS_MODULE_VERSION_MAJOR = 1;
    constexpr int PLUGINS_MODULE_VERSION_MINOR = 0;
    constexpr int PLUGINS_MODULE_VERSION_PATCH = 0;
    
    // Module information
    inline const char* getPluginsModuleVersion() {
        return "1.0.0";
    }
    
    inline const char* getPluginsModuleDescription() {
        return "AxonVex Plugins Module - Plugin system for framework extensibility";
    }
    
    /**
     * @brief Initialize the plugins module
     */
    inline void initialize() {
        // Initialize any global plugin system components if needed
    }

    /**
     * @brief Cleanup the plugins module
     */
    inline void cleanup() {
        // Cleanup any global plugin system components if needed
    }

} // namespace axonvex::plugins