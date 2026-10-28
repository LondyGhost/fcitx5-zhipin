# fcitx5 addon 适配层

A4 拥有本模块。接口契约版本 1.0.0（rc1，见 `docs/interfaces/`）。

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
`fcitx::Key::isReleaseOfModifier`，所以左右 Shift 释放都能命中默认的 `Shift`。
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

构建时若存在 `abc_config`，addon 读取
`$XDG_CONFIG_HOME/abc/config.ini`，否则 `$HOME/.config/abc/config.ini`。
解析失败由 A5 回退默认值。没有 `abc_config` 时使用契约默认值。

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
这是给集成负责人的工具链变更请求：要么确认 addon 局部 C++20，要么把根标准
升到 C++20。A4 不能改根 `CMakeLists.txt`。

安装前缀默认是 `$HOME/.local`。部署脚本拒绝 `/`、`/usr`、`/opt`。

## 测试

| 测试 | 覆盖 |
|---|---|
| `abc_addon_keyconv` | 转换表、修饰键、热键释放、未识别键 |
| `abc_addon_ui_mapping` | preedit、commit 顺序、未消费键的 effects、停用清理 |
| `abc_addon_session_driver` | 真实 `EngineSession` 的 preedit、选词提交、reset、未消费标点 |
| `abc_addon_install_paths` | 临时前缀安装、清单、系统路径拒绝、卸载与备份恢复 |

这些测试不链接 fcitx5，不能代替 A6 的 Wayland 端到端验收。

## 注册

安装布局见 `deploy/fcitx5-addon/install-manifest.md`。
fcitx5 5.1.23 默认不搜索用户级 addon 库目录，运行环境需要：

```sh
export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
```

然后 `fcitx5 -r`。`fcitx5-diagnose` 应列出 `abc`，库路径应在用户前缀下。

## 已知限制

- 点击候选不提交。选择只走数字键和空格。
- `modeSwitchKey` 只支持 Shift、Control、Alt、Super 及其左右键名。
- 索引不可用时 addon 仍注册，但按键全部转发。
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

未改核心、词典、公共头、根 `CMakeLists.txt` 和其他任务文件。

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

`abc.so` 链接 `libFcitx5Core.so.7`、`libFcitx5Config.so.6`、
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

- `lib/fcitx5/abc.so`
- `share/fcitx5/addon/abc.conf`
- `share/fcitx5/inputmethod/abc.conf`

`/usr`、`/opt` 下无 `abc.so` 或 `abc.conf`。
`install-user.sh` 在临时前缀下通过安装、拒绝系统路径、卸载和备份恢复测试。

### 自查

- 无 XIM、XTest、xdotool 或模拟按键注入。
- 无 `/opt` 安装，不覆盖系统 `/usr`。
- 仓库和本记录不含部署凭据、真实用户输入或 token。
- Wayland 端到端不在本任务内，不能把本次注册结果当成 A6 通过。

### 给集成负责人

1. 目标机 fcitx5 5.1.23 公开头要求 C++20。addon 目标单独设为 C++20，
   根清单仍是 C++17。请确认局部 C++20，或把根标准升到 C++20。
2. 该版本没有用户级 addon 库目录。用户安装后要在会话里设置
   `FCITX_ADDON_DIRS=$HOME/.local/lib/fcitx5:/usr/lib/fcitx5`。
3. A3 的 `source/dictionary/CMakeLists.txt` 引用了尚未入库的
   `dictionary_index.cpp`。验证 A4 时临时移开了目标机副本里的该清单，
   仓库中的 A3 文件未改。

