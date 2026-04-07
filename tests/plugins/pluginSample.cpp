#include <gtest/gtest.h>
#include <axonvex_plugins/pluginInterface.hpp>
#include <axonvex_plugins/pluginManager.hpp>
#include <string>

using namespace axonvex::plugins;

class DummyPlugin : public PluginInterface {
public:
    explicit DummyPlugin(std::string name, std::string ver = "1.0") : n(std::move(name)), v(std::move(ver)) {}
    std::string name() const override { return n; }
    std::string version() const override { return v; }
    bool initialize() override { inited = true; return true; }
    void shutdown() override { inited = false; }
    bool inited{false};
private:
    std::string n;
    std::string v;
};

namespace {
static std::string baseNameNoExt(const std::string& path) {
    auto pos = path.find_last_of('/');
    std::string fname = (pos == std::string::npos) ? path : path.substr(pos + 1);
    if (fname.size() > 3 && fname.substr(fname.size() - 3) == ".so") {
        fname = fname.substr(0, fname.size() - 3);
    }
    if (fname.empty()) return std::string("dummy");
    return fname;
}
}

// A mock loader that creates a DummyPlugin without touching the filesystem
static PluginManager::LoaderFn makeMockLoader() {
    return [](const std::string& path) -> PluginManager::LoadResult {
        PluginManager::LoadResult lr;
        auto* inst = new DummyPlugin(baseNameNoExt(path));
        lr.instance = inst;
        lr.destroy = [](PluginInterface* p){ delete p; };
        lr.handle = nullptr;
        return lr;
    };    
}

TEST(PluginManagerTest, LoadAndUnloadWithMockLoader) {
    PluginManager pm(makeMockLoader());
    ASSERT_TRUE(pm.loadPlugin("ignored.so"));

    auto loaded = pm.getLoadedPlugins();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0], std::string("ignored"));

    EXPECT_TRUE(pm.unloadPlugin("ignored"));
    EXPECT_TRUE(pm.getLoadedPlugins().empty());
}

TEST(PluginManagerTest, UnloadAll) {
    PluginManager pm(makeMockLoader());
    ASSERT_TRUE(pm.loadPlugin("p1.so"));
    ASSERT_TRUE(pm.loadPlugin("p2.so"));
    auto loaded = pm.getLoadedPlugins();
    ASSERT_EQ(loaded.size(), 2u);

    pm.unloadAllPlugins();
    EXPECT_TRUE(pm.getLoadedPlugins().empty());
}
