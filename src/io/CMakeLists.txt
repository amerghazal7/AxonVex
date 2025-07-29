# AxonVex IO Module CMakeLists.txt
# Phase 3A: Foundation Modules - IO Module
# Input/Output operations, file handling, and data streaming
# Dependencies: Core, Utils, Types

set(IO_SOURCES
    files/fileManager.cpp
    streams/dataStream.cpp
    network/networkManager.cpp
    devices/deviceManager.cpp
    protocols/protocolHandler.cpp
)

set(IO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/files/fileManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/streams/dataStream.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/network/networkManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/devices/deviceManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/protocols/protocolHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/axonvex/io/io.hpp
)

# Add IO sources to the main axonvex library
target_sources(axonvex PRIVATE 
    ${IO_SOURCES}
    ${IO_HEADERS}
)

# Define module availability
target_compile_definitions(axonvex 
    PUBLIC
        AXONVEX_IO_MODULE_AVAILABLE
)

# Create source groups for IDE organization
source_group("IO\\Files" FILES
    files/fileManager.cpp
)

source_group("IO\\Streams" FILES
    streams/dataStream.cpp
)

source_group("IO\\Network" FILES
    network/networkManager.cpp
)

source_group("IO\\Devices" FILES
    devices/deviceManager.cpp
)

source_group("IO\\Protocols" FILES
    protocols/protocolHandler.cpp
)

message(STATUS "IO module configured successfully")