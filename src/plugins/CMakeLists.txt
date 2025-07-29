# AxonVex Plugins Module CMakeLists.txt
# Phase 3B: Communication Modules - Plugins Module
# Plugin system for framework extensibility
# Dependencies: Core, Utils, Types

set(PLUGINS_SOURCES
    loader/pluginLoader.cpp
    registry/pluginRegistry.cpp
    api/pluginInterface.cpp
    lifecycle/pluginManager.cpp
)

set(PLUGINS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/plugins/loader/pluginLoader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/plugins/registry/pluginRegistry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/plugins/api/pluginInterface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/plugins/lifecycle/pluginManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/plugins/plugins.hpp
)

# Add plugins sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${PLUGINS_SOURCES}
    ${PLUGINS_HEADERS}
)

# Define module availability
target_compile_definitions(axonvex 
    PUBLIC
        AXONVEX_PLUGINS_MODULE_AVAILABLE
)

# Create source groups for IDE organization
source_group("Plugins\\Loader" FILES
    loader/pluginLoader.cpp
)

source_group("Plugins\\Registry" FILES
    registry/pluginRegistry.cpp
)

source_group("Plugins\\API" FILES
    api/pluginInterface.cpp
)

source_group("Plugins\\Lifecycle" FILES
    lifecycle/pluginManager.cpp
)

message(STATUS "Plugins module configured successfully")
