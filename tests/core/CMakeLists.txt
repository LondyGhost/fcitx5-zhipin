# A1 核心测试模块构建清单（拥有者：A1）。
# 目标名 abc_test_core、测试名 abc_core_tests 遵循 docs/interfaces/build.md
# 第 2 节（abc_ 前缀 + add_test 纳入 CTest）。
#
# 根构建：cmake -S . -B build && cmake --build build && ctest --test-dir build
# 独立构建：cmake -S tests/core -B build-test-core && cmake --build build-test-core
#           && ctest --test-dir build-test-core --output-on-failure
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-core-tests CXX)
  # 独立配置时自建公共目标与核心库（根构建时由根文件/核心模块定义）。
  if(NOT TARGET abc_interfaces)
    add_library(abc_interfaces INTERFACE)
    target_include_directories(abc_interfaces INTERFACE
      "${CMAKE_CURRENT_LIST_DIR}/../../source/include")
    target_compile_features(abc_interfaces INTERFACE cxx_std_17)
  endif()
  if(NOT TARGET abc_build_options)
    add_library(abc_build_options INTERFACE)
    if(MSVC)
      target_compile_options(abc_build_options INTERFACE /W4 /permissive-)
    else()
      target_compile_options(abc_build_options INTERFACE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  if(NOT TARGET abc_core)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../source/core"
                     "${CMAKE_BINARY_DIR}/abc-core")
  endif()
endif()

add_executable(abc_test_core
  test_main.cpp
  test_letters_commit.cpp
  test_candidates.cpp
  test_modes_forward.cpp
  test_factory_reset.cpp
  test_invariants.cpp
)
target_include_directories(abc_test_core PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(abc_test_core PRIVATE abc_core abc_build_options)

add_test(NAME abc_core_tests COMMAND abc_test_core)
