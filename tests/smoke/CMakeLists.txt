# 接口冒烟测试（A0 拥有）。
# 目的：证明公共契约可编译、可用测试替身实现、头文件依赖方向合规。
# 不测试实现行为（A1–A3 未交付）；实现期符号（工厂/加载器）在骨架期不得被引用。
add_executable(abc_interface_smoke test_interfaces.cpp)
target_link_libraries(abc_interface_smoke
  PRIVATE abc_interfaces abc_build_options)
target_compile_definitions(abc_interface_smoke PRIVATE
  ABC_SOURCE_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../source/include")
add_test(NAME abc_interface_smoke COMMAND abc_interface_smoke)
