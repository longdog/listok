cmake_minimum_required(VERSION 3.24)

project(leaf-core
  VERSION 1.0.0
  DESCRIPTION "Deterministic leaf asymmetry analysis library"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LEAF_BUILD_TESTS "Build leaf-core unit and integration tests" OFF)
option(LEAF_BUILD_CLI "Build leaf-analyzer CLI" OFF)
option(LEAF_BUILD_BENCHMARKS "Build leaf-core benchmarks" OFF)
option(LEAF_ENABLE_DEBUG "Build desktop-only debug artifacts target" OFF)
option(LEAF_FETCH_DEPS "Fetch nlohmann/json and GoogleTest when not installed" OFF)
option(LEAF_ENABLE_SANITIZERS "Enable ASan+UBSan for supported targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LeafDependencies)
include(LeafSanitizers)

leaf_find_nlohmann_json()
leaf_find_opencv()

add_library(leaf-core
  src/adapters/json/config_json.cpp
  src/adapters/json/result_json.cpp
  src/api/analyzer.cpp
  src/contracts/config.cpp
  src/contracts/version.cpp
  src/detection/center/center_vein_detector.cpp
  src/detection/contour/opencv_leaf_detector.cpp
  src/detection/secondary/keypoint_extractor.cpp
  src/detection/secondary/secondary_vein_detector.cpp
  src/geometry/contour_arc.cpp
  src/geometry/coordinate_system.cpp
  src/geometry/intersection.cpp
  src/geometry/polyline.cpp
  src/image/image.cpp
  src/image/resize.cpp
  src/measurement/extractor.cpp
  src/preprocess/preprocessor.cpp
  src/skeleton/graph.cpp
  src/skeleton/thinning.cpp
  src/statistics/asymmetry.cpp
  src/statistics/batch.cpp
  src/statistics/score.cpp
  src/validation/quality.cpp
)

add_library(leaf::core ALIAS leaf-core)

target_include_directories(leaf-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(leaf-core
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(leaf-core
  PUBLIC
    nlohmann_json::nlohmann_json
  PRIVATE
    opencv_core
    opencv_imgproc
)

target_compile_features(leaf-core PUBLIC cxx_std_20)

add_library(leaf-core-c STATIC src/adapters/c/leaf_c.cpp)
add_library(leaf::core-c ALIAS leaf-core-c)
target_link_libraries(leaf-core-c PUBLIC leaf-core)
target_include_directories(leaf-core-c
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(leaf-core-c PUBLIC cxx_std_20)

if(LEAF_ENABLE_DEBUG)
  add_library(leaf-core-debug STATIC src/debug/debug_renderer.cpp)
  add_library(leaf::core-debug ALIAS leaf-core-debug)
  target_link_libraries(leaf-core-debug PUBLIC leaf-core PRIVATE opencv_imgproc)
  target_include_directories(leaf-core-debug PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/debug>
  )
endif()

if(LEAF_BUILD_CLI)
  add_executable(leaf-analyzer
    tools/leaf-analyzer-cli/main.cpp
    tools/leaf-analyzer-cli/image_io.cpp
  )
  target_include_directories(leaf-analyzer PRIVATE tools/leaf-analyzer-cli)
  target_link_libraries(leaf-analyzer PRIVATE leaf-core opencv_imgcodecs opencv_imgproc)
  if(LEAF_ENABLE_DEBUG)
    target_compile_definitions(leaf-analyzer PRIVATE LEAF_ENABLE_DEBUG=1)
    target_link_libraries(leaf-analyzer PRIVATE leaf-core-debug)
    target_include_directories(leaf-analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/debug)
  endif()
endif()

if(LEAF_BUILD_BENCHMARKS)
  add_executable(leaf-pipeline-benchmark benchmarks/leaf_pipeline_benchmark.cpp)
  target_link_libraries(leaf-pipeline-benchmark PRIVATE leaf-core opencv_core opencv_imgproc)
  target_include_directories(leaf-pipeline-benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/preprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detection/contour
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detection/center
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detection/secondary
    ${CMAKE_CURRENT_SOURCE_DIR}/src/skeleton
    ${CMAKE_CURRENT_SOURCE_DIR}/src/geometry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/measurement
  )
endif()

if(LEAF_BUILD_TESTS)
  enable_testing()
  leaf_find_gtest()
  add_subdirectory(tests)
endif()

if(LEAF_ENABLE_SANITIZERS)
  leaf_enable_sanitizers(leaf-core)
  leaf_enable_sanitizers(leaf-core-c)
  if(TARGET leaf-core-debug)
    leaf_enable_sanitizers(leaf-core-debug)
  endif()
  if(TARGET leaf-analyzer)
    leaf_enable_sanitizers(leaf-analyzer)
  endif()
  if(TARGET leaf-core-tests)
    leaf_enable_sanitizers(leaf-core-tests)
  endif()
  if(TARGET leaf-pipeline-tests)
    leaf_enable_sanitizers(leaf-pipeline-tests)
  endif()
  if(TARGET leaf-determinism-tests)
    leaf_enable_sanitizers(leaf-determinism-tests)
  endif()
  if(TARGET leaf-c-abi-consumer)
    leaf_enable_sanitizers(leaf-c-abi-consumer)
  endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS leaf-core leaf-core-c
  EXPORT leaf-core-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/leaf
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
  cmake/leaf-core-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/leaf-core-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/leaf-core
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/leaf-core-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/leaf-core-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/leaf-core-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/leaf-core
)

install(EXPORT leaf-core-targets
  FILE leaf-core-targets.cmake
  NAMESPACE leaf::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/leaf-core
)
