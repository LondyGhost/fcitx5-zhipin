# A5 — 配置解析库（abc_config）。本文件由 A5 拥有，替换 A0 的空模块占位。
# 依赖方向契约：仅依赖 abc_interfaces（公共头）与标准库；禁止 fcitx5。
# 见 docs/interfaces/build.md 第 2/4 节与 docs/modules/deployment.md。
add_library(abc_config STATIC config.cpp)
target_include_directories(abc_config
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(abc_config PUBLIC abc_interfaces)
target_link_libraries(abc_config PRIVATE abc_build_options)
# A4 addon is a MODULE and links this archive; compile position-independent.
set_target_properties(abc_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
