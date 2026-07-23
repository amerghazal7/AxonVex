# Install script for directory: /home/ag7/Documents/AxonVex/src/libs/axonvex_core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/ag7/Documents/AxonVex/install/libs")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so"
         RPATH "/home/ag7/Documents/AxonVex/install/libs/axonvex_core/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_plugins/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_interfaces/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_safety/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_io/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_visualization/libs")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/axonvex_core/libs" TYPE SHARED_LIBRARY FILES "/home/ag7/Documents/AxonVex/build-tsan/src/libs/axonvex_core/libaxonvex_core.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so")
    file(RPATH_CHANGE
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so"
         OLD_RPATH ":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::"
         NEW_RPATH "/home/ag7/Documents/AxonVex/install/libs/axonvex_core/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_plugins/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_interfaces/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_safety/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_io/libs:/home/ag7/Documents/AxonVex/install/libs/axonvex_visualization/libs")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/axonvex_core/libs/libaxonvex_core.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/axonvex_core/include/axonvex_core" TYPE DIRECTORY FILES "/home/ag7/Documents/AxonVex/src/libs/axonvex_core/include/")
endif()

