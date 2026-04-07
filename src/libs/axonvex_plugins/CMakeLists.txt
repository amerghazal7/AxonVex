message("############# AxonVex Plugins Lib CMake ##################")

add_library(axonvex_plugins INTERFACE)

set_target_properties(axonvex_plugins PROPERTIES EXPORT_NAME plugins)

target_include_directories(axonvex_plugins
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_plugins/include/axonvex_plugins>
)

target_link_libraries(axonvex_plugins
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_plugins
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_plugins/include/axonvex_plugins)
