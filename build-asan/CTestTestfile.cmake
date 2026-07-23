# CMake generated Testfile for 
# Source directory: /home/ag7/Documents/AxonVex
# Build directory: /home/ag7/Documents/AxonVex/build-asan
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/home/ag7/Documents/AxonVex/build-asan/test_core[1]_include.cmake")
add_test([=[core_tests]=] "/home/ag7/Documents/AxonVex/build-asan/test_core")
set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/ag7/Documents/AxonVex/CMakeLists.txt;124;add_test;/home/ag7/Documents/AxonVex/CMakeLists.txt;0;")
subdirs("src/libs")
subdirs("plugins")
subdirs("examples")
