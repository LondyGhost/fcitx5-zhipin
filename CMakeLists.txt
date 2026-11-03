# 智拼（ZhiPin）输入法引擎 — 根构建清单（A0 维护）
# 契约：docs/interfaces/build.md（模块注册规则、依赖方向、目标命名）。
# 模块在自己的目录提供 CMakeLists.txt 即自动纳入；禁止并行任务编辑本文件。
cmake_minimum_required(VERSION 3.16)

project(abc_engine
  VERSION 1.0.0
  DESCRIPTION "智拼（ZhiPin）：fcitx5 输入法引擎（经典 ABC 字母输入风格）"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ABC_BUILD_TESTS "构建测试（tests/ 下已注册的模块）" ON)

# 公共接口头文件（source/include/abc，A0 单点维护）。
# 依赖方向契约：所有模块只经 abc_interfaces 使用公共头；纯核心禁止 fcitx5。
add_library(abc_interfaces INTERFACE)
target_include_directories(abc_interfaces INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source/include>")
target_compile_features(abc_interfaces INTERFACE cxx_std_17)

# 统一告警设置：模块目标私有链接继承。
add_library(abc_build_options INTERFACE)
if(MSVC)
  target_compile_options(abc_build_options INTERFACE /W4 /permissive-)
else()
  target_compile_options(abc_build_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

# 模块注册表：目录存在 CMakeLists.txt 才加入（模块可随时补齐，无需改本文件）。
# 新增模块须由 A0/集成负责人登记（build.md 第 2 节）。
set(ABC_SOURCE_MODULES
  core            # A1 核心状态机（abc_core）
  dict-importer   # A2 Rime 词库导入器（abc_dict_import）
  dictionary      # A3 运行时词典与候选检索（abc_dictionary）
  config          # A5 配置解析（abc_config）
  rime            # B 方案：librime 后端（abc_rime），缺 librime 时优雅跳过
  fcitx5-addon    # A4 fcitx5 addon 适配层（自行 find_package，缺依赖时优雅跳过）
)
set(ABC_TEST_MODULES
  smoke           # A0 接口冒烟测试
  core            # A1
  dict-importer   # A2
  dictionary      # A3
  fcitx5-addon    # A4
  deploy          # A5
)

foreach(_mod IN LISTS ABC_SOURCE_MODULES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/source/${_mod}/CMakeLists.txt")
    add_subdirectory("source/${_mod}")
  endif()
endforeach()

if(ABC_BUILD_TESTS)
  foreach(_mod IN LISTS ABC_TEST_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_mod}/CMakeLists.txt")
      add_subdirectory("tests/${_mod}")
    endif()
  endforeach()
endif()
