# A6 集成测试模块构建清单（拥有者：A6）。
# 目标名 abc_test_integration、测试名 abc_integration_tests 遵循
# docs/interfaces/build.md 第 2 节（abc_ 前缀 + add_test 纳入 CTest）。
#
# 链路：A2 导入器生成索引 → A3 loader → A1 状态机；
# A4 UI 映射在 tests/fcitx5-addon 覆盖；真实 Wayland 客户端仍须手工验收。
#
# 根构建：cmake -S . -B build && cmake --build build && ctest --test-dir build
# 独立构建：cmake -S tests/integration -B build-itest
#           && cmake --build build-itest
#           && ctest --test-dir build-itest --output-on-failure
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-integration-tests CXX)
  enable_testing()
  get_filename_component(ABC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  if(NOT TARGET abc_interfaces)
    add_library(abc_interfaces INTERFACE)
    target_include_directories(abc_interfaces INTERFACE
      "${ABC_SOURCE_ROOT}/source/include")
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
    add_subdirectory("${ABC_SOURCE_ROOT}/source/core" "${CMAKE_BINARY_DIR}/abc-core")
  endif()
  if(NOT TARGET abc_dictionary)
    add_subdirectory("${ABC_SOURCE_ROOT}/source/dictionary"
                     "${CMAKE_BINARY_DIR}/abc-dictionary")
  endif()
  if(NOT TARGET abc_dict_import_core)
    add_subdirectory("${ABC_SOURCE_ROOT}/source/dict-importer"
                     "${CMAKE_BINARY_DIR}/abc-dict-importer")
  endif()
  set(ABC_INTEGRATION_TESTS_ENABLED ON)
else()
  set(ABC_SOURCE_ROOT "${PROJECT_SOURCE_DIR}")
endif()

if(ABC_BUILD_TESTS OR ABC_INTEGRATION_TESTS_ENABLED)
add_executable(abc_test_integration test_chain.cpp)
configure_file(
  "${ABC_SOURCE_ROOT}/tests/dict-importer/fixtures/minimal.dict.yaml"
  "${CMAKE_CURRENT_BINARY_DIR}/a6-minimal.dict.yaml" COPYONLY)
target_include_directories(abc_test_integration
  PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(abc_test_integration PRIVATE
  "${ABC_SOURCE_ROOT}/source/dict-importer")
target_link_libraries(abc_test_integration PRIVATE
  abc_core abc_dictionary abc_dict_import_core abc_build_options)
target_compile_definitions(abc_test_integration PRIVATE
  ABC_A6_FIXTURE_PATH="${CMAKE_CURRENT_BINARY_DIR}/a6-minimal.dict.yaml")

# A5 的 abc_config 已纳入构建时，自动启用 A5→A1 seam 用例。
if(TARGET abc_config)
  target_link_libraries(abc_test_integration PRIVATE abc_config)
  target_compile_definitions(abc_test_integration PRIVATE ABC_HAVE_CONFIG=1)
endif()

add_test(NAME abc_integration_tests COMMAND abc_test_integration)
endif()
