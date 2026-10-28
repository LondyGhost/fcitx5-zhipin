# A1 核心模块构建清单（拥有者：A1，替换 A0 骨架占位文件）。
# 目标名 abc_core 遵循 docs/interfaces/build.md 第 2 节；仅依赖 abc_interfaces
# 与标准库，禁止 fcitx5/X11/Wayland（依赖方向冻结）。
#
# 根构建：根 CMakeLists.txt 按存在性检查自动 add_subdirectory(source/core)。
# 独立构建：cmake -S source/core -B build-core && cmake --build build-core
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # 独立配置时自建公共目标（根构建时由根文件定义，此处跳过）。
  project(abc-core CXX)
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

add_library(abc_core STATIC engine_session.cpp)
target_link_libraries(abc_core PUBLIC abc_interfaces)
target_link_libraries(abc_core PRIVATE abc_build_options)
# addon（共享库）会链接本静态库：启用 PIC 以免 recompile -fPIC 报错。
set_target_properties(abc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
