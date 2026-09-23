include(GoogleTest)

add_executable(leaf-core-tests
  support/fixture_factory.cpp
  support/golden_compare.cpp
  unit/contracts_test.cpp
  unit/center_vein_test.cpp
  unit/contour_test.cpp
  unit/geometry_test.cpp
  unit/json_test.cpp
  unit/measurement_test.cpp
  unit/preprocess_test.cpp
  unit/quality_analyzer_test.cpp
  unit/secondary_keypoints_test.cpp
  unit/statistics_test.cpp
  unit/thinning_graph_test.cpp
  integration/golden_test.cpp
)

target_compile_definitions(leaf-core-tests PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_include_directories(leaf-core-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/detection/center
  ${CMAKE_SOURCE_DIR}/src/detection/contour
  ${CMAKE_SOURCE_DIR}/src/detection/secondary
  ${CMAKE_SOURCE_DIR}/src/geometry
  ${CMAKE_SOURCE_DIR}/src/measurement
  ${CMAKE_SOURCE_DIR}/src/preprocess
  ${CMAKE_SOURCE_DIR}/src/validation
  ${CMAKE_SOURCE_DIR}/src/statistics
  ${CMAKE_SOURCE_DIR}/src/skeleton
  ${CMAKE_SOURCE_DIR}/tests/support
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

add_executable(leaf-pipeline-tests integration/pipeline_test.cpp)
target_compile_definitions(leaf-pipeline-tests PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(leaf-pipeline-tests PRIVATE leaf-core GTest::gtest_main)
gtest_discover_tests(leaf-pipeline-tests PROPERTIES LABELS "integration")

add_executable(leaf-determinism-tests integration/determinism_test.cpp)
target_compile_definitions(leaf-determinism-tests PRIVATE CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(leaf-determinism-tests PRIVATE leaf-core GTest::gtest_main)
gtest_discover_tests(leaf-determinism-tests PROPERTIES LABELS "determinism")

add_executable(leaf-c-abi-consumer integration/c_abi_consumer.c)
target_link_libraries(leaf-c-abi-consumer PRIVATE leaf-core-c)
set_target_properties(leaf-c-abi-consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX
)
target_compile_definitions(leaf-c-abi-consumer PRIVATE
  LEAF_CROSS_PPM="${CMAKE_SOURCE_DIR}/tests/fixtures/cli/leaf_cross.ppm"
  LEAF_CROSS_CONFIG="${CMAKE_SOURCE_DIR}/tests/fixtures/cli/leaf_cross.config.json"
)
add_test(NAME c_abi_consumer COMMAND leaf-c-abi-consumer)
set_tests_properties(c_abi_consumer PROPERTIES LABELS "c_abi")

if(LEAF_BUILD_CLI)
  add_test(NAME cli_smoke
    COMMAND ${CMAKE_COMMAND}
      -DANALYZER=$<TARGET_FILE:leaf-analyzer>
      -DFIXTURE=${CMAKE_SOURCE_DIR}/tests/fixtures/cli/leaf_cross.png
      -DCONFIG=${CMAKE_SOURCE_DIR}/tests/fixtures/cli/leaf_cross.config.json
      -DCORRUPT=${CMAKE_SOURCE_DIR}/tests/fixtures/cli/corrupt.jpg
      -DDEBUG_ENABLED=$<IF:$<BOOL:${LEAF_ENABLE_DEBUG}>,ON,OFF>
      -P ${CMAKE_SOURCE_DIR}/tests/integration/cli_smoke.cmake
  )
  set_tests_properties(cli_smoke PROPERTIES LABELS "cli")
endif()
