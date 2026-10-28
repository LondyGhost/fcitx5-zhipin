# A6 本地集成验证日志（轮次 1）

- 日期：2026-10-28
- 验收提交：`a6d95ac`（A1 HEAD；契约基线 `f314103`）
- 执行环境：Windows 10 x64 build 19044，MinGW g++ 16.1.0（无 CMake/Ninja，采用与
  `abc_build_options` 等价的 `-Wall -Wextra -Wpedantic` 直接编译；CMake 路径在目标机复跑，见目标机日志）
- 脱敏声明：无凭据、无 token、无真实用户输入；所有词条为自造测试数据。

## 1. A1 模块自测（独立复跑）

```
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isource/include -Itests/core \
    source/core/engine_session.cpp tests/core/*.cpp -o abc_test_core
./abc_test_core
→ ran 67 test cases, 0 failed assertions   (exit 0)
```

## 2. 集成测试（tests/integration，A6 编写）

变体 A（含 A5 未提交 config 的 seam 用例，ABC_HAVE_CONFIG=1）：

```
g++ -std=c++17 -Wall -Wextra -Wpedantic -DABC_HAVE_CONFIG=1 \
    -Isource/include -Isource/config -Itests/integration \
    source/core/engine_session.cpp source/config/config.cpp \
    tests/integration/test_chain.cpp -o abc_test_integration
./abc_test_integration
→ integration: ran 12 cases, 0 failed assertions   (exit 0)
```

变体 B（仅已提交内容：A0 头 + A1 核心 + 词典契约替身）：

```
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isource/include -Itests/integration \
    source/core/engine_session.cpp tests/integration/test_chain.cpp \
    -o abc_test_integration
./abc_test_integration
→ integration: ran 11 cases, 0 failed assertions   (exit 0)
```

覆盖：配置默认值→会话、工厂越界拒绝、输入→候选→选词全链、翻页/页边界、
Enter 原文/Esc/退格、非处理键提交转发、模式切换双策略、词典错误路径、
页大小配置传播、reset、400 步确定性伪随机走查（核算+不变量+双会话一致性）。

## 3. A5 未提交工作非正式预检（不作为验收结论）

```
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isource/include -Isource/config \
    source/config/config.cpp a5_precheck_driver.cpp -o a5_precheck
./a5_precheck
→ A5 precheck: ALL PASS (informal)  — 19/19 检查点
```

覆盖：默认值、边界值接受、越界回退+警告、BOM/CRLF、未知键/重复键警告、
警告不含值内容（脱敏）、~ 展开、含空格路径。
备注：HOME 存在时 `~/...` 展开在 Windows 产生混合分隔符（`C:\Users\x/...`），
Linux 目标平台无此问题；记录备查。

## 4. 缺陷观察（非正式预检发现，详情见 docs/验收记录.md）

- A3 草稿 `source/dictionary/internal/bytes.hpp`：`readF64` 为占位实现（恒返回 0.0），
  注释引用尚不存在的 `index_loader.cpp`/`dictionary_index.cpp` —— 未交付项。
