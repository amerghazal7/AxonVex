message("############# AxonVex Libs CMake ##################")

##################### Build axonvex_core Lib ##########################
add_subdirectory(axonvex_core)

##################### Build axonvex_plugins Lib ##########################
add_subdirectory(axonvex_plugins)

##################### Build axonvex_interfaces Lib ##########################
add_subdirectory(axonvex_interfaces)

##################### Build axonvex_safety Lib ##########################
add_subdirectory(axonvex_safety)

##################### Build axonvex_io Lib ##########################
add_subdirectory(axonvex_io)

##################### Build axonvex_visualization Lib ##########################
add_subdirectory(axonvex_visualization)

##################### Build axonvex_adapters Lib ##########################
add_subdirectory(axonvex_adapters)

##################### Build axonvex_ros2 Plugin Lib (optional) ##########################
add_subdirectory(axonvex_ros2)
