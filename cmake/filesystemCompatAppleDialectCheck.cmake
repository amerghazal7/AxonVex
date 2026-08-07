# Regression check for filesystem_compat.hpp's Apple branch (review blocker:
# selecting the experimental-vs-standardized filesystem header by
# __has_include() presence alone is unsafe on Apple, whose libc++ replaces
# the dropped <experimental/filesystem> with a header that exists but whose
# whole body is `#error`). Run via `cmake -P` (see the add_test() call in the
# root CMakeLists.txt) rather than as a compiled GTest case: this is a
# preprocessor-branch-selection question, host-independent -- it forces
# __APPLE__ itself, so it exercises the Apple path on any host with a C++
# compiler, no Apple toolchain required.
#
# Required inputs (-D on the cmake -P invocation):
#   AXONVEX_CXX            compiler to invoke (same one the build uses)
#   AXONVEX_CORE_INCLUDE    axonvex_core's public include dir
#   AXONVEX_STUB_INCLUDE    dir containing the fixture's experimental/filesystem stub
#   AXONVEX_PROBE_SRC       the probe.cpp fixture

foreach(_required AXONVEX_CXX AXONVEX_CORE_INCLUDE AXONVEX_STUB_INCLUDE AXONVEX_PROBE_SRC)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "filesystemCompatAppleDialectCheck.cmake: missing required -D${_required}")
    endif()
endforeach()

# Scenario 1: simulated Apple, C++17, and the poisoned experimental/filesystem
# on the include path. Must succeed by never even probing/including the
# stub -- proves the fix goes straight to <filesystem> on Apple instead of
# trusting __has_include() presence.
execute_process(
    COMMAND ${AXONVEX_CXX} -D__APPLE__ -std=c++17
            -I ${AXONVEX_STUB_INCLUDE} -I ${AXONVEX_CORE_INCLUDE}
            -fsyntax-only ${AXONVEX_PROBE_SRC}
    RESULT_VARIABLE _r1
    OUTPUT_VARIABLE _o1
    ERROR_VARIABLE _e1
)
if(NOT _r1 EQUAL 0)
    message(FATAL_ERROR
        "filesystem_compat.hpp regression: simulated Apple + C++17 + poisoned "
        "<experimental/filesystem> failed to compile (should bypass the stub "
        "entirely and use <filesystem>). Compiler output:\n${_o1}\n${_e1}")
endif()

# Scenario 2: simulated Apple, but the dialect requirement did NOT reach this
# TU (still C++14). Must fail, and fail with the named diagnostic -- not the
# generic "'filesystem' is not a namespace-name" the pre-fix code produced.
execute_process(
    COMMAND ${AXONVEX_CXX} -D__APPLE__ -std=c++14
            -I ${AXONVEX_CORE_INCLUDE}
            -fsyntax-only ${AXONVEX_PROBE_SRC}
    RESULT_VARIABLE _r2
    OUTPUT_VARIABLE _o2
    ERROR_VARIABLE _e2
)
if(_r2 EQUAL 0)
    message(FATAL_ERROR
        "filesystem_compat.hpp regression: simulated Apple + C++14 compiled "
        "successfully -- it must fail, naming the missing C++17 requirement.")
endif()
if(NOT _e2 MATCHES "AXONVEX_FS_NEEDS_CXX17_ON_APPLE")
    message(FATAL_ERROR
        "filesystem_compat.hpp regression: simulated Apple + C++14 failed as "
        "expected, but not with the named AXONVEX_FS_NEEDS_CXX17_ON_APPLE "
        "diagnostic -- got:\n${_o2}\n${_e2}")
endif()

# Scenario 3: no __APPLE__ (this host's real platform), C++14 -- must be
# completely unaffected: still resolves via <experimental/filesystem> as
# before the fix. Guards against the fix regressing every non-Apple platform,
# including this codebase's own gnu++17 TUs (ROS 2 plugin) whose axonvex_fs
# resolution must keep matching axonvex_core.so's build dialect (ABI).
execute_process(
    COMMAND ${AXONVEX_CXX} -std=c++14
            -I ${AXONVEX_CORE_INCLUDE}
            -fsyntax-only ${AXONVEX_PROBE_SRC}
    RESULT_VARIABLE _r3
    OUTPUT_VARIABLE _o3
    ERROR_VARIABLE _e3
)
if(NOT _r3 EQUAL 0)
    message(FATAL_ERROR
        "filesystem_compat.hpp regression: the non-Apple, C++14 branch (this "
        "host's real platform) no longer compiles. Compiler output:\n${_o3}\n${_e3}")
endif()

message(STATUS "filesystemCompatAppleDialectCheck: all 3 scenarios passed")
