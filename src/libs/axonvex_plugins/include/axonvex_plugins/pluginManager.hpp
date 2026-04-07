#pragma once

#include <axonvex_plugins/pluginInterface.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace axonvex::plugins {

class PluginManager {
  public:
    struct LoadResult {
        PluginInterface* instance{nullptr};
        std::function<void(PluginInterface*)> destroy; // required
        void* handle{nullptr};                         // platform handle (dlopen)
    };

    using LoaderFn = std::function<LoadResult(const std::string& path)>;

    explicit PluginManager(LoaderFn loader = LoaderFn{}) : loader_(std::move(loader)) {}

    // Factory for POSIX loader (Linux). Expects plugins to export C symbols:
    //   extern "C" axonvex::plugins::PluginInterface* axonvex_create_plugin();
    //   extern "C" void axonvex_destroy_plugin(axonvex::plugins::PluginInterface*);
    static LoaderFn makePosixLoader() {
#if defined(AXONVEX_PLATFORM_LINUX)
        return [](const std::string& path) -> LoadResult {
            LoadResult lr;
            void* handle = dlopen(path.c_str(), RTLD_NOW);
            if (!handle) return lr;
            using CreateFn = PluginInterface* (*)();
            using DestroyFn = void (*)(PluginInterface*);
            auto create = reinterpret_cast<CreateFn>(dlsym(handle, "axonvex_create_plugin"));
            auto destroy = reinterpret_cast<DestroyFn>(dlsym(handle, "axonvex_destroy_plugin"));
            if (!create || !destroy) {
                dlclose(handle);
                return lr;
            }
            PluginInterface* inst = nullptr;
            try { inst = create(); }
            catch (...) { dlclose(handle); return lr; }
            if (!inst) { dlclose(handle); return lr; }
            lr.instance = inst;
            lr.handle = handle;
            lr.destroy = [destroy](PluginInterface* p){ destroy(p); };
            return lr;
        };
#else
        return LoaderFn{};
#endif
    }

    bool loadPlugin(const std::string& path) {
        if (!loader_) return false; // No loader configured
        LoadResult lr = loader_(path);
        if (!lr.instance || !lr.destroy) {
            return false;
        }
        if (!lr.instance->initialize()) {
            // Ensure cleanup on failed initialize
            lr.destroy(lr.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
            if (lr.handle) dlclose(lr.handle);
#endif
            return false;
        }
        auto name = lr.instance->name();
        plugins_.emplace(name, LoadedPlugin{lr.instance, std::move(lr.destroy), lr.handle});
        return true;
    }

    bool loadPluginsFromDirectory(const std::string& directory) {
#if defined(AXONVEX_PLATFORM_LINUX)
        if (!loader_) return false;
        DIR* dir = opendir(directory.c_str());
        if (!dir) return false;
        struct dirent* entry;
        bool any = false;
        while ((entry = readdir(dir)) != nullptr) {
            std::string fname = entry->d_name;
            if (fname.size() > 3 && fname.rfind(".so") == fname.size() - 3) {
                std::string full = directory + "/" + fname;
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    any = loadPlugin(full) || any;
                }
            }
        }
        closedir(dir);
        return any;
#else
        (void)directory;
        return false;
#endif
    }

    void unloadAllPlugins() {
        for (auto& [name, p] : plugins_) {
            (void)name;
            if (p.instance) {
                p.instance->shutdown();
                p.destroy(p.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
                if (p.handle) dlclose(p.handle);
#endif
            }
        }
        plugins_.clear();
    }

    bool unloadPlugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) return false;
        auto& p = it->second;
        if (p.instance) {
            p.instance->shutdown();
            p.destroy(p.instance);
#if defined(AXONVEX_PLATFORM_LINUX)
            if (p.handle) dlclose(p.handle);
#endif
        }
        plugins_.erase(it);
        return true;
    }

    std::vector<std::string> getLoadedPlugins() const {
        std::vector<std::string> names;
        names.reserve(plugins_.size());
        for (const auto& [name, _] : plugins_) names.push_back(name);
        return names;
    }

    void setLoader(LoaderFn loader) { loader_ = std::move(loader); }

  private:
    struct LoadedPlugin {
        PluginInterface* instance{nullptr};
        std::function<void(PluginInterface*)> destroy{};
        void* handle{nullptr};
    };

    std::unordered_map<std::string, LoadedPlugin> plugins_;
    LoaderFn loader_;
};

} // namespace axonvex::plugins
