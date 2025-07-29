/**
 * @file pluginInterface.hpp
 * @brief Plugin Interface for AxonVex Framework
 * @author AxonVex Development Team
 * @version 1.0.0
 * @date 2025
 */

#pragma once

#include <axonvex/core/processingUnit.hpp>
#include <string>
#include <vector>
#include <memory>

namespace axonvex::plugins::api {

/**
 * @brief Base interface for all AxonVex plugins
 */
class PluginInterface : public axonvex::core::ProcessingUnit {
public:
    virtual ~PluginInterface() = default;
    
    // Plugin lifecycle
    virtual bool initialize(const std::string& config) = 0;
    virtual void cleanup() = 0;
    virtual bool isInitialized() const = 0;
    
    // Plugin information
    virtual std::string getName() const = 0;
    virtual std::string getVersion() const = 0;
    virtual std::string getDescription() const = 0;
    virtual std::string getAuthor() const = 0;
    
    // Plugin capabilities
    virtual std::vector<std::string> getProvidedServices() const = 0;
    virtual std::vector<std::string> getRequiredServices() const = 0;
    virtual bool supportsService(const std::string& serviceName) const = 0;
    virtual void* getService(const std::string& serviceName) = 0;
    
    // Plugin state
    virtual bool isActive() const = 0;
    virtual void setActive(bool active) = 0;

protected:
    std::string config_;
    std::vector<std::string> providedServices_;
    std::vector<std::string> requiredServices_;
    bool initialized_ = false;
    bool active_ = false;
};

/**
 * @brief Plugin metadata structure
 */
struct PluginMetadata {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::vector<std::string> providedServices;
    std::vector<std::string> requiredServices;
    std::string configSchema;
};

} // namespace axonvex::plugins::api