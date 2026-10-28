# A4 — addon 适配层测试（不链接 fcitx5，Windows 与目标机都能跑）。
# 覆盖：按键转换、preedit 更新、commit、停用清理、未消费键。
# 真实 fcitx5 注册与 Wayland 端到端由目标机构建和 A6 验收。
if(NOT TARGET abc_core)
  message(STATUS "abc: abc_core 不存在，跳过 fcitx5 addon 测试")
  return()
endif()

add_executable(abc_addon_keyconv test_keyconv.cpp
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon/keyconv.cpp")
target_include_directories(abc_addon_keyconv PRIVATE
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon")
target_link_libraries(abc_addon_keyconv PRIVATE abc_interfaces abc_build_options)
add_test(NAME abc_addon_keyconv COMMAND abc_addon_keyconv)

add_executable(abc_addon_ui_mapping test_ui_mapping.cpp
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon/ui_mapping.cpp")
target_include_directories(abc_addon_ui_mapping PRIVATE
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon")
target_link_libraries(abc_addon_ui_mapping PRIVATE abc_interfaces abc_build_options)
add_test(NAME abc_addon_ui_mapping COMMAND abc_addon_ui_mapping)

add_executable(abc_addon_session_driver test_session_driver.cpp
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon/ui_mapping.cpp")
target_include_directories(abc_addon_session_driver PRIVATE
  "${CMAKE_SOURCE_DIR}/source/fcitx5-addon")
target_link_libraries(abc_addon_session_driver PRIVATE
  abc_core abc_interfaces abc_build_options)
add_test(NAME abc_addon_session_driver COMMAND abc_addon_session_driver)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME abc_addon_install_paths
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/test_install_paths.py")
endif()
