message("############# AxonVex IO Lib CMake ##################")

add_library(axonvex_io INTERFACE)

set_target_properties(axonvex_io PROPERTIES EXPORT_NAME io)

target_include_directories(axonvex_io
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_io/include>
)

target_link_libraries(axonvex_io
    INTERFACE
        axonvex_core
)

install(TARGETS axonvex_io
        EXPORT axonvexTargets)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_io/include)
