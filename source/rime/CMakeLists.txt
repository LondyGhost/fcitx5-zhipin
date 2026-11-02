# B 方案：librime 后端（abc_rime）
#
# 只提供 EngineSession 的 librime 实现，不依赖 fcitx5。
# 缺 librime 时优雅跳过：addon 仍可回退到自研引擎（abc_dictionary）。

if(NOT TARGET abc_core)
  message(STATUS "abc: abc_core 不存在，跳过 rime 模块")
  return()
endif()

find_path(ABC_RIME_INCLUDE_DIR rime_api.h)
find_library(ABC_RIME_LIBRARY NAMES rime)

if(NOT ABC_RIME_INCLUDE_DIR OR NOT ABC_RIME_LIBRARY)
  message(STATUS "abc: 未找到 librime，跳过 rime 模块（addon 将只用自研引擎）")
  return()
endif()

message(STATUS "abc: librime -> ${ABC_RIME_LIBRARY}")

add_library(abc_rime STATIC rime_engine.cpp)
target_link_libraries(abc_rime PUBLIC abc_core)
target_include_directories(abc_rime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${ABC_RIME_INCLUDE_DIR}")
target_link_libraries(abc_rime PUBLIC "${ABC_RIME_LIBRARY}")
target_link_libraries(abc_rime PRIVATE abc_build_options)
set_target_properties(abc_rime PROPERTIES POSITION_INDEPENDENT_CODE ON)
