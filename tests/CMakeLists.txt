include(GoogleTest)

add_executable(leaf-core-tests
  unit/contracts_test.cpp
  unit/geometry_test.cpp
  unit/preprocess_test.cpp
)

target_compile_definitions(leaf-core-tests PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_include_directories(leaf-core-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/preprocess
)

target_link_libraries(leaf-core-tests
  PRIVATE
    leaf-core
    GTest::gtest_main
    opencv_core
    opencv_imgproc
)

gtest_discover_tests(
  leaf-core-tests
  TEST_PREFIX ""
  PROPERTIES LABELS "unit"
)
