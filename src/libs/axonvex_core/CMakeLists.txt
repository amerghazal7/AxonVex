message("############# AxonVex Core Lib CMake ##################")

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(TBB REQUIRED)

add_library(axonvex_core SHARED
    src/configuration.cpp
    src/errorHandler.cpp
    src/logger.cpp
    src/path.cpp
    src/ports.cpp
    src/precisionTimer.cpp
    src/processingUnit.cpp
    src/system.cpp
    src/timingController.cpp
)

set_target_properties(axonvex_core PROPERTIES EXPORT_NAME core)

target_include_directories(axonvex_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:axonvex_core/include/axonvex_core>
)

target_link_libraries(axonvex_core
    PUBLIC
        Threads::Threads
        TBB::tbb
        nlohmann_json::nlohmann_json
    PRIVATE
        $<$<PLATFORM_ID:Linux>:rt>
)

install(TARGETS axonvex_core
        EXPORT axonvexTargets
        LIBRARY DESTINATION axonvex_core/libs)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION axonvex_core/include/axonvex_core)
