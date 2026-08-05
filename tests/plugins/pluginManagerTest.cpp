#include <axonvex_plugins/pluginInterface.hpp>
#include <axonvex_plugins/pluginManager.hpp>
#include <gtest/gtest.h>
#include <string>

// Paths to the fixture MODULE libraries, injected by CMake
// (see root CMakeLists.txt / tests/plugins/fixture/testPlugin.cpp).
namespace {
const char* const kNormalPluginPath = AXONVEX_TEST_PLUGIN_PATH;
const char* const kOldAbiPluginPath = AXONVEX_TEST_PLUGIN_OLDABI_PATH;
const char* const kNoSymbolsPluginPath = AXONVEX_TEST_PLUGIN_NOSYMBOLS_PATH;
} // namespace

using axonvex::plugins::PluginManager;

TEST(PluginManagerTest, LoadsRealSharedObjectAndCallsFactory) {
    PluginManager pm(PluginManager::makePosixLoader());
    ASSERT_TRUE(pm.loadPlugin(kNormalPluginPath)) << pm.lastError();

    auto loaded = pm.getLoadedPlugins();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0], std::string("test_plugin"));
}

TEST(PluginManagerTest, DuplicateNameIsRefusedWithoutLeak) {
    PluginManager pm(PluginManager::makePosixLoader());
    ASSERT_TRUE(pm.loadPlugin(kNormalPluginPath)) << pm.lastError();

    EXPECT_FALSE(pm.loadPlugin(kNormalPluginPath));
    EXPECT_FALSE(pm.lastError().empty());
    // Still exactly one loaded instance: the duplicate's instance/handle
    // must have been torn down, not just discarded (LSan proves this).
    EXPECT_EQ(pm.getLoadedPlugins().size(), 1u);
}

TEST(PluginManagerTest, AbiMismatchIsRefused) {
    PluginManager pm(PluginManager::makePosixLoader());
    EXPECT_FALSE(pm.loadPlugin(kOldAbiPluginPath));
    EXPECT_NE(pm.lastError().find("ABI"), std::string::npos) << pm.lastError();
    EXPECT_TRUE(pm.getLoadedPlugins().empty());
}

TEST(PluginManagerTest, MissingSymbolsAreRefused) {
    PluginManager pm(PluginManager::makePosixLoader());
    EXPECT_FALSE(pm.loadPlugin(kNoSymbolsPluginPath));
    EXPECT_FALSE(pm.lastError().empty());
    EXPECT_TRUE(pm.getLoadedPlugins().empty());
}
