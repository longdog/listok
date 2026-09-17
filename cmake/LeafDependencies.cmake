include_guard(GLOBAL)

function(leaf_require_target target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR
      "Required imported target '${target}' was not found. "
      "Install the system development package or configure with -DLEAF_FETCH_DEPS=ON.")
  endif()
endfunction()

function(leaf_require_nlohmann_json_major)
  if(nlohmann_json_VERSION)
    if(nlohmann_json_VERSION VERSION_LESS 3.11.2)
      message(FATAL_ERROR
        "nlohmann/json version ${nlohmann_json_VERSION} is below the minimum 3.11.2.")
    endif()
    if(nlohmann_json_VERSION VERSION_GREATER_EQUAL 4.0.0)
      message(FATAL_ERROR
        "nlohmann/json major version 4 is not supported in leaf-core v1. Use major 3.")
    endif()
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
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endfunction()

function(leaf_find_nlohmann_json)
  if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.11.2 QUIET CONFIG)
    if(NOT TARGET nlohmann_json::nlohmann_json)
      find_package(nlohmann_json 3.11.2 QUIET)
    endif()
    if(NOT TARGET nlohmann_json::nlohmann_json AND LEAF_FETCH_DEPS)
      leaf_fetch_nlohmann_json()
    endif()
  endif()
  leaf_require_target(nlohmann_json::nlohmann_json)
  leaf_require_nlohmann_json_major()
endfunction()

function(leaf_find_gtest)
  if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main)
    find_package(GTest 1.14.0 QUIET CONFIG)
    if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main)
      find_package(GTest 1.14.0 QUIET)
    endif()
    if(NOT TARGET GTest::gtest AND NOT TARGET GTest::gtest_main AND LEAF_FETCH_DEPS)
      leaf_fetch_gtest()
    endif()
  endif()
  if(TARGET GTest::gtest_main)
    return()
  endif()
  leaf_require_target(GTest::gtest)
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
