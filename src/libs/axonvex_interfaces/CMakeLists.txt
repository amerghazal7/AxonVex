message("############# AxonVex Interfaces Lib CMake ##################")

add_library(axonvex_interfaces INTERFACE)

set_target_properties(axonvex_interfaces PROPERTIES EXPORT_NAME interfaces)

target_include_directories(axonvex_interfaces
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_interfaces/include>
)

target_link_libraries(axonvex_interfaces
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_interfaces
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_interfaces/include)
