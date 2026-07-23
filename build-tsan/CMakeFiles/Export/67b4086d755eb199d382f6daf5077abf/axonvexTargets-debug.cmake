#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "axonvex::core" for configuration "Debug"
set_property(TARGET axonvex::core APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(axonvex::core PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/axonvex_core/libs/libaxonvex_core.so"
  IMPORTED_SONAME_DEBUG "libaxonvex_core.so"
  )

list(APPEND _cmake_import_check_targets axonvex::core )
list(APPEND _cmake_import_check_files_for_axonvex::core "${_IMPORT_PREFIX}/axonvex_core/libs/libaxonvex_core.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
