#include <axonvex_plugins/pluginInterface.hpp>
#include <axonvex_plugins/pluginManager.hpp>
#include <gtest/gtest.h>
#include <string>

#if defined(AXONVEX_PLATFORM_LINUX)
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Paths to the fixture MODULE libraries, injected by CMake
// (see root CMakeLists.txt / tests/plugins/fixture/testPlugin.cpp).
namespace {
const char* const kNormalPluginPath = AXONVEX_TEST_PLUGIN_PATH;
const char* const kOldAbiPluginPath = AXONVEX_TEST_PLUGIN_OLDABI_PATH;
const char* const kNoSymbolsPluginPath = AXONVEX_TEST_PLUGIN_NOSYMBOLS_PATH;
const char* const kThrowInitPluginPath = AXONVEX_TEST_PLUGIN_THROWINIT_PATH;

#if defined(AXONVEX_PLATFORM_LINUX)
bool copyFile(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary);
    if (!in || !out)
        return false;
    out << in.rdbuf();
    return static_cast<bool>(out);
}
#endif
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

// Finding 3 (whole-branch review): initialize() wasn't try/catch-wrapped
// like abiVersion()/create() are, so a throwing initialize() propagated
// out of loadPlugin() (breaking its bool+lastError contract) and leaked
// the instance and dlopen handle. ASan/LSan is the real assertion: a
// pre-fix run of this test leaks both the TestPlugin instance and the
// dlopen handle instead of returning false cleanly.
TEST(PluginManagerTest, ThrowingInitializeIsCaughtWithoutLeak) {
    PluginManager pm(PluginManager::makePosixLoader());
    EXPECT_FALSE(pm.loadPlugin(kThrowInitPluginPath));
    EXPECT_NE(pm.lastError().find("threw"), std::string::npos) << pm.lastError();
    EXPECT_TRUE(pm.getLoadedPlugins().empty());
}

#if defined(AXONVEX_PLATFORM_LINUX)
// Finding 2 (whole-branch review): loadPlugin() clears lastError_ on every
// call, and readdir() order is unspecified — a scan with one failing and
// one succeeding plugin could silently erase the failure's reason if the
// successful load happened to be enumerated after it. loadPluginsFromDirectory()
// now accumulates every failure from the scan and reports them all,
// regardless of enumeration order.
TEST(PluginManagerTest, ScanAggregatesFailureReasonsRegardlessOfOrder) {
    std::string dir = "pm_scan_fixture_" + std::to_string(::getpid());
    ASSERT_EQ(::mkdir(dir.c_str(), 0755), 0);
    ASSERT_TRUE(copyFile(kNormalPluginPath, dir + "/a_good.so"));
    ASSERT_TRUE(copyFile(kOldAbiPluginPath, dir + "/b_bad.so"));

    PluginManager pm(PluginManager::makePosixLoader());
    EXPECT_TRUE(pm.loadPluginsFromDirectory(dir));
    EXPECT_NE(pm.lastError().find("b_bad.so"), std::string::npos) << pm.lastError();
    EXPECT_NE(pm.lastError().find("ABI"), std::string::npos) << pm.lastError();
    EXPECT_EQ(pm.getLoadedPlugins().size(), 1u);

    ::remove((dir + "/a_good.so").c_str());
    ::remove((dir + "/b_bad.so").c_str());
    ::rmdir(dir.c_str());
}

// Finding 4 (final review): the no-loader/opendir-fail/no-scan-support
// `return false` paths carried an empty lastError_ — a caller had no way
// to tell "nothing to load" apart from "couldn't even look". Each path now
// sets a reason before returning.
TEST(PluginManagerTest, OpendirFailureSetsLastError) {
    std::string dir = "pm_scan_nonexistent_" + std::to_string(::getpid());
    PluginManager pm(PluginManager::makePosixLoader());
    EXPECT_FALSE(pm.loadPluginsFromDirectory(dir));
    EXPECT_FALSE(pm.lastError().empty());
    EXPECT_NE(pm.lastError().find("opendir"), std::string::npos) << pm.lastError();
}

TEST(PluginManagerTest, NoLoaderConfiguredSetsLastError) {
    PluginManager pm; // default-constructed: no loader
    EXPECT_FALSE(pm.loadPluginsFromDirectory("."));
    EXPECT_EQ(pm.lastError(), "no loader configured");
}
#endif
