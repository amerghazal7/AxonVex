#pragma once

#include <cstdint>
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

// ABI version for the C plugin entry points below. Every loadable plugin
// must export:
//   extern "C" std::uint32_t axonvex_plugin_abi_version();
// returning this value. PluginManager calls it via dlsym BEFORE
// axonvex_create_plugin() and refuses to load on mismatch or on a missing
// symbol — a plugin built against a stale PluginInterface layout is UB at
// the first virtual call, so the handshake happens before any vtable is
// touched. Bump this value on any binary-incompatible change to
// PluginInterface (new/removed/reordered virtuals, ABI-affecting members).
constexpr std::uint32_t AXONVEX_PLUGIN_ABI_VERSION = 1;

} // namespace axonvex::plugins
