# A5 — 部署与配置测试（tests/deploy，A5 拥有，替换 A0 占位）。
# 组成：配置解析 C++ 单元测试、安装/卸载 shell 测试套件（临时 HOME/DESTDIR）、
# 脚本语法检查（bash -n）与 shellcheck（若可用）。
add_executable(abc_config_tests test_config_parser.cpp)
target_link_libraries(abc_config_tests
  PRIVATE abc_config abc_interfaces abc_build_options)
add_test(NAME abc_config_tests COMMAND abc_config_tests)

find_program(ABC_BASH_EXECUTABLE bash)
if(ABC_BASH_EXECUTABLE)
  add_test(NAME abc_deploy_install_uninstall
    COMMAND "${ABC_BASH_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/deploy/test_install_uninstall.sh")
  set_tests_properties(abc_deploy_install_uninstall PROPERTIES
    ENVIRONMENT "ABC_PROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    TIMEOUT 300)

  foreach(_script IN ITEMS build-abc.sh build-staging.sh install-user.sh uninstall-user.sh)
    add_test(NAME abc_bash_syntax_${_script}
      COMMAND "${ABC_BASH_EXECUTABLE}" -n
              "${CMAKE_SOURCE_DIR}/scripts/${_script}")
  endforeach()
endif()

find_program(ABC_SHELLCHECK shellcheck)
if(ABC_SHELLCHECK)
  foreach(_script IN ITEMS build-abc.sh build-staging.sh install-user.sh uninstall-user.sh)
    add_test(NAME abc_shellcheck_${_script}
      COMMAND "${ABC_SHELLCHECK}" "${CMAKE_SOURCE_DIR}/scripts/${_script}")
  endforeach()
endif()
