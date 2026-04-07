message("############# AxonVex Safety Lib CMake ##################")

add_library(axonvex_safety INTERFACE)

set_target_properties(axonvex_safety PROPERTIES EXPORT_NAME safety)

target_include_directories(axonvex_safety
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_safety/include/axonvex_safety>
)

target_link_libraries(axonvex_safety
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_safety
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_safety/include/axonvex_safety)
