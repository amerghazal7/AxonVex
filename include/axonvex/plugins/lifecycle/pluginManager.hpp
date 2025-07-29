/**
 * @file pluginManager.hpp
 * @brief Plugin Manager for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/plugins/api/pluginInterface.hpp>
#include <axonvex/core/configuration.hpp>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace axonvex::plugins::lifecycle {

/**
 * @brief Manages plugin loading, unloading, and lifecycle
 */
class PluginManager {
public:
    using PluginPtr = std::unique_ptr<api::PluginInterface>;
    using ServiceCallback = std::function<void(const std::string&, void*)>;

    PluginManager();
    ~PluginManager();

    // Plugin loading and management
    bool loadPlugin(const std::string& pluginPath);
    bool loadPluginsFromDirectory(const std::string& directory);
    void unloadPlugin(const std::string& pluginName);
    void unloadAllPlugins();

    // Plugin discovery
    std::vector<std::string> getLoadedPlugins() const;
    std::vector<std::string> getAvailableServices() const;
    bool hasPlugin(const std::string& pluginName) const;
    bool hasService(const std::string& serviceName) const;

    // Service access
    template<typename T>
    T* getService(const std::string& serviceName) {
        std::lock_guard<std::mutex> lock(servicesMutex_);
        auto it = services_.find(serviceName);
        if (it != services_.end()) {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }

    void registerService(const std::string& serviceName, void* service);
    void unregisterService(const std::string& serviceName);

    // Configuration
    void setConfiguration(const axonvex::core::Configuration& config);
    axonvex::core::Configuration getConfiguration() const;

    // Event callbacks
    void setPluginLoadedCallback(ServiceCallback callback);
    void setPluginUnloadedCallback(ServiceCallback callback);

    // Plugin information
    api::PluginMetadata getPluginMetadata(const std::string& pluginName) const;
    bool isPluginActive(const std::string& pluginName) const;
    void setPluginActive(const std::string& pluginName, bool active);

private:
    std::unordered_map<std::string, PluginPtr> plugins_;
    std::unordered_map<std::string, void*> services_;
    std::unordered_map<std::string, void*> pluginHandles_; // For dynamic loading
    axonvex::core::Configuration config_;
    
    mutable std::mutex pluginsMutex_;
    mutable std::mutex servicesMutex_;
    
    ServiceCallback pluginLoadedCallback_;
    ServiceCallback pluginUnloadedCallback_;

    bool registerPlugin(const std::string& name, PluginPtr plugin);
    void unregisterPlugin(const std::string& name);
    void registerServices(api::PluginInterface* plugin);
    void unregisterServices(api::PluginInterface* plugin);
    
    // Dynamic loading helpers
    void* loadLibrary(const std::string& path);
    void unloadLibrary(void* handle);
    void* getSymbol(void* handle, const std::string& symbolName);
};

} // namespace axonvex::plugins::lifecycle