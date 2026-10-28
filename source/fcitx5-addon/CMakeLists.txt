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

# 配置解析（A5）与词典加载（A3）是可选链接：缺席时 addon 使用契约默认值，
# 词典加载符号由运行时按需解析，不阻断 addon 自身的构建。
set(ABC_ADDON_OPTIONAL_LIBS)
if(TARGET abc_config)
  list(APPEND ABC_ADDON_OPTIONAL_LIBS abc_config)
endif()
if(TARGET abc_dictionary)
  list(APPEND ABC_ADDON_OPTIONAL_LIBS abc_dictionary)
endif()

add_library(abc_fcitx5_addon MODULE abcengine.cpp keyconv.cpp ui_mapping.cpp)
set_target_properties(abc_fcitx5_addon PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  PREFIX ""
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
