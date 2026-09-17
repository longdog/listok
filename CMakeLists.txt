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
  src/api/analyzer.cpp
  src/contracts/config.cpp
  src/contracts/version.cpp
  src/geometry/contour_arc.cpp
  src/geometry/coordinate_system.cpp
  src/geometry/intersection.cpp
  src/geometry/polyline.cpp
  src/image/image.cpp
)

add_library(leaf::core ALIAS leaf-core)

target_include_directories(leaf-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(leaf-core
  PUBLIC
    nlohmann_json::nlohmann_json
)

target_compile_features(leaf-core PUBLIC cxx_std_20)

add_library(leaf-core-c INTERFACE)
add_library(leaf::core-c ALIAS leaf-core-c)
target_link_libraries(leaf-core-c INTERFACE leaf-core)
target_include_directories(leaf-core-c
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(LEAF_BUILD_CLI)
  message(STATUS "leaf-analyzer CLI target is added in Task 13")
endif()

if(LEAF_BUILD_TESTS)
  enable_testing()
  leaf_find_gtest()
  add_subdirectory(tests)
endif()

if(LEAF_ENABLE_SANITIZERS)
  leaf_enable_sanitizers(leaf-core)
  if(TARGET leaf-core-tests)
    leaf_enable_sanitizers(leaf-core-tests)
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
