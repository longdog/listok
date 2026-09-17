include_guard(GLOBAL)

function(leaf_require_package package)
  if(NOT ${package}_FOUND)
    message(FATAL_ERROR
      "Required package '${package}' was not found. "
      "Install the system development package or configure with -DLEAF_FETCH_DEPS=ON.")
  endif()
endfunction()

function(leaf_fetch_nlohmann_json)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
  )
  FetchContent_MakeAvailable(nlohmann_json)
endfunction()

function(leaf_fetch_gtest)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796ef0f878d5eac383202688a8693babaee1ec2c8a3c542b7d5cfb
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endfunction()

function(leaf_find_nlohmann_json)
  find_package(nlohmann_json 3.11.2 QUIET CONFIG)
  if(NOT nlohmann_json_FOUND)
    find_package(nlohmann_json 3.11.2 QUIET)
  endif()
  if(NOT nlohmann_json_FOUND AND LEAF_FETCH_DEPS)
    leaf_fetch_nlohmann_json()
  endif()
  leaf_require_package(nlohmann_json)
endfunction()

function(leaf_find_gtest)
  find_package(GTest 1.14.0 QUIET CONFIG)
  if(NOT GTest_FOUND)
    find_package(GTest 1.14.0 QUIET)
  endif()
  if(NOT GTest_FOUND AND LEAF_FETCH_DEPS)
    leaf_fetch_gtest()
  endif()
  leaf_require_package(GTest)
endfunction()

function(leaf_find_opencv)
  find_package(OpenCV 4.5.5 QUIET COMPONENTS core imgproc)
  if(NOT OpenCV_FOUND)
    find_package(OpenCV 4.5.5 QUIET)
  endif()
  if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
      "Required package 'OpenCV' (>= 4.5.5, major 4) was not found. "
      "Install the system OpenCV development package. OpenCV is always a system dependency.")
  endif()
  if(OpenCV_VERSION_MAJOR GREATER 4)
    message(FATAL_ERROR
      "OpenCV major version ${OpenCV_VERSION_MAJOR} is not supported in leaf-core v1. "
      "Use OpenCV 4.x.")
  endif()
endfunction()
