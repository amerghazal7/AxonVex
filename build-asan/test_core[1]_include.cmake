if(EXISTS "/home/ag7/Documents/AxonVex/build-asan/test_core")
  if(NOT EXISTS "/home/ag7/Documents/AxonVex/build-asan/test_core[1]_tests.cmake" OR
     NOT "/home/ag7/Documents/AxonVex/build-asan/test_core[1]_tests.cmake" IS_NEWER_THAN "/home/ag7/Documents/AxonVex/build-asan/test_core" OR
     NOT "/home/ag7/Documents/AxonVex/build-asan/test_core[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/local/share/cmake-3.28/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/ag7/Documents/AxonVex/build-asan/test_core]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/ag7/Documents/AxonVex/build-asan]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[test_core_TESTS]==]
      CTEST_FILE [==[/home/ag7/Documents/AxonVex/build-asan/test_core[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[30]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/home/ag7/Documents/AxonVex/build-asan/test_core[1]_tests.cmake")
else()
  add_test(test_core_NOT_BUILT test_core_NOT_BUILT)
endif()
