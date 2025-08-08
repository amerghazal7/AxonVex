# AxonVex Plugins build

# Example plugin (disabled by default). To enable, place sources in src/plugins/examples/
# and add_subdirectory(examples)

# Nothing to build here by default; runtime plugins are shared libraries built out-of-tree.

# Guidance for building a plugin (out-of-tree):
#   add_library(my_plugin SHARED my_plugin.cpp)
#   target_link_libraries(my_plugin PRIVATE axonvex)
#   target_compile_definitions(my_plugin PRIVATE AXONVEX_PLATFORM_LINUX)
#   # Implement extern "C" factory functions:
#   # axonvex::plugins::PluginInterface* axonvex_create_plugin();
#   # void axonvex_destroy_plugin(axonvex::plugins::PluginInterface*);
