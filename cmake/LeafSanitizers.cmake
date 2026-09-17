include_guard(GLOBAL)

function(leaf_enable_sanitizers target)
  if(NOT LEAF_ENABLE_SANITIZERS)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "LEAF_ENABLE_SANITIZERS requires GCC or Clang.")
  endif()

  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
