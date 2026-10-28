# A6 集成测试模块构建清单（拥有者：A6）。
# 目标名 abc_test_integration、测试名 abc_integration_tests 遵循
# docs/interfaces/build.md 第 2 节（abc_ 前缀 + add_test 纳入 CTest）。
#
# 链路：A0 公共头 →（A5 abc_config，若已纳入构建）→ A1 abc_core →
#       A3 词典接口（契约保真替身；A3 交付后可切换为真实加载器）。
#
# 根构建：cmake -S . -B build && cmake --build build && ctest --test-dir build
# 独立构建：cmake -S tests/integration -B build-itest && cmake --build build-itest
#           && ctest --test-dir build-itest --output-on-failure
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-integration-tests CXX)
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

add_executable(abc_test_integration test_chain.cpp)
target_include_directories(abc_test_integration
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(abc_test_integration PRIVATE abc_core abc_build_options)

# A5 的 abc_config 已纳入构建时，自动启用 A5→A1 seam 用例。
if(TARGET abc_config)
  target_link_libraries(abc_test_integration PRIVATE abc_config)
  target_compile_definitions(abc_test_integration PRIVATE ABC_HAVE_CONFIG=1)
endif()

add_test(NAME abc_integration_tests COMMAND abc_test_integration)
