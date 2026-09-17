include(GoogleTest)

add_executable(leaf-core-tests
  unit/contracts_test.cpp
)

target_compile_definitions(leaf-core-tests PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(leaf-core-tests
  PRIVATE
    leaf-core
    GTest::gtest_main
)

gtest_discover_tests(
  leaf-core-tests
  TEST_PREFIX "contracts."
  PROPERTIES LABELS "unit;contracts"
)
