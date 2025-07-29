#include <axonvex/plugins/lifecycle/pluginManager.hpp>
#include <axonvex/core/logger.hpp>
#include <axonvex/core/path.hpp>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace axonvex::plugins::lifecycle {

PluginManager::PluginManager() {
    // Initialize with default configuration
}

PluginManager::~PluginManager() {
    unloadAllPlugins();
}

bool PluginManager::loadPlugin(const std::string& pluginPath) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    try {
        // Load the shared library
        void* handle = loadLibrary(pluginPath);
        if (!handle) {
            return false;
        }
        
        // For now, just store the handle - actual plugin loading would require
        // a factory function in the shared library
        std::filesystem::path path(pluginPath);
        std::string pluginName = path.stem().string();
        
        pluginHandles_[pluginName] = handle;
        
        // TODO: Get plugin factory function and create plugin instance
        // This is a simplified implementation for the basic framework
        
        return true;
    } catch (const std::exception& e) {
        axonvex::core::Logger logger;
        logger.error("PluginManager", "Failed to load plugin: " + std::string(e.what()));
        return false;
    }
}

bool PluginManager::loadPluginsFromDirectory(const std::string& directory) {
    if (!std::filesystem::exists(directory)) {
        return false;
    }
    
    bool success = true;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string extension = entry.path().extension().string();
            // Load shared libraries based on platform
#ifdef _WIN32
            if (extension == ".dll") {
#else
            if (extension == ".so") {
#endif
                if (!loadPlugin(entry.path().string())) {
                    success = false;
                }
            }
        }
    }
    
    return success;
}

void PluginManager::unloadPlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    // Unregister services first
    auto pluginIt = plugins_.find(pluginName);
    if (pluginIt != plugins_.end()) {
        unregisterServices(pluginIt->second.get());
        plugins_.erase(pluginIt);
    }
    
    // Unload shared library
    auto handleIt = pluginHandles_.find(pluginName);
    if (handleIt != pluginHandles_.end()) {
        unloadLibrary(handleIt->second);
        pluginHandles_.erase(handleIt);
    }
    
    if (pluginUnloadedCallback_) {
        pluginUnloadedCallback_(pluginName, nullptr);
    }
}

void PluginManager::unloadAllPlugins() {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    // Copy plugin names to avoid iterator invalidation
    std::vector<std::string> pluginNames;
    for (const auto& pair : plugins_) {
        pluginNames.push_back(pair.first);
    }
    
    // Unload each plugin
    for (const std::string& name : pluginNames) {
        unloadPlugin(name);
    }
}

std::vector<std::string> PluginManager::getLoadedPlugins() const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    
    std::vector<std::string> names;
    for (const auto& pair : plugins_) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::string> PluginManager::getAvailableServices() const {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    
    std::vector<std::string> services;
    for (const auto& pair : services_) {
        services.push_back(pair.first);
    }
    return services;
}

bool PluginManager::hasPlugin(const std::string& pluginName) const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    return plugins_.find(pluginName) != plugins_.end();
}

bool PluginManager::hasService(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    return services_.find(serviceName) != services_.end();
}

void PluginManager::registerService(const std::string& serviceName, void* service) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    services_[serviceName] = service;
}

void PluginManager::unregisterService(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(servicesMutex_);
    services_.erase(serviceName);
}

void PluginManager::setConfiguration(const axonvex::core::Configuration& config) {
    // Configuration class doesn't support copy - store reference or pointer instead
    // For now, we'll skip storing the configuration until we implement proper handling
}

axonvex::core::Configuration PluginManager::getConfiguration() const {
    // Return a default configuration since we can't copy the stored one
    return axonvex::core::Configuration();
}

void PluginManager::setPluginLoadedCallback(ServiceCallback callback) {
    pluginLoadedCallback_ = callback;
}

void PluginManager::setPluginUnloadedCallback(ServiceCallback callback) {
    pluginUnloadedCallback_ = callback;
}

void* PluginManager::loadLibrary(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LAZY);
#endif
}

void PluginManager::unloadLibrary(void* handle) {
    if (!handle) return;
    
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* PluginManager::getSymbol(void* handle, const std::string& symbolName) {
    if (!handle) return nullptr;
    
#ifdef _WIN32
    return GetProcAddress(static_cast<HMODULE>(handle), symbolName.c_str());
#else
    return dlsym(handle, symbolName.c_str());
#endif
}

void PluginManager::registerServices(api::PluginInterface* plugin) {
    if (!plugin) return;
    
    auto services = plugin->getProvidedServices();
    for (const std::string& serviceName : services) {
        void* service = plugin->getService(serviceName);
        if (service) {
            registerService(serviceName, service);
        }
    }
}

void PluginManager::unregisterServices(api::PluginInterface* plugin) {
    if (!plugin) return;
    
    auto services = plugin->getProvidedServices();
    for (const std::string& serviceName : services) {
        unregisterService(serviceName);
    }
}

} // namespace axonvex::plugins::lifecycle