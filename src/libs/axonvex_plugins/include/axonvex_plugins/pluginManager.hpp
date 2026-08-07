#pragma once

#include <axonvex_plugins/pluginInterface.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#endif

namespace axonvex::plugins {

class PluginManager {
  public:
    struct LoadResult {
        PluginInterface* instance{nullptr};
        std::function<void(PluginInterface*)> destroy; // required
        void* handle{nullptr};                         // platform handle (dlopen)
        std::string error{};                           // reason, set when instance is null
    };

    using LoaderFn = std::function<LoadResult(const std::string& path)>;

    explicit PluginManager(LoaderFn loader = LoaderFn{}) : loader_(std::move(loader)) {}

    // Owns dlopen handles + plugin instances: never safe to duplicate.
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = default;

    // Not = default: a defaulted move-assign would replace plugins_ without
    // running any cleanup on the entries it overwrites (LoadedPlugin has no
    // destructor of its own), leaking whatever *this already owned.
    PluginManager& operator=(PluginManager&& other) {
        if (this != &other) {
            unloadAllPlugins();
            plugins_ = std::move(other.plugins_);
            loader_ = std::move(other.loader_);
            lastError_ = std::move(other.lastError_);
        }
        return *this;
    }

    ~PluginManager() {
        unloadAllPlugins();
    }

    // Factory for POSIX loader (Linux). Expects plugins to export C symbols:
    //   extern "C" std::uint32_t axonvex_plugin_abi_version(); // checked first
    //   extern "C" axonvex::plugins::PluginInterface* axonvex_create_plugin();
    //   extern "C" void axonvex_destroy_plugin(axonvex::plugins::PluginInterface*);
    static LoaderFn makePosixLoader() {
#if defined(AXONVEX_PLATFORM_LINUX)
        return [](const std::string& path) -> LoadResult {
            LoadResult lr;
            // Explicit flags: POSIX leaves the default scope implementation-defined
            // even though glibc happens to default to LOCAL.
            void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                const char* reason = dlerror();
                lr.error = std::string("dlopen failed: ") + (reason ? reason : "unknown error");
                return lr;
            }
            using AbiFn = std::uint32_t (*)();
            using CreateFn = PluginInterface* (*)();
            using DestroyFn = void (*)(PluginInterface*);

            auto abiVersion = reinterpret_cast<AbiFn>(dlsym(handle, "axonvex_plugin_abi_version"));
            if (!abiVersion) {
                lr.error =
                    "missing axonvex_plugin_abi_version symbol (stale or non-conforming plugin)";
                dlclose(handle);
                return lr;
            }
            std::uint32_t pluginAbi = 0;
            try {
                pluginAbi = abiVersion();
            } catch (...) {
                lr.error = "axonvex_plugin_abi_version() threw";
                dlclose(handle);
                return lr;
            }
            if (pluginAbi != AXONVEX_PLUGIN_ABI_VERSION) {
                lr.error = "ABI mismatch: plugin reports " + std::to_string(pluginAbi) +
                           ", loader expects " + std::to_string(AXONVEX_PLUGIN_ABI_VERSION);
                dlclose(handle);
                return lr;
            }

            auto create = reinterpret_cast<CreateFn>(dlsym(handle, "axonvex_create_plugin"));
            auto destroy = reinterpret_cast<DestroyFn>(dlsym(handle, "axonvex_destroy_plugin"));
            if (!create || !destroy) {
                lr.error = "missing axonvex_create_plugin/axonvex_destroy_plugin symbol";
                dlclose(handle);
                return lr;
            }
            PluginInterface* inst = nullptr;
            try {
                inst = create();
            } catch (...) {
                lr.error = "axonvex_create_plugin() threw";
                dlclose(handle);
                return lr;
            }
            if (!inst) {
                lr.error = "axonvex_create_plugin() returned null";
                dlclose(handle);
                return lr;
            }
            lr.instance = inst;
            lr.handle = handle;
            lr.destroy = [destroy](PluginInterface* p) { destroy(p); };
            return lr;
        };
#else
        return LoaderFn{};
#endif
    }

    bool loadPlugin(const std::string& path) {
        lastError_.clear();
        if (!loader_) {
            lastError_ = "no loader configured";
            return false;
        }
        LoadResult lr = loader_(path);
        if (!lr.instance || !lr.destroy) {
            lastError_ = lr.error.empty() ? "plugin load failed" : lr.error;
            return false;
        }
        bool initialized = false;
        bool initializeThrew = false;
        try {
            initialized = lr.instance->initialize();
        } catch (...) { initializeThrew = true; }
        if (!initialized) {
            lastError_ =
                initializeThrew ? "plugin initialize() threw" : "plugin initialize() failed";
            // Ensure cleanup on failed/throwing initialize
            lr.destroy(lr.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
            if (lr.handle)
                dlclose(lr.handle);
#endif
            return false;
        }
        auto name = lr.instance->name();
        auto inserted = plugins_.emplace(name, LoadedPlugin{lr.instance, lr.destroy, lr.handle});
        if (!inserted.second) {
            lastError_ = "duplicate plugin name: " + name;
            lr.instance->shutdown();
            lr.destroy(lr.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
            if (lr.handle)
                dlclose(lr.handle);
#endif
            return false;
        }
        return true;
    }

    // Reason for the most recent loadPlugin() call's failure; empty after
    // a successful loadPlugin(). NOT meaningful after loadPluginsFromDirectory()
    // for a single file's failure — see that method's doc comment.
    const std::string& lastError() const {
        return lastError_;
    }

    // Loads every *.so in `directory`. Each file's loadPlugin() clears and
    // resets lastError_ in turn (readdir order is unspecified), so a
    // failure followed by a later success in the same scan would
    // otherwise erase the failure entirely. Instead: on return, lastError_
    // holds a "filename: reason" summary joined with "; " for every file
    // that failed to load in THIS call (empty if all succeeded or none
    // were found), regardless of what any individual loadPlugin() call
    // left behind. Cleared at entry so the no-loader/opendir-fail/empty-
    // directory early-return paths never leak a stale error from a
    // previous call.
    bool loadPluginsFromDirectory(const std::string& directory) {
        lastError_.clear();
#if defined(AXONVEX_PLATFORM_LINUX)
        if (!loader_) {
            lastError_ = "no loader configured";
            return false;
        }
        DIR* dir = opendir(directory.c_str());
        if (!dir) {
            lastError_ = "opendir(" + directory + ") failed: " + std::strerror(errno);
            return false;
        }
        struct dirent* entry;
        bool any = false;
        std::vector<std::string> failures;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fname = entry->d_name;
            if (fname.size() > 3 && fname.rfind(".so") == fname.size() - 3) {
                std::string full = directory + "/" + fname;
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    if (loadPlugin(full)) {
                        any = true;
                    } else {
                        failures.push_back(fname + ": " + lastError_);
                    }
                }
            }
        }
        closedir(dir);
        if (!failures.empty()) {
            std::string combined;
            for (size_t i = 0; i < failures.size(); ++i) {
                if (i > 0)
                    combined += "; ";
                combined += failures[i];
            }
            lastError_ = combined;
        }
        return any;
#else
        (void)directory;
        lastError_ = "directory scanning not implemented on this platform";
        return false;
#endif
    }

    void unloadAllPlugins() {
        for (auto& kv : plugins_) {
            auto& p = kv.second;
            if (p.instance) {
                p.instance->shutdown();
                p.destroy(p.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
                if (p.handle)
                    dlclose(p.handle);
#endif
            }
        }
        plugins_.clear();
    }

    bool unloadPlugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end())
            return false;
        auto& p = it->second;
        if (p.instance) {
            p.instance->shutdown();
            p.destroy(p.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
            if (p.handle)
                dlclose(p.handle);
#endif
        }
        plugins_.erase(it);
        return true;
    }

    std::vector<std::string> getLoadedPlugins() const {
        std::vector<std::string> names;
        names.reserve(plugins_.size());
        for (const auto& kv : plugins_)
            names.push_back(kv.first);
        return names;
    }

    void setLoader(LoaderFn loader) {
        loader_ = std::move(loader);
    }

  private:
    struct LoadedPlugin {
        PluginInterface* instance{nullptr};
        std::function<void(PluginInterface*)> destroy{};
        void* handle{nullptr};
    };

    std::unordered_map<std::string, LoadedPlugin> plugins_;
    LoaderFn loader_;
    std::string lastError_;
};

} // namespace axonvex::plugins
