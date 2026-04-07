message("############# AxonVex Adapters Lib CMake ##################")

add_library(axonvex_adapters INTERFACE)
set_target_properties(axonvex_adapters PROPERTIES EXPORT_NAME adapters)

target_include_directories(axonvex_adapters
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_adapters/include>
)

target_link_libraries(axonvex_adapters
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_adapters
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_adapters/include)
