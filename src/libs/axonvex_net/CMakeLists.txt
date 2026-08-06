message("############# AxonVex Net Lib CMake ##################")

add_library(axonvex_net SHARED
    src/detail/addressResolver.cpp
    src/tcpClient.cpp
    src/udpSocket.cpp
)

set_target_properties(axonvex_net PROPERTIES EXPORT_NAME net)

target_include_directories(axonvex_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_net/include/axonvex_net>
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
        DESTINATION axonvex_net/include/axonvex_net)
