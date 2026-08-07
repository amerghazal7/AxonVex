// Test-bed plugin for PluginManagerTest. Built as four separate MODULE
// targets from this one TU (see root CMakeLists.txt):
//   axonvex_test_plugin           - conforming plugin, correct ABI
//   axonvex_test_plugin_oldabi    - AXONVEX_TEST_PLUGIN_BAD_ABI: wrong ABI version
//   axonvex_test_plugin_nosymbols - AXONVEX_TEST_PLUGIN_NO_SYMBOLS: exports nothing
//   axonvex_test_plugin_throwinit - AXONVEX_TEST_PLUGIN_THROW_INIT: initialize() throws
#include <axonvex_plugins/pluginInterface.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

class TestPlugin : public axonvex::plugins::PluginInterface {
  public:
    std::string name() const override {
        return "test_plugin";
    }
    std::string version() const override {
        return "1.0.0";
    }
    bool initialize() override {
#if defined(AXONVEX_TEST_PLUGIN_THROW_INIT)
        throw std::runtime_error("boom");
#else
        return true;
#endif
    }
    void shutdown() override {}
};

} // namespace

#if !defined(AXONVEX_TEST_PLUGIN_NO_SYMBOLS)

extern "C" axonvex::plugins::PluginInterface* axonvex_create_plugin() {
    return new TestPlugin();
}

extern "C" void axonvex_destroy_plugin(axonvex::plugins::PluginInterface* p) {
    delete p;
}

extern "C" std::uint32_t axonvex_plugin_abi_version() {
#if defined(AXONVEX_TEST_PLUGIN_BAD_ABI)
    return axonvex::plugins::AXONVEX_PLUGIN_ABI_VERSION + 1;
#else
    return axonvex::plugins::AXONVEX_PLUGIN_ABI_VERSION;
#endif
}

#endif // AXONVEX_TEST_PLUGIN_NO_SYMBOLS
