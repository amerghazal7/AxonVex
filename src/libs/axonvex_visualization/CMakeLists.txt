message("############# AxonVex Visualization Lib CMake ##################")

add_library(axonvex_visualization INTERFACE)

set_target_properties(axonvex_visualization PROPERTIES EXPORT_NAME visualization)

target_include_directories(axonvex_visualization
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_visualization/include/axonvex_visualization>
)

target_link_libraries(axonvex_visualization
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_visualization
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_visualization/include/axonvex_visualization)
