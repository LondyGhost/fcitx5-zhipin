# A2 Rime 词库导入器模块构建清单（拥有者：A2；替换 A0 占位文件）。
# 目标命名遵循 docs/interfaces/build.md 第 2 节（abc_ 前缀）：
#   abc_dict_import       可执行（CLI 入口，契约规定的模块目标）
#   abc_dict_import_core  静态库（解析/编译核心；供 CLI 与 tests/dict-importer 复用，
#                         目标名以模块表名 abc_dict_import 为前缀）
# 依赖方向（build.md 第 4 节）：仅 abc_interfaces + 标准库；
# 禁止链接/包含 fcitx5、X11、Wayland。
#
# 根构建：根 CMakeLists.txt 按存在性检查自动 add_subdirectory(source/dict-importer)。
# 独立构建：cmake -S source/dict-importer -B build-importer
#           && cmake --build build-importer
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-dict-importer CXX)
  # 独立配置时自建公共目标（根构建时由根文件定义，此处跳过）。
  if(NOT TARGET abc_interfaces)
    add_library(abc_interfaces INTERFACE)
    target_include_directories(abc_interfaces INTERFACE
      "${CMAKE_CURRENT_LIST_DIR}/../include")
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
endif()

add_library(abc_dict_import_core STATIC dict_importer.cpp)
target_link_libraries(abc_dict_import_core PUBLIC abc_interfaces)
target_link_libraries(abc_dict_import_core PRIVATE abc_build_options)
# 模块 API 头（dict_importer.hpp）随库公开给测试等消费方。
target_include_directories(abc_dict_import_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
# addon/测试可能经共享库间接链接：与 abc_core/abc_dictionary 一致启用 PIC。
set_target_properties(abc_dict_import_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(abc_dict_import main.cpp)
target_link_libraries(abc_dict_import PRIVATE
  abc_dict_import_core abc_build_options)
