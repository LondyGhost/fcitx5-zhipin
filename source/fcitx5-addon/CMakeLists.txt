# A4 — fcitx5 addon 适配层构建清单（替换 A0 占位文件）。
# 契约：docs/interfaces/build.md 第 2 节。
#   - find_package(Fcitx5Core) 失败必须 return()，不得让根骨架配置失败。
#   - 目标名 abc_fcitx5_addon；只经 abc_interfaces 使用公共头，链接 abc_core。
#
# 目标机 fcitx5 5.1.23 的公开头使用 std::span / std::ranges，要求 C++20。
# 本目标单独设为 C++20；根清单的全局 C++17 不改（build.md 禁止并行任务改根文件）。
# 纯核心目标不受影响。详见 docs/modules/fcitx5-addon.md 的变更请求记录。

if(NOT TARGET abc_core)
  message(STATUS "abc: abc_core 不存在，跳过 fcitx5 addon（A1 未纳入本次构建）")
  return()
endif()

find_package(Fcitx5Core QUIET)
if(NOT Fcitx5Core_FOUND)
  message(STATUS "abc: 未找到 Fcitx5Core，跳过 fcitx5 addon（本环境无法构建 addon）")
  return()
endif()

# A3/A5 在根构建中均为 addon 的运行期必需模块；缺目标时保持优雅跳过，
# 避免产出看似可安装、实际不具备字典与配置能力的 addon。
if(NOT TARGET abc_dictionary OR NOT TARGET abc_config)
  message(STATUS "abc: abc_dictionary 或 abc_config 不存在，跳过 fcitx5 addon")
  return()
endif()

set(ABC_ADDON_OPTIONAL_LIBS)
list(APPEND ABC_ADDON_OPTIONAL_LIBS abc_config abc_dictionary)

# B 方案：librime 后端可选。存在则链接，addon 内部按 __has_include 自动启用。
if(TARGET abc_rime)
  list(APPEND ABC_ADDON_OPTIONAL_LIBS abc_rime)
endif()

# MODULE 需要链接 A1/A3/A5 静态归档；这些归档若未以 PIC 编译会在
# x86_64/ELF linker 阶段失败。只对 addon 的实际静态依赖目标启用 PIC。
foreach(_abc_pic_target IN ITEMS abc_core abc_dictionary abc_config)
  if(TARGET "${_abc_pic_target}")
    set_target_properties("${_abc_pic_target}" PROPERTIES
      POSITION_INDEPENDENT_CODE ON)
  endif()
endforeach()

add_library(abc_fcitx5_addon MODULE abcengine.cpp keyconv.cpp ui_mapping.cpp)
set_target_properties(abc_fcitx5_addon PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  PREFIX "lib"
  OUTPUT_NAME "abc")
target_link_libraries(abc_fcitx5_addon PRIVATE
  abc_core
  ${ABC_ADDON_OPTIONAL_LIBS}
  Fcitx5::Core
  abc_build_options)
target_compile_definitions(abc_fcitx5_addon PRIVATE FCITX_GETTEXT_DOMAIN="fcitx5-abc")

# 安装只进入调用方给定的 CMAKE_INSTALL_PREFIX。
# 前缀未指定时才落到 $HOME/.local。部署脚本会拒绝 /、/usr、/opt。
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH
        "abc addon 用户级安装前缀" FORCE)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS abc_fcitx5_addon
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/abc-addon.conf"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
  RENAME "abc.conf")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/abc-im.conf"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
  RENAME "abc.conf")

# Rime schema 与 classicui 主题（B 方案运行数据）。
# schema 放 rime-data 共享目录（与 addon 的 sharedDataDir=/usr/share/rime-data 对应）；
# 用户目录同名文件可覆盖系统版。文件在仓库顶层 packaging/，用 PROJECT_SOURCE_DIR 定位。
set(ABC_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging/rime")
if(NOT EXISTS "${ABC_PACKAGING_DIR}/abc.schema.yaml")
  message(STATUS "abc: packaging/rime/abc.schema.yaml 不存在，跳过 schema/主题安装")
  return()
endif()
install(FILES "${ABC_PACKAGING_DIR}/abc.schema.yaml"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/rime-data")
install(FILES "${ABC_PACKAGING_DIR}/abc.theme.conf"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/themes/abc"
  RENAME "theme.conf")
