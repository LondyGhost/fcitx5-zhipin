# fcitx5 addon 适配层

A4 拥有本模块。接口契约版本 1.0.0（rc1，见 `docs/interfaces/`）。

## 当前基线复核（2026-10-28）

- 本轮开始基线：`a037b8132ef17ea9782e3366f3adfa6a06779f9f`。
- A4 既有实现提交：`de638416ac958990560a3e826eb170237d097489`；既有交付记录提交：
  `2c8a4e753a7fa280432ca363c45c96682c771375`。本轮 A4 修复提交全号将在验证后登记。
- 仓库 git author 显示 A4/A1/A3/A5 交付均为 `DNP Dev <dev@dnp.local>`；仓库中没有
  新 A4 负责人身份变更记录，因此不能据此确认组织层面的负责人姓名。当前下游提交：
  A1 `a6d95ac70dd1822244cb9d50d5718e21e63cecb6`，A3
  `a037b8132ef17ea9782e3366f3adfa6a06779f9f`，A5
  `45d5cb70a7a835359bb6ccae55d63112233a2591`。A1 接口为 1.0.0-rc1；A3 已实现
  `loadDictionaryFromFile`；A5 已交付配置解析和正式部署入口。此次只读仓库核对未进行
  仓库外通知或协作消息。
- 接口契约为 **1.0.0-rc1**。目标 fcitx5 是 **5.1.23**，动态 ABI 依赖
  `Fcitx5Core` / `Fcitx5Config` / `Fcitx5Utils` 对应 ABI `libFcitx5Core.so.7`、
  `libFcitx5Config.so.6`、`libFcitx5Utils.so.2`；构建 addon 目标局部使用 C++20，核心和
  根项目继续 C++17。已知并验证范围只有 fcitx5 5.1.23；其他 5.1.x/ABI 主版本尚未
  验证，升级后需重新构建、注册和运行生命周期回归。
- 本轮代码修复：`source/fcitx5-addon/abcengine.cpp`、`keyconv.h`、`ui_mapping.cpp`；
  测试修复：`tests/fcitx5-addon/test_keyconv.cpp`、`test_ui_mapping.cpp`、
  `test_session_driver.cpp`；另更新本模块文档。未改 A0–A3/A5/A6 文件。
- 复核发现 `preeditInClient=false` 的映射语义曾与测试预期冲突。现映射为空文本，
  addon 同时清空客户端与面板 preedit；焦点丢失后
  `deactivate()` 调用 session reset，清空面板和会话、不提交未提交输入；属性析构也
  清理 UI 后释放上下文会话。左右 Shift 现分别按对应 fcitx5 modifier keysym 检查释放，
  避免默认热键只认左侧的问题。
- A4 替身/核心驱动测试不会声称是真实 fcitx5 集成。它们验证 KeyCommand 转换、effects
  顺序、preedit/candidate/highlight 映射、候选数字/空格选择、模式切换、未消费键、多个
  会话隔离和 reset。真实 fcitx5 5.1.23 ABI 构建及真实 Wayland 应用测试本轮未运行；旧
  交付记录中的目标机结果来自旧提交，不能替代对当前提交的干净复跑。目标机本轮 SSH
  BatchMode 检查因 `Host key verification failed` 失败；没有读取或输出仓库外口令。
- 本轮 A4 核心测试命令及结果见本节“本轮本机复现结果”。Windows 环境未发现 CMake
  或 Ninja，故本机不能给出干净 CMake/CTest 或 fcitx5 addon 动态模块构建结果。
- 本次复核实际以 MinGW g++ 16.1.0、Python 3.14.6 运行 A4 替身/核心测试：
  `test_keyconv`、`test_ui_mapping`、`test_session_driver` 均编译成功并输出 `ok`；
  `python tests/fcitx5-addon/test_install_paths.py` 输出 `abc_addon_install_paths: ok`。
  临时可执行文件位于 `C:\Users\DELL\AppData\Local\Temp\opencode\`。这不是
  CMake/CTest、fcitx5 SDK 构建或目标机验证结果。
- **正式用户级安装入口只有** `scripts/install-user.sh`，卸载/恢复入口为
  `scripts/uninstall-user.sh`（A5 所有）；A4 的
  `deploy/fcitx5-addon/install-user.sh` 与 `uninstall-user.py` 仅用于隔离临时前缀的
  A4 脚本自测，不是正式部署入口。它使用 `.abc-im/manifest.list`，与 A5 的
  `.abc-im/manifest.tsv` 不兼容；正式部署前若存在旧 A4 manifest，须先用 A4 自测卸载器
  清理临时安装，再使用 A5 唯一入口。
- fcitx5 5.1.23 的正式查找设置由 A5 `scripts/install-user.sh --print-env` 只读生成：
  它保留并前置用户已有 `FCITX_ADDON_DIRS` 内容，不覆盖系统 `/usr/lib/fcitx5`，不写
  `environment.d`、shell 配置或当前会话。需先检查用户现有 `FCITX_ADDON_DIRS` 及
  `~/.config/environment.d/` 设置，再由用户选择把输出接入 fcitx5 会话环境；设置后重启
  fcitx5 并用 `fcitx5-diagnose` 确认 addon 路径。不得直接套用 A4 文档旧的 export 示例。
- 目标机 CatOS / x86_64 / kernel 7.2.11-arch1-1 / GCC 16.2.1 / CMake 4.4.4 /
  Ninja 1.13.2 / fcitx5 5.1.23 信息来自既有 `docs/目标机环境.md`，不是本轮复测结果。
  完整目标机复跑、Wayland 真实应用、运行环境冲突检查与日志归档仍待完成；脱敏日志
  路径：**待本轮目标机复测后由 A4 提供给 A6**。

## 职责

把 fcitx5 `KeyEvent` 转成 `abc::KeyCommand`，调用 `EngineSession::handle`，
再把 `EngineResult` 按 `engine.md` 4.4 的顺序写成 fcitx5 的 commit、preedit
和候选列表。

适配层不做这些事：

- 不保存第二份输入缓冲。缓冲只在 `abc::EngineSession` 里，每个输入上下文一个会话。
- 不解析词库，不排序候选。候选顺序和高亮原样来自核心快照。
- 不实现 Wayland 文本协议。Wayland 由 fcitx5 的前端负责。

## 文件

| 文件 | 作用 |
|---|---|
| `source/fcitx5-addon/keyconv.h` / `keyconv.cpp` | `engine.md` 2.1 转换表。不包含 fcitx5 头 |
| `source/fcitx5-addon/ui_mapping.h` / `ui_mapping.cpp` | effects 与快照到 UI 动作的顺序。不包含 fcitx5 头 |
| `source/fcitx5-addon/abcengine.h` / `abcengine.cpp` | `InputMethodEngine`、每上下文状态、fcitx5 UI 调用 |
| `source/fcitx5-addon/abc-addon.conf` | addon 描述，安装为 `addon/abc.conf` |
| `source/fcitx5-addon/abc-im.conf` | 输入法条目，安装为 `inputmethod/abc.conf` |
| `source/fcitx5-addon/CMakeLists.txt` | 仅在找到 `Fcitx5Core` 且 `abc_core` 存在时构建 |

## 按键转换

`keyconv.cpp` 是 `engine.md` 2.1 的唯一实现：

- `a..z` / `A..Z` → `kCharacter`，统一成小写码点。
- `1..9` 与小键盘 `KP_1..KP_9` → `kDigit`。
- `0`、`KP_0` 和其他无修饰可打印 ASCII → `kPunctuation`。
- BackSpace、Return、KP_Enter、space、Escape、Prior/KP_Prior、Next/KP_Next
  按契约映射。
- Ctrl、Alt、Super、Super2、Meta 下的可打印键 → `kUnknown`，不进缓冲。
- 其余键（方向键、Tab、功能键、按键释放）→ `kUnknown`。

`modeSwitchKey` 优先于转换表。配置值 `Shift`、`Control`、`Alt`、`Super`
（含左右键名）在对应修饰键释放时产出 `kModeSwitch`。识别用
`fcitx::Key::isReleaseOfModifier` 分别匹配左右修饰键释放，因此左右 Shift 都能命中默认
`Shift`；本轮补充两侧回归测试。
无法识别的配置值关闭切换，不吞键。

## UI 与消费

`mapResult` 决定动作顺序，`AbcState::apply` 执行同一序列：

1. `kCommitText` → `InputContext::commitString`。空文本不提交。
2. `kClearComposition` → 清空 preedit 与候选。
3. 快照 preedit → `InputPanel::setPreedit`；`preeditInClient` 为真时同步
   `setClientPreedit`。
4. 快照候选 → `CommonCandidateList`，标签固定 `1.` 到 `9.`，高亮用
   `setGlobalCursorIndex`。空候选则清空列表。

`EngineResult.consumed == false` 时不调用 `KeyEvent::filterAndAccept`，
fcitx5 会把键转发给应用。effects 仍然执行，所以“先提交原文再转发”不会丢字。

`preeditInClient=false` 清空 client preedit，并隐藏面板预编辑文本；候选面板仍按
核心快照更新。焦点丢失或 reset 仍调用核心 `reset()`，清除两个 UI 目标和候选而不提交。

候选词用 `DisplayOnlyCandidateWord`。数字选择、空格和翻页都由核心处理；
点击候选不提交，避免在适配层做选择逻辑。

## 生命周期

目标机 fcitx5 5.1.23 的行为（已对照该版本源码）：

- 输入上下文销毁没有单独的引擎回调。会话放在 `InputContextProperty` 里，
  属性析构时清空面板并释放会话，不提交。
- `focusOut` 不调用 `engine->reset()`。核心会先提交 client preedit，再调用
  `deactivate()`。`deactivate` 调用 `session->reset()`，清掉核心缓冲和面板，
  避免切回时看到陈旧 preedit。
- `ResetEvent` 由核心转成 `engine->reset()`，同样调用 `session->reset()`。
- `activate` 用 `snapshot()` 重画，不新开缓冲。

会话只在拥有该上下文的 fcitx5 事件线程上使用，符合 `engine.md` 第 7 节。

## 配置与词典

构建时要求 `abc_config` 与 `abc_dictionary` 均存在并链接；addon 读取
`$XDG_CONFIG_HOME/abc/config.ini`，否则 `$HOME/.config/abc/config.ini`。
解析失败由 A5 回退默认值。缺少 A3/A5 时跳过 addon 构建，避免生成不具备词典能力的半成品。

`indexPath` 交给 `abc::loadDictionaryFromFile`。索引缺失或损坏时记一条
不含原文的警告，引擎仍加载；按键不消费，应用照常收到字符。

## 构建

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

`Fcitx5Core` 缺失或 `abc_core` 未纳入时，本目录 `return()`，根配置继续成功。

目标机 fcitx5 5.1.23 的公开头使用 `std::span` 和 `std::ranges`，addon 目标
因此单独设为 C++20。根清单的全局 C++17 未改。纯核心目标仍是 C++17。
这是 A4 适配目标对 fcitx5 5.1.23 头文件的局部编译要求，不改变 A0 公共契约或根
项目 C++17 设置。当前只有 5.1.23 的既有构建记录；其他版本没有在本轮验证。

安装前缀默认是 `$HOME/.local`。部署脚本拒绝 `/`、`/usr`、`/opt`。

## 测试

| 测试 | 覆盖 |
|---|---|
| `abc_addon_keyconv` | 转换表、修饰键、左右 Shift 默认热键释放、未识别键 |
| `abc_addon_ui_mapping` | client preedit 开/关、面板 preedit、commit 顺序、未消费键 effects、reset 清理 |
| `abc_addon_session_driver` | A1 `EngineSession` 的 preedit、数字/空格选词、高亮、模式切换、独立会话、失焦 reset、未消费标点 |
| `abc_addon_install_paths` | 临时前缀安装、清单、系统路径拒绝、卸载与备份恢复 |

这些测试不链接 fcitx5，不能代替 A6 的 Wayland 端到端验收。

## 注册

正式部署由 A5 唯一入口 `scripts/install-user.sh` 管理；卸载/备份恢复由
`scripts/uninstall-user.sh` 管理。安装布局与环境处理见 `docs/modules/deployment.md`
第 3 节。A5 的只读 `scripts/install-user.sh --print-env` 会为 fcitx5 输出建议的
`FCITX_ADDON_DIRS`，保留现有值并把本项目 addon 目录前置，不会改写会话配置。
确认 `~/.config/environment.d/` 等用户现有环境设置后，才由用户决定如何将建议值
提供给 fcitx5；不要用旧固定 `export` 示例覆盖已有变量。

设置好会话变量后重启用户级 fcitx5，并用 `fcitx5-diagnose` 确认 addon 列表与库路径。
部署路径不得进入 `/usr` 或 `/opt`。

`deploy/fcitx5-addon/install-user.sh` 与 `uninstall-user.py` 是 A4 早期的隔离前缀
自测工具，只供 `tests/fcitx5-addon/test_install_paths.py` 临时目录测试；它生成的
`.abc-im/manifest.list` 与 A5 正式入口的 `.abc-im/manifest.tsv` 不兼容，不得用于正式
用户安装。正式路径唯一为 A5 脚本。

## 已知限制

- 点击候选不提交。选择只走数字键和空格。
- `modeSwitchKey` 只支持 Shift、Control、Alt、Super 及其左右键名。
- 索引不可用时 addon 仍注册，但按键全部转发。
- 本机未安装 CMake/Ninja/fcitx5 SDK；目标机本轮因 SSH host key 校验失败，未复跑
  fcitx5 模块编译、注册发现和目标机自动测试。
- 真实 Wayland 应用验收不属于本模块，由 A6 执行。
- X11 或本模块的模拟测试不能作为 Wayland 通过证据。

## 交付记录

| 项 | 值 |
|---|---|
| 任务 | A4 fcitx5 addon 适配层 |
| 提交 | `de63841`（分支 `master`） |
| 接口契约 | 1.0.0（rc1，未改公共头） |
| fcitx5 | 5.1.23（`pkg-config --modversion Fcitx5Core`） |
| API | `Fcitx5Core` / `Fcitx5Utils` 5.1.23，C++20 |
| 编译器 | 目标机 GCC 16.2.1，CMake 4.4.4，Ninja 1.13.2 |
| 系统 | CatOS（Arch 系）x86_64，内核 7.2.11-arch1-1 |

### 变更文件

只含 A4 所有权路径，共 18 个文件：

- `source/fcitx5-addon/`：`CMakeLists.txt`、`abcengine.h`、`abcengine.cpp`、
  `keyconv.h`、`keyconv.cpp`、`ui_mapping.h`、`ui_mapping.cpp`、
  `abc-addon.conf`、`abc-im.conf`
- `tests/fcitx5-addon/`：`CMakeLists.txt`、`test_keyconv.cpp`、
  `test_ui_mapping.cpp`、`test_session_driver.cpp`、`test_install_paths.py`
- `docs/modules/fcitx5-addon.md`
- `deploy/fcitx5-addon/`：`install-user.sh`、`uninstall-user.py`、
  `install-manifest.md`

本轮排查追加的构建接线变更超出 A4 原有所有权：根 `CMakeLists.txt` 调整 A5/A4 顺序，
A5 配置库启用 PIC，A0 build 契约登记 A4→A3/A5 静态链接。需各模块负责人确认后方可合并；
此前“未改根文件和其他任务文件”仅描述原 A4 交付，不适用于本轮工作树。

### 构建与测试

目标机 `~/abc-work/src`，2026-10-28：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target abc_fcitx5_addon abc_addon_keyconv \
  abc_addon_ui_mapping abc_addon_session_driver
ctest --test-dir build -R abc_addon --output-on-failure
```

结果：4/4 通过（`abc_addon_keyconv`、`abc_addon_ui_mapping`、
`abc_addon_session_driver`、`abc_addon_install_paths`）。

`libabc.so` 链接 `libFcitx5Core.so.7`、`libFcitx5Config.so.6`、
`libFcitx5Utils.so.2`。源码检索无 XIM、XTest、xdotool、`XSendEvent`。

### 注册

隔离 `HOME=/home/londy/abc-work/fcitx-home` 启动 fcitx5 5.1.23，
`FCITX_ADDON_DIRS` 指向 `/home/londy/abc-work/prefix/lib/fcitx5:/usr/lib/fcitx5`，
`FCITX_DATA_DIRS` 指向该前缀的 `share` 加 `/usr/share`。

- `AvailableInputMethods` 含 `abc` / 智能ABC / zh_CN。
- `AddonForIM("abc")` 返回 `abc`。
- `GetAddonsV2` 含 addon `abc`，依赖 `core`。
- 探测进程已停止，未触碰用户日常会话。

### 安装路径

`cmake --install build --prefix /home/londy/abc-work/cmake-prefix` 只写出：

- `lib/fcitx5/libabc.so`
- `share/fcitx5/addon/abc.conf`
- `share/fcitx5/inputmethod/abc.conf`

`/usr`、`/opt` 下无 `libabc.so` 或 `abc.conf`。
`install-user.sh` 在临时前缀下通过安装、拒绝系统路径、卸载和备份恢复测试。

### 自查

- 无 XIM、XTest、xdotool 或模拟按键注入。
- 无 `/opt` 安装，不覆盖系统 `/usr`。
- 仓库和本记录不含部署凭据、真实用户输入或 token。
- Wayland 端到端不在本任务内，不能把本次注册结果当成 A6 通过。

### 给集成负责人

1. 历史 A4 提交 `de63841` 曾报告在目标机 fcitx5 5.1.23 构建/注册通过；它不是当前
   HEAD 的干净复跑证据。本轮修改的 native `abcengine.cpp` 尚待目标机重新编译。
2. A3 当前已交付 `dictionary_index.cpp`。本轮 HEAD 的 A3 加入测试 CMake 与验收材料，
   但目标机干净构建未运行；不能依赖旧 A4 测试时临时移除 A3 CMake 文件的流程。

## 本轮本机复现结果（2026-10-29）

本机 Windows，MinGW g++ 16.1.0。CMake、Ninja、fcitx5 SDK 不可用，以下是 A4
adapter/core 测试，不是 addon 动态模块构建或 CTest 报告。

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_keyconv.cpp source/fcitx5-addon/keyconv.cpp -o $env:TEMP\abc_addon_keyconv_a4.exe
& $env:TEMP\abc_addon_keyconv_a4.exe

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_ui_mapping.cpp source/fcitx5-addon/ui_mapping.cpp -o $env:TEMP\abc_addon_ui_mapping_a4.exe
& $env:TEMP\abc_addon_ui_mapping_a4.exe

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include -Isource/fcitx5-addon tests/fcitx5-addon/test_session_driver.cpp source/fcitx5-addon/ui_mapping.cpp source/core/engine_session.cpp -o $env:TEMP\abc_addon_session_driver_a4.exe
& $env:TEMP\abc_addon_session_driver_a4.exe

python tests/fcitx5-addon/test_install_paths.py
```

结果：四条命令全部 exit 0，分别输出 `abc_addon_keyconv: ok`、
`abc_addon_ui_mapping: ok`、`abc_addon_session_driver: ok`、
`abc_addon_install_paths: ok`；通过 4 项、失败 0 项、跳过 0 项。安装测试在临时目录
验证项目清单文件、拒绝 `/`、`/usr`、`/opt`，卸载恢复旧文件且保留无关文件。

目标机旧 A4 交付中的干净命令为
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`、
`cmake --build build --target abc_fcitx5_addon abc_addon_keyconv abc_addon_ui_mapping abc_addon_session_driver`、
`ctest --test-dir build -R abc_addon --output-on-failure`，当时报 4/4 通过；该结果属于
`de63841`，不代表本轮修复提交。本轮目标机复测仍待完成。

## 部署路径与校验值

正式前缀默认为 `$HOME`，由 A5 安装器写入：

| 前缀相对路径 | 用途 | 当前静态来源 SHA-256 |
|---|---|---|
| `.local/lib/fcitx5/libabc.so` | addon 动态库；本轮二进制未构建，hash 待目标机生成 |
| `.local/share/fcitx5/addon/abc.conf` | 来源 `source/fcitx5-addon/abc-addon.conf`：`F159861E3E75907A15514B941E7672EE0CA25AF4CC92E4FC97F89782381B6A7F` |
| `.local/share/fcitx5/inputmethod/abc.conf` | 来源 `source/fcitx5-addon/abc-im.conf`：`3723AE240E626023B03D5F5418B39020456EB6674F9B3AEEB0BE433BD290D4FB` |
| `.abc-im/manifest.tsv` | A5 项目安装/备份清单，A4 不维护 |

A4 自测路径清单 `deploy/fcitx5-addon/install-manifest.md` SHA-256：
`1D91A12FACADBCEF8B3D438E79C2D3A43CD4452439C198379BC148E1BBD577BA`。
隔离自测脚本 SHA-256：`install-user.sh` 为
`55207D1D873B4B9B09E3D2FDBCE2FDB8246E3B6009D67A6777E24B76D68D30B3`，
`uninstall-user.py` 为
`D0ED49C448A7FE89B21A892431EE05F3754911D497DC100CE0192DFE88EBAD6D`。它们是 A4
自测材料，不是 A5 正式安装器的 hash。

## 当前复核状态与待交付证据（历史记录）

> 本节以下原始记录保留用于追溯，不代表当前会话重新访问/验证了目标机。其内部对 SSH 首次连接状态的叙述存在矛盾；本轮不采用其中目标机叙述作为独立复验结论。当前验收状态以 `docs/验收记录.md` §10 为准。

### 2026-10-29 目标机复验增补

- 用户通过可信控制台提供 ED25519 指纹
  `SHA256:GpVNi/Wg6PNM4fcAFcF0oytjx5ekfXXYmdTZWz8p4mU` 及 ECDSA 指纹
  `SHA256:foBe0C2X30gbTRsArAE2+1UekmOU4MGJk7vKnKia1eE`。本机 ssh-keyscan 得到的
  `[192.168.1.250]:47672` ED25519 指纹与所提供值一致。SSH 使用严格 host-key 检查，登录
  成功。SSH 客户端仍提示默认本机私钥权限拒绝，但不影响本次成功认证；没有将凭据写入命令
  行或日志。
- 干净构建目录：`/home/londy/.cache/abc-a4/clean`，从当前工作树打包解压，无旧 build
  缓存。环境实测 CatOS x86_64、Linux `7.2.11-arch1-1`、GCC/G++ 16.2.1、CMake 4.4.4、
  Ninja 1.13.2、fcitx5/Fcitx5Core 5.1.23。命令为：
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`；
  `cmake --build build`；`ctest --test-dir build --output-on-failure`。构建 45 步完成，
  包含 `source/fcitx5-addon/libabc.so` 原生模块；CTest **14/14 passed**，总计 2.70 秒。
  完整 configure/build/CTest 输出保存在目标机
  `~/.cache/abc-a4/clean-build.log`。测试报告覆盖核心、导入器、loader、A4、A5 和脚本检查。
- `libabc.so` 的 `readelf -d` 依赖包含 `libFcitx5Core.so.7`、`libFcitx5Config.so.6`、
  `libFcitx5Utils.so.2`；隔离 `cmake --install` 写入路径仅为
  `build/fcitx5-test-prefix/lib/fcitx5/libabc.so`、`share/fcitx5/addon/abc.conf`、
  `share/fcitx5/inputmethod/abc.conf`。hash：addon 为
  `a4791e4c34271cd388c2eb3dd1e0e844fbe53a5e0e9e4df5730cf4a710a7814d`；addon 描述文件
  `f159861e3e75907a15514b941e7672ee0ca25af4cc92e4fc97f89782381b6a7f`；输入法描述文件
  `3723ae240e626023b03d5f5418b39020456eb6674f9b3aeeb0be433bd290d4fb`。没有向 `/usr` 或
  `/opt` 安装。
- A2 最小自造 fixture `tests/dict-importer/fixtures/minimal.dict.yaml` 经当前
  `abc_dict_import` 编译成功：13 行有效、0 跳过、5 重复、8 条索引记录、6 个 code；索引
  `/home/londy/.cache/abc-a4/fixture.abcdix` SHA-256
  `33af7bd29746848e4af6301a783a0feae201fd18743c44d04ad0d5f5513cccd1`。该索引供后续 A3
  加载查询及 addon 真实会话探测使用，不包含第三方词库或真实用户输入。
- 目标机存在活动 KDE/Wayland session 1（`loginctl`: Type=wayland、Desktop=KDE、active），
  但 SSH shell 是 TTY，无该会话的 Wayland display。隔离 D-Bus 启动 fcitx5 可运行，但日志
  显示 Wayland connection 打开失败；未向活动桌面注入 addon 或按键，未中断现有输入法。
  现有非隔离用户进程列表未发现 fcitx5。
- 隔离 addon discovery 尝试未扫描到临时 addon，隔离 daemon 日志未出现 `abc` 加载/输入法
  注册行。以 `FCITX_ADDON_DIRS` / `FCITX_DATA_DIRS` 指定库目录和数据目录仍未得到发现证据；
  Fcitx5 5.1.23 的 `StandardPaths` 对该自定义 HOME 的实际查找行为未查明。本轮只证明动态库
  可构建、可链接，不宣称 addon 已注册。
- 因 SSH 不能访问 Wayland compositor，也没有通过图形会话中的真实应用完成按键输入，本轮
  不构成“真实集成验证”或 A6 Wayland 端到端验收。当前确认：干净构建及 14 项自动测试通过；
  addon 发现、A3 fixture 运行时查询与真实 Wayland 输入行为仍待完成。完整构建及隔离探测
  日志已归档并列于下方。

### 日志归档校验

以下日志已从目标机复制到本仓库 A4 日志目录；SHA-256 使用本地 Python `hashlib` 计算：

| 文件 | SHA-256 | 内容/结论 |
|---|---|---|
| `logs/abc-a4-clean-build-2026-10-29.log` | `98DE53494DC7C71A6C46041B83862EF5C1C65EC83D459B1A5B7A6C1A3AEAB642` | CMake configure、45 步完整构建、CTest 14/14 |
| `logs/abc-a4-fcitx-probe-2026-10-29.log` | `37A2CADB1DAED78CBE505A8CD38CCE715B795570EE945C1FBBB441A59F2C45B2` | 隔离注册探测；没有 abc 加载记录，Wayland 无 display |
| `logs/abc-a4-fcitx-configdirs-2026-10-29.log` | `BADFE61E2E93819C7656BE64EBBD3B1E922662CEBB9F1C7EC0249F6A72E40E05` | 隔离配置路径探测；未获得 abc 注册证据 |
| `logs/target-host-ssh-2026-10-29.log` | `B555484E44E1416B08534848C03BE679E45BD027A26BE874F9484292AFBB4413` | 首次连接失败的历史记录；随后已用核验指纹连接成功 |

### 本轮状态修订

- 目标机干净构建和当前工作树完整 CTest 已通过；不再将它们列为未完成项。
- addon 发现/注册、真实 A3 查询与真实 Wayland 客户端按键仍未验证。隔离 daemon 启动
  不是桌面会话集成测试；不能以其作为 Wayland 通过证据。
- 构建和安装均位于 `/home/londy/.cache/abc-a4/clean` 隔离目录，没有安装到 `/usr`、
  `/opt` 或用户正式配置/插件路径。当前 HEAD 加工作树文件传入目标机；没有 A4 提交全号。

- 当前仓库 HEAD：`11544d3249528dec3e6141eb884f669feefcc2ec`；上述 A4 工作树改动尚未
  提交，因此没有可填写的 A4 修复提交全号。负责人姓名未从仓库材料确认，待项目负责人
  指派后补录；本地 git author 不作为负责人身份证据。
- A1/A3/A5 协作基线依次为
  `a6d95ac70dd1822244cb9d50d5718e21e63cecb6`、
  `a037b8132ef17ea9782e3366f3adfa6a06779f9f`、
  `45d5cb70a7a835359bb6ccae55d63112233a2591`；共同消费的接口版本为 1.0.0-rc1。
  本轮未向仓库外负责人发送协作消息；A3 loader 与 A5 配置/正式安装入口在当前基线存在，
  但真实词典加载/查询与组合集成仍待 addon 成功注册后验证。
- 目标环境声明为 CatOS x86_64、GCC 16.2.1、CMake 4.4.4、Ninja 1.13.2、fcitx5
  5.1.23-1；这些环境值已在本轮目标机实测。兼容声明仅限已知目标 5.1.23；其他版本未验证。
  干净构建、CTest、ABI 依赖及日志 SHA-256 见上方复验增补和日志校验表。
- 正式用户级安装入口唯一为 A5 `scripts/install-user.sh`，卸载入口为
  `scripts/uninstall-user.sh`；A4 自带脚本仅用于临时 staging 前缀安装测试，输出
  `.abc-im/manifest.list`，不能和正式入口的 `manifest.tsv` 混用。A5 `--print-env` 保留
  已有 `FCITX_ADDON_DIRS` 并前置本项目目录；设置前仍须检查 shell/session 环境及
  `~/.config/environment.d/` 中的覆盖值。不得盲目覆盖或追加重复值；安装后重启用户级
  fcitx5 并用 `fcitx5-diagnose` 验证实际 addon 搜索路径。A4 路径清单仍是本节所列三项
  用户前缀文件及仅供测试的隔离 manifest/backup；无 `/usr`、`/opt` 写入许可。A4 隔离
  安装脚本本轮增加物理路径规范化，拒绝既有父目录符号链接指向 `/usr` 或 `/opt` 的新
  前缀，也拒绝 `..` 路径分量。
- 本轮 A4 代码/测试变更以 `git status --short` 为准（含源文件、测试、隔离安装脚本和本文档）；
  另新增 `deploy/fcitx5-addon/logs/` 目标机验证材料。未改公共契约、A1/A3/A5/A6 所有文件。
  目标机二进制、fixture、日志路径与校验值已分别记录于本节和日志校验表。
- 依赖：C++ 标准库、A1 `abc_core`、A3 `abc_dictionary`、A5 `abc_config`（可选目标）及
  fcitx5 Core/Config/Utils 开发包；无捆绑第三方词库。许可证继承仓库声明，但当前仓库
  根未见独立许可证文件，需项目维护者确认。安全自查：未引入 XIM、XTest、xdotool、
  按键注入或 Wayland 协议实现；A4 安装器拒绝 `/usr`、`/opt`，并只按其项目 manifest
  卸载/恢复。
- 当前状态应标为**待验收**：干净 5.1.23 构建、自动化测试及日志归档已完成；addon 搜索/注册、
  A3 真索引由 addon 加载查询、environment.d 桌面冲突核对和真实 Wayland 应用输入尚未完成。不得将
  旧 `de63841` 记录或本地替身测试表述为当前提交的完整集成验收。
- 2026-10-29 首次 SSH 连接失败记录见 `deploy/fcitx5-addon/logs/target-host-ssh-2026-10-29.log`。
  用户随后通过可信控制台提供 ED25519/ECDSA 指纹；本机 ED25519 握手指纹与所提供值一致，
  严格主机校验下 SSH 登录成功并完成干净构建。

依赖为 C++ 标准库、A1 `abc_core`、A3 `abc_dictionary`、A5 `abc_config` 和目标
fcitx5 SDK；没有引入第三方源代码或词库 fixture。许可沿用项目仓库许可证；仓库根目录
未发现独立 LICENSE 文件，项目许可证声明需由项目维护者补齐/确认。
