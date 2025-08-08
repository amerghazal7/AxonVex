#pragma once

#include <string>
#include <vector>

namespace axonvex::plugins {

class PluginInterface {
  public:
    virtual ~PluginInterface() = default;
    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
};

} // namespace axonvex::plugins
