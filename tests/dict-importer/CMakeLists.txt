# A2 导入器测试与交叉验证构建清单（拥有者：A2）。
# 目标名 abc_test_dict_import、测试名 abc_dict_import_tests 遵循
# docs/interfaces/build.md 第 2 节。
#
# 链接说明（build.md 第 4 节允许的跨模块复用方式）：
#   - abc_dict_import_core：A2 解析/编译核心（本模块）；
#   - abc_dictionary：A3 只读 loader —— 用于“A2 生成索引 → A3 加载查询”
#     交叉验证（test_cross.cpp）；不修改 A3 的任何文件。
#
# 测试参数：argv[1] = fixtures 目录；argv[2] = abc_dict_import 可执行路径
# （e2e 用例需要；缺省时 e2e 用例自动跳过并在输出中标注 SKIP）。
#
# 根构建：cmake -S . -B build && cmake --build build
#           && ctest --test-dir build -R abc_dict_import_tests -V
# 独立构建：cmake -S tests/dict-importer -B build-test-importer
#           && cmake --build build-test-importer
#           && ctest --test-dir build-test-importer --output-on-failure
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-dict-importer-tests CXX)
  enable_testing()
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
  if(NOT TARGET abc_dict_import_core)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../source/dict-importer"
                     "${CMAKE_BINARY_DIR}/abc-dict-importer")
  endif()
  if(NOT TARGET abc_dictionary)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../source/dictionary"
                     "${CMAKE_BINARY_DIR}/abc-dictionary")
  endif()
endif()

add_executable(abc_test_dict_import
  test_main.cpp
  test_parse.cpp
  test_compile.cpp
  test_cli.cpp
  test_cross.cpp
)
target_include_directories(abc_test_dict_import PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(abc_test_dict_import PRIVATE
  abc_dict_import_core abc_dictionary abc_build_options)
add_dependencies(abc_test_dict_import abc_dict_import)

add_test(
  NAME abc_dict_import_tests
  COMMAND abc_test_dict_import
          "${CMAKE_CURRENT_LIST_DIR}/fixtures"
          "$<TARGET_FILE:abc_dict_import>"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
