# A6 复验轮次 2：本机执行日志

- 日期：2026-10-29
- 项目提交基线：`11544d3249528dec3e6141eb884f669feefcc2ec` 加未提交工作树改动
- 执行环境：Windows / MinGW g++ 16.1.0；本机无 CMake、Ninja、fcitx5 SDK
- 安全：未访问目标机，未读取凭据；测试数据仅使用仓库内自造最小 fixture。

## A4 可复现测试

以下命令在仓库根目录执行，临时可执行文件写入 `%LOCALAPPDATA%\Temp\opencode`：

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_keyconv.cpp source/fcitx5-addon/keyconv.cpp -o "$LOCALAPPDATA/Temp/opencode/abc_addon_keyconv_a4.exe"
"$LOCALAPPDATA/Temp/opencode/abc_addon_keyconv_a4.exe"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_ui_mapping.cpp source/fcitx5-addon/ui_mapping.cpp -o "$LOCALAPPDATA/Temp/opencode/abc_addon_ui_mapping_a4.exe"
"$LOCALAPPDATA/Temp/opencode/abc_addon_ui_mapping_a4.exe"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_session_driver.cpp source/fcitx5-addon/ui_mapping.cpp source/core/engine_session.cpp -o "$LOCALAPPDATA/Temp/opencode/abc_addon_session_driver_a4.exe"
"$LOCALAPPDATA/Temp/opencode/abc_addon_session_driver_a4.exe"
python tests/fcitx5-addon/test_install_paths.py
```

结果：四项 exit 0，分别输出 `abc_addon_keyconv: ok`、`abc_addon_ui_mapping: ok`、`abc_addon_session_driver: ok`、`abc_addon_install_paths: ok`。这些是 adapter/core 与临时路径测试，不是原生 addon 构建或 Wayland 证据。

## A2→A3→A1 集成测试

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DABC_A6_FIXTURE_PATH='"D:/workbuddy/linux智能ABC/tests/dict-importer/fixtures/minimal.dict.yaml"' \
  -Isource/include -Itests/integration -Isource/dict-importer \
  tests/integration/test_chain.cpp source/dict-importer/dict_importer.cpp \
  source/dictionary/index_loader.cpp source/dictionary/dictionary_index.cpp \
  source/core/engine_session.cpp \
  -o "$LOCALAPPDATA/Temp/opencode/abc_test_integration_a6.exe"
"$LOCALAPPDATA/Temp/opencode/abc_test_integration_a6.exe"
```

结果：`integration: ran 11 cases, 0 failed assertions`，exit 0。该链真实调用 A2 `compileIndexFile`、A3 `loadDictionaryFromFile`/lookup 和 A1 EngineSession；不含原生 A4 动态模块。

再次执行上述 A4 四项与 A6 集成命令：A4 仍 4/4 通过，A6 仍 11/11 通过；`git diff --check` exit 0。

## 未执行

- 根 CMake configure/build/CTest：工具缺失。
- 目标机 ABI 编译、addon discovery、真实 Wayland 客户端按键、安装/卸载/回滚：安全授权与会话访问前置未满足，未执行。
- `git diff --check`：通过。
