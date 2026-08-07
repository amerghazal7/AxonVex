message("############# AxonVex Plugins Build ##################")

file(GLOB plugin_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(dir ${plugin_dirs})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir} AND
       EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
