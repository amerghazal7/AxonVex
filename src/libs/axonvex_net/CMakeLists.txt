message("############# AxonVex Net Lib CMake ##################")

add_library(axonvex_net SHARED
    src/detail/addressResolver.cpp
    src/tcpClient.cpp
    src/udpSocket.cpp
)

set_target_properties(axonvex_net PROPERTIES
    EXPORT_NAME net
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    # Only axonvex_net gets hidden-by-default visibility for now: its public
    # surface (TcpClient, UdpSocket) is small enough to annotate directly
    # below. axonvex_core's equivalent classes (System, Ports, TimingController,
    # UnitFactory, SystemSpec) live in headers locked to concurrent phase3
    # branches (decomposition/scheduler-rt/spec-instantiation) for the
    # duration of this fix — annotating them is a follow-up once those land.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

include(GenerateExportHeader)
generate_export_header(axonvex_net
    EXPORT_MACRO_NAME AXONVEX_NET_API
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/axonvex_net/net_export.hpp"
)

target_include_directories(axonvex_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_net/include>
)

# Layering: net sits above core and uses the ProtocolInterface contract (and
# DispatchBarrier) from axonvex_interfaces; core itself depends on nothing
# internal.
target_link_libraries(axonvex_net
    PUBLIC
        axonvex_core
        axonvex_interfaces
)

install(TARGETS axonvex_net
        EXPORT axonvexTargets
        LIBRARY DESTINATION axonvex_net/libs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_net/include)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/axonvex_net/net_export.hpp"
        DESTINATION axonvex_net/include/axonvex_net)
