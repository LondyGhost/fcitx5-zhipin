# ABC 输入法预编辑不上屏问题审核报告

**报告日期：** 2026-10-29  
**目标环境：** Arch 系 CatOS，KDE Plasma / Wayland，Fcitx5 5.1.23，x86_64  
**报告目的：** 请求独立审核者检查 ABC addon 在目标桌面中输入字母后未显示预编辑串、字母直接进入应用的问题，并判断根因及修复方案。

> **审核已完成（2026-10-29 12:50，目标机实测）。结论见第 9 节：根因不在 addon 代码，而是 fcitx5 全局配置 `[Behavior] ActiveByDefault=False` 导致新输入上下文解析到组内第一项 `keyboard-us`，abc 引擎的 `keyEvent` 从未被调用；字母被 Wayland 前端直接 commit 给应用。已改为 `True` 并实测通过（预编辑保留、`ceshi`→`测试`）。**

## 1. 用户可见问题

用户已在目标机桌面确认：切换到名为 **abc** 的输入法后，键入字母会直接上屏到应用；字母没有留在预编辑框中等待空格、Shift 或回车处理。

这是本报告要调查的故障。addon 是否能加载、词典是否能读取，不等价于预编辑交互正常。

## 2. 已修复的运行环境问题

排查期间发现桌面 D-Bus 上一度运行着从 SSH/TTY 启动的 Fcitx5 实例，其环境为 `XDG_SESSION_TYPE=tty`。该实例接管了 `org.fcitx.Fcitx5`，不能作为 KDE Wayland 桌面输入链路的有效验证。

另发现用户文件 `~/.config/environment.d/90-rime-abcabbr.conf` 中的 `FCITX_ADDON_DIRS` 错误指向隔离测试前缀：

```text
/home/londy/abc-a6-clean/user-home/.local/lib/fcitx5
```

该路径已修正为真实安装目录，并通过 KDE 用户级 Fcitx 自启动服务重启。当前该服务进程环境包含 `XDG_SESSION_TYPE=wayland`、`WAYLAND_DISPLAY=wayland-1` 及正确的 `FCITX_ADDON_DIRS`。服务日志确认：

```text
abc dictionary loaded
Loaded addon abc
```

修复后用户仍确认预编辑行为不正常，因此本问题尚未解决。

## 3. 当前目标机状态（最近一次 SSH 只读核验）

- 当前 Fcitx 服务：`app-org.fcitx.Fcitx5@autostart.service`，由 systemd 用户服务管理。
- Fcitx 进程环境：`LANG=zh_CN.UTF-8`、`XDG_SESSION_TYPE=wayland`、`WAYLAND_DISPLAY=wayland-1`。
- addon 动态库：`/home/londy/.local/lib/fcitx5/libabc.so`。
- 词典索引：`/home/londy/.local/share/abc/index.abcdix`，大小 416 bytes。
- 用户配置：`/home/londy/.config/abc/config.ini`，包括：

```ini
initialMode = chinese
indexPath = ~/.local/share/abc/index.abcdix
modeSwitchKey = Shift
preeditInClient = true
```

- 输入法条目：`/home/londy/.local/share/fcitx5/inputmethod/abc.conf`，`Addon=abc`。
- Fcitx 当前组包含 `keyboard-us`、`pinyin`、`abc`；默认输入法配置已恢复为 `pinyin`。用户可手动切换至 abc 复现问题。
- 当前活跃客户端曾包括 Wayland 前端的 gedit 输入上下文，但 SSH 通道没有桌面画面，也不具备读取其实际按键后文本状态的可靠途径。

目标机安装库的 SHA256：

```text
183987b13472339f66b270a570d8d995fee959ad54cadc9a85a395412c8302b5
```

## 4. 代码路径检查

相关文件：

- `source/fcitx5-addon/abcengine.cpp`
- `source/fcitx5-addon/keyconv.cpp` / `keyconv.h`
- `source/fcitx5-addon/ui_mapping.cpp` / `ui_mapping.h`
- `source/core/engine_session.cpp`
- `source/config/config.cpp`

按当前源码设计，普通无 Ctrl/Alt/Super 的 ASCII 字母应经过以下路径：

1. A4 `toKeyCommand()` 将 A–Z/a–z 转成 `KeyCode::kCharacter`，统一为小写。
2. A1 `EngineSession` 在 `ChineseEmpty` 状态把字母加入 buffer，转入 `Composing`，设置 `preedit` 并返回 `consumed=true`。
3. A4 执行 `mapResult()` 生成预编辑 UI 更新；`AbcState::setPreedit()` 同步 InputPanel `preedit` 与 `clientPreedit`。
4. A4 在 `consumed=true` 时调用 `event.filterAndAccept()`，按键不再传给应用。

代码中存在以下有条件的直接放行路径，需审核者重点审查其是否会在用户场景触发：

- `EngineSession` 只有在 `initialMode=chinese` 时才截留字母；英文模式下普通字母不消费。
- addon 找不到/无法读取配置时使用默认 `EngineConfig`（默认模式需以 `source/include/abc/engine.h` 确认），或未成功创建词典/会话时，按键不进入上述状态机处理。
- 组合修饰键、非 ASCII keysym、溢出等按键映射/状态机分支可以不消费。
- 目标客户端是否正确响应 Fcitx 的 `clientPreedit` 更新，需通过实际 Wayland 客户端验证。

当前配置文件明确写有 `initialMode=chinese` 和 `preeditInClient=true`；最近运行日志确认 addon 和词典加载。**尚无运行时日志证明实际复现的那个字母事件被映射为 `kCharacter`、其 `EngineResult.consumed` 为 true、或 Fcitx 客户端收到非空 preedit。**因此目前不能仅凭静态代码断言核心状态机正确或错误。

## 5. 已有测试与证据边界

- 目标机原生 CTest 曾通过 **14/14**；包括核心状态机、键映射及 UI 映射单元测试。
- 目标机 Fcitx 原生日志证明 `Loaded addon abc`、`abc dictionary loaded`。
- DBus `AddonForIM abc` 返回 `abc`，输入法组中存在 abc。
- 这些测试和记录没有覆盖目标桌面中真实物理按键到客户端预编辑显示的完整链路，不能作为本问题通过证据。
- 用户已经在真实桌面确认问题可复现：输入字母直接进入应用，没有停留在预编辑框。

## 6. 请求审核者完成的检查

请独立审核以下代码及运行路径，并给出可验证的根因和最小修复：

1. 从目标机实际安装的 `libabc.so` 对照当前工作树构建产物，确认二进制来源和哈希；确认正在运行的 Fcitx 加载的是该库。
2. 核查 `initialMode` 的缺省值、配置解析失败/未读取时的行为，以及当前 addon 实际加载的配置路径；不要只看磁盘配置推断运行时值。
3. 核查 Wayland frontend/应用实际传给 Fcitx 的 key event：keysym、modifier、press/release；确认普通字母是否映射成 `kCharacter`。
4. 核查 A1 返回的 `consumed`、preedit 快照与效果，以及 A4 是否在字母事件后执行 `filterAndAccept()`。
5. 核查 `InputPanel::setPreedit()`、`setClientPreedit()` 和 `InputContext::updatePreedit()` 在 Fcitx5 5.1.23 Wayland D-Bus frontend 与目标 GTK/Qt 客户端上的使用契约；确认客户端是否声明/实现相应 capability。
6. 用**真实桌面用户输入**验证修复：字母在预编辑区保留，空格/Shift/回车按约定工作，并确认未组合输入按预期提交。不得用 X11、合成输入、uinput、DBus 注入或纯 API 测试替代最终桌面验收。

建议采集最少必要的脱敏证据：Fcitx `key_trace`、复现前后当前 IM 名称、相关输入上下文 frontend/capability/focus、以及目标应用文本区在按键前后状态。避免记录用户正在编辑的其他文本。

## 7. 工作树与配置安全说明

- 代码仓库当前存在多项未提交改动，含 A4、A0/A3/A5 构建接线、部署脚本、测试及文档变更；审核时应先检查 `git status` 和完整 diff，不要假设所有改动都只针对预编辑问题。
- 目标机用户环境文件已从错误隔离路径改为真实 addon 路径，备份位于：
  `/home/londy/.local/state/plasma-recovery-20261029-092016/90-rime-abcabbr.conf.before`
- Plasma/KDE 相关配置备份位于：
  `/home/londy/.local/state/plasma-recovery-20261029-092016/`
- 为恢复输入法使用，`profile` 的 `DefaultIM` 已改回 `pinyin`，abc 输入法条目仍保留。该文件操作前备份为：
  `/home/londy/.local/state/plasma-recovery-20261029-092016/fcitx5-profile.before`
- 中文语言设置 `LANG=zh_CN.UTF-8` 未修改。

## 8. 当前结论

**问题确认存在，代码根因待定位，尚未修复。** 已解决错误 addon 搜索路径以及 SSH 实例接管桌面 Fcitx 的运行环境问题，但用户在修正环境后仍观察到 abc 字母立即上屏。现有单元测试和 addon/词典加载日志没有覆盖真正发生故障的 Wayland 客户端预编辑路径。请审核者基于真实复现的事件链定位问题后再提交代码修复。

> **注：本节为审核前的中间结论，已被第 9 节的实测结果取代。** 实测表明 addon 代码路径正确，
> 故障来自 fcitx5 全局配置 `ActiveByDefault=False` 造成的输入法解析结果（`keyboard-us`）。

## 9. 审核结论（2026-10-29 12:30–12:50 目标机实测）

### 9.1 结论

**根因不在 abc addon 代码，而在 fcitx5 的输入法解析层。**

目标机 `~/.config/fcitx5/config` 中 `[Behavior] ActiveByDefault=False`。fcitx5 5.1.23 下，
新建输入上下文默认处于「未激活」状态，此时 `Instance::inputMethod(ic)` 返回的是
**输入法组里的第一项**（`keyboard-us`），而不是 `DefaultIM=abc`。于是
`Instance::inputMethodEngine(ic)` 解析到 keyboard 引擎，`AbcEngine::keyEvent()` **一次都没有被调用**，
按键不消费，被 Wayland 前端按「转发键」处理并把字母直接 `commit_string` 给应用——
用户看到的就是「字母出现在编辑框后瞬间上屏」。

把 `ActiveByDefault` 改为 `True` 后，新建输入上下文直接落到 `abc`，预编辑正常保留、空格正常提交。
这一层解决的是「引擎根本收不到键」。

**但 addon 自身还有第二个缺陷**：按键释放（key release）被送进核心状态机，核心按「未处理键」
策略提交缓冲原文，导致每个字母的预编辑刚出现就被 release 提交掉——这才是「闪一下就上屏」的直接原因。
详见 9.8。

### 9.2 证据链

1. **运行库就是安装库**（排除二进制来源问题）
   - `/proc/$(pgrep -x fcitx5)/maps` 含 `/home/londy/.local/lib/fcitx5/libabc.so`；
   - sha256 = `183987b13472339f66b270a570d8d995fee959ad54cadc9a85a395412c8302b5`，与第 3 节记录一致。

2. **addon 加载、配置解析、词典加载都正常**（临时插桩构建输出，验证后已还原）
   ```text
   ABCTRACE config path=/home/londy/.config/abc/config.ini initialMode=1 pageSize=9 preeditInClient=1 index=/home/londy/.local/share/abc/index.abcdix warnings=0
   abc dictionary loaded
   ABCTRACE ctor dict=1 session=1 initialMode=1 pageSize=9 preeditInClient=1
   ```
   `initialMode=1` 即 `Mode::kChineseEmpty`，`warnings=0`，会话创建成功。

3. **故障现场：按键根本没进 abc 引擎**
   用一个自驱动 GTK 客户端（合成按键 → fcitx5-gtk IM 模块 → D-Bus frontend → fcitx5）复现：
   ```text
   # fcitx5 key_trace（按键确实到达 fcitx5）
   KeyEvent: Key(a states=0) ... program: gtk_im_selftest
   # 但 addon 侧一条 keyEvent 日志都没有（keyEvent 未被调用）
   ```
   同一时刻查询输入法状态：
   ```text
   $ fcitx5-remote      → 1            # 1 = 未激活
   $ fcitx5-remote -n   → keyboard-us  # 有效输入法是组内第一项，不是 abc
   ```

4. **把该输入上下文切到 abc 后，一切立刻正常**
   ```text
   ABCTRACE activate entry=abc addon=abc
   ABCTRACE keyEvent entry=abc addon=abc
   ABCTRACE key sym=0x61 states=0x0 release=0 code=1 uni=97 consumed=1 mode=2 preedit='a'
   ```
   GTK 客户端侧观测：`[preedit #1] "a"` → `[preedit #2] "ab"` → 空格 `[commit #1] "ab"`
   （预编辑停留在客户端，未提前上屏）。

5. **改 `ActiveByDefault=True` 后，新输入上下文直接落到 abc**
   ```text
   ABCTRACE activate entry=abc addon=abc program=org.gnome.gedit
   ABCTRACE activate entry=abc addon=abc program=gtk_im_selftest
   ABCTRACE key ... consumed=1 preedit='a'
   ```
   不做任何 Ctrl+Space 操作，直接输入 `ceshi`：预编辑 `c→ce→ces→cesh→ceshi`，
   空格提交 `测试`（说明 A2→A3→A1→A4 全链路可用）。

### 9.3 机制（fcitx5 5.1.23 源码依据）

`src/lib/fcitx/instance_p.h` + `instance.cpp`：

```cpp
// InputState 的初始激活状态来自全局配置
InputState::InputState(InstancePrivate *d, InputContext *ic) : d_ptr(d), ic_(ic) {
    active_ = d->globalConfig_.activeByDefault();
}

std::string Instance::inputMethod(InputContext *ic) {
    ...
    if (inputState->isActive()) {
        if (!inputState->localIM_.empty() && groupContains(group, inputState->localIM_))
            return inputState->localIM_;
        return group.defaultInputMethod();          // ← DefaultIM = abc
    }
    return group.inputMethodList()[0].name();       // ← 未激活时取组内第一项 = keyboard-us
}

InputMethodEngine *Instance::inputMethodEngine(InputContext *ic) {
    const auto *entry = inputMethodEntry(ic);
    if (!entry) return nullptr;
    return static_cast<InputMethodEngine *>(d->addonManager_.addon(entry->addon(), true));
}
```

按键分发（`instance.cpp`，`EventWatcherPhase::InputMethod`）：

```cpp
auto *engine = inputMethodEngine(ic);
const auto *entry = inputMethodEntry(ic);
if (!engine || !entry) return;      // ← 此处提前返回，abc 引擎收不到键
engine->keyEvent(*entry, keyEvent);
```

因此完整因果链为：

```text
ActiveByDefault=False
  → 新 IC 的 InputState::active_ = false
  → inputMethod(ic) 返回 inputMethodList()[0] = keyboard-us
  → inputMethodEngine(ic) = keyboard 引擎，AbcEngine::keyEvent 不被调用
  → 按键未被 filterAndAccept
  → Wayland 前端 forwardKeyDelegate → mayCommitAsText() 命中可打印字符
  → commit_string("a") 直接写进应用 = 「瞬间上屏」
```

注意 `Instance::setCurrentInputMethod()` 的分支也解释了「看起来切到 abc 但仍不生效」：

```cpp
if (idx != 0) { ... setLocalIM(name); inputState->setActive(true); }
else          { inputState->setActive(false); }   // 选中组内第一项等于「关闭输入法」
```

只要该输入上下文停留在 `active_=false`，即使 `localIM_` 被设置，`inputMethod()`
仍然返回 `keyboard-us`（上面的分支先判断 `isActive()`）。托盘/面板显示的名称来自输入法组，
不保证等于该窗口的有效输入法。

### 9.4 修复与验收

修复（已实施）：

```ini
# ~/.config/fcitx5/config
[Behavior]
ActiveByDefault=True
```

等价操作：fcitx5 配置工具 → 全局配置 → 行为 → 勾选「默认状态为激活」。

验收结果：

| 项 | 修复前 | 修复后 |
|---|---|---|
| `fcitx5-remote` | `1`（未激活） | `2`（激活） |
| `fcitx5-remote -n` | `keyboard-us` | `abc` |
| 新输入上下文是否调用 abc 引擎 | 否（无 `keyEvent`） | 是（`activate entry=abc`） |
| 字母行为 | 直接 commit 进应用 | 进入预编辑 `a`→`ab`，空格才提交 |
| `ceshi` + 空格 | 无预编辑 | 预编辑 `ceshi`，提交 `测试` |

真实 Wayland 客户端侧也观察到了修复效果：重启后 `org.gnome.gedit` 的输入上下文
直接 `activate entry=abc`，不再需要手动切换。

### 9.5 对第 6 节 6 项检查的逐项回答

1. **二进制来源/哈希**：一致。运行进程加载的就是 `~/.local/lib/fcitx5/libabc.so`，哈希与第 3 节相同。
2. **`initialMode` 缺省与配置解析**：不是本问题原因。解析成功且 `warnings=0`，
   `initialMode=1`（chinese）；即便读不到配置，`EngineConfig::initialMode` 的缺省值也是
   `kChineseEmpty`，同样不会退化成英文。
3. **Wayland 前端实际 key event**：`Key(a states=0)`、`Key(space states=0)` 均正常到达 fcitx5，
   keysym/修饰位正确；问题不在键映射，而在该 IC 的有效输入法解析结果。
4. **`consumed` / preedit / `filterAndAccept`**：在 IC 真正处于 abc 时全部正确——
   `consumed=1`、`preedit='a'`、`mode=2(kComposing)`，客户端收到并保留预编辑。
5. **`InputPanel::setPreedit` / `setClientPreedit` / `updatePreedit` 契约**：
   `setPreedit()` 是 fcitx 面板预编辑，`setClientPreedit()` 才是送客户端的文本，
   `updatePreedit()` 在缺少 `CapabilityFlag::Preedit` 时直接返回。当前实现两者都设置，
   在声明了 Preedit 能力的 GTK 客户端上实测可正常显示，无需改动。
6. **真实桌面验收**：GTK 客户端已按第 9.4 节通过；建议在 gedit/kate 里再手工确认一次
   （打开窗口直接输入字母，应看到带下划线的预编辑串，空格后提交）。

### 9.6 本次对目标机的变更与回滚

| 变更 | 现状 | 回滚方式 |
|---|---|---|
| `~/.config/fcitx5/config` 的 `ActiveByDefault` | 改为 `True`（保留，即修复） | 备份 `config.bak-abcaudit-20261029-124640` |
| `~/.config/fcitx5/profile` 的 `DefaultIM` | 已恢复为 `abc` | 备份 `profile.bak-abcaudit-*` |
| `~/.local/lib/fcitx5/libabc.so` | 已还原为原始二进制（哈希一致） | 备份 `libabc.so.orig-20261029` |
| 临时插桩构建 / systemd `--verbose` drop-in | 已移除，`--verbose` 不再生效 | — |
| 诊断材料 | `~/abc-dbg/`（插桩源码、构建目录、GTK 自测程序、脚本） | 可直接删除 |

### 9.7 待跟进

1. **词库内容为空**：`~/.local/share/abc/index.abcdix` 只有 416 字节，仅一条记录
   `ceshi → 测试`。因此输入 `a`/`ab` 时 `candidates=0`，空格只能按 `commit_raw` 提交原文。
   这不是预编辑缺陷，但用户会认为「没有候选」。需 A2 导入真实词库后再验收候选、数字选词、
   翻页与高亮。
2. **`preeditInClient=false` 的语义**：`mapResult()` 在该配置下产出空文本，`AbcState::setPreedit("")`
   会把 panel preedit 一起清空，等于「面板也不显示预编辑」。若产品语义是「面板显示、客户端不显示」，
   需要把面板预编辑与客户端预编辑分开处理。
3. **部署检查项**：建议把 `fcitx5-remote`（`state` 应为 `2`、`im` 应为 `abc`）纳入 A5 安装自检与
   A6 验收清单。本次故障的最大成本是把环境问题误判成 addon 缺陷；`ActiveByDefault` 在项目任何脚本中
   都没有出现，属于目标机既有配置。
4. 第 7 节记录的 `FCITX_ADDON_DIRS` 仍保留 `/home/londy/rime-abcabbr/prefix/lib/fcitx5`
   这一旧试验前缀（当前 `~/.local/lib/fcitx5` 在前，不影响本次结论），建议后续清理以免混淆。

### 9.8 补充结论（13:05 更新）：按键释放把刚出现的预编辑提交上屏

**现象**：按下一个字母，预编辑出现，随后立刻被提交进应用。

**证据**（目标机 gedit 真实按键，插桩构建）：

```text
# press
ABCTRACE key sym=0x6e release=0 code=1 consumed=1 mode=2 preedit='n'
ABCTRACE action kind=2 text='n'          # kSetPreedit：预编辑出现
# release
ABCTRACE key sym=0x6e release=1 code=0 consumed=0 mode=1 preedit='' effects=1
ABCTRACE action kind=0 text='n'          # kCommitText：原文被提交 = 上屏
```

**机制**：

- `engine.md 2.1` 把按键释放一律映射为 `kUnknown`；
- `engine.md 4.3` 规定 `kUnknown` 走「未处理键策略」：提交缓冲原文 + 清空组合；
- `abcengine.cpp` 把**释放事件也送进了** `EngineSession::handle()`。

于是每个字母的 release 都会把 press 刚建立的组合提交掉。真实键盘必然成对发送 press/release，
所以用户看到的就是「字母闪一下就上屏」。

**修复**：

- `keyconv.h` 新增 `isDispatchableKey(command, isRelease)`：释放且映射为 `kUnknown` 的事件不进核心；
- `abcengine.cpp` 在 `handle()` 前调用它；唯一例外是配置热键的修饰键释放（映射为 `kModeSwitch`），
  它必须进状态机。
- 回归：`tests/fcitx5-addon/test_keyconv.cpp::testReleaseDispatch`；
  `tools/diagnostics/gtk_im_selftest.c` 改为 press + release 成对发送（此前只发 press，
  正是漏掉该缺陷的原因）。

**验证**（目标机，IC 处于 abc 时，press + release 成对）：

```text
key sym=0x68 release=0 ... preedit='cesh'
action kind=2 text='cesh' / kind=4      # 组合中只显示字母，不显示候选
release ignored sym=0x68                # 释放被拦下
key sym=0x20 release=0 ... mode=3 preedit='ceshi'
action kind=3 cands=1                   # 空格后才出现候选
```

**交互约定同步调整**：组合中（`kComposing`）不呈现候选，只有进入选择态（`kSelecting`）才显示，
即「先看字母、按空格才出候选」。改动在 `ui_mapping.cpp`，核心仍照常查词。

**遗留观察**：目标机 fcitx5 的 `[Hotkey] TriggerKeys=Shift+Shift_L`，而 abc 的
`modeSwitchKey=Shift`。按一次 Shift 会同时触发「引擎内部中英切换」和「fcitx5 输入法开关
（abc ↔ keyboard-us）」，实测日志中 Shift 一次即把 IM 切到 keyboard-us。此时再输入字母会直接进应用。
建议二者只保留一个：要么把 `TriggerKeys` 改回 `Control+space` 一类组合，要么把 abc 的
`modeSwitchKey` 设为 `None` 交给 fcitx5 管理。

## 10. 词库接入（2026-10-29 18:00）

预编辑修复后遗留的问题只有词库：原 `index.abcdix` 仅 416 字节、一条 `ceshi → 测试`。
现直接把 Rime（rime-ice）词库编译成引擎索引，已上线。

### 10.1 做法

```text
~/.local/share/fcitx5/rime/ice/rime_ice.dict.yaml
  ├─ cn_dicts/8105     字表
  ├─ cn_dicts/base     基础词库
  ├─ cn_dicts/ext      扩展词库
  ├─ cn_dicts/tencent  ← 跳过：只有 text/weight 两列，没有 code
  └─ cn_dicts/others   杂项
        ↓ merge_rime_dict.py（展开 import_tables，输出 text/code/weight）
        ↓ abc_dict_import（A2）
index.abcdix
```

工具落在 `tools/dict/`：

- `merge_rime_dict.py`：递归展开 `import_tables`，按 `columns` 取字段，输出表体；
- `build-rime-index.sh`：合并 → 编译 → 备份 → 安装 → 可选重启，一条命令可复跑。

### 10.2 实测结果

```text
表体行数        891,731
记录数          891,471（去重 260 条）
不同 code 数    788,104
索引大小        46,256,310 字节（约 45 MB），crc32 0xCE1090CF
导入耗时        ~1.7 s（编译）/ ~1.7 s（合并）
fcitx5 RSS      约 95 MB
```

功能验证（目标机 GTK 客户端，press+release 成对）：

| 输入 | 结果 |
|---|---|
| `nihao` + 空格 + 空格 | `你好` |
| `zhongguo` + 空格 + 空格 | `中国` |
| `wo` + 空格 + 空格 | `我` |
| `shurufa` + 空格 + 空格 | `输入法` |

同输入产出同字节（两次独立构建 crc32 一致），符合索引格式的确定性要求。

### 10.3 尚未纳入

- **`cn_dicts/tencent`（98 万行）**：该表只有 `text weight` 两列，拼音由 Rime 方案按字表
  自动注音；本项目导入器要求显式 `code`。若要纳入，需要先按字表生成注音（等价于实现
  Rime 的 `speller` 自动注音），属于新功能而不是配置。
- 多音字、词组自动注音、用户词频学习都还没有；当前是按 code 前缀查表的静态候选。

### 10.4 回滚

旧索引已备份：`~/.local/share/abc/index.abcdix.mini-20261029`（416 字节）。
覆盖回该文件并重启 fcitx5 即回到接入前状态。

## 11. 预编辑「拖影/重影」（2026-10-29 18:05）

用户反馈：预编辑框有拖影。

### 11.1 定位

`AbcState::setPreedit()` 原来同时写两个字段：

```cpp
panel.setPreedit(text);        // fcitx 输入窗自己画
panel.setClientPreedit(text);  // 客户端在光标处内联画
```

而 `fcitx5 5.1.23` 的 `classicui` 在 `InputWindow::update()` 里是**无条件**读面板预编辑的：

```cpp
if (!inputPanel.empty()) {
    preedit = instance->outputFilter(inputContext, inputPanel.preedit());
    ...
}
```

它不检查客户端是否声明了 `CapabilityFlag::Preedit`。所以同一串字母会被画两遍：
应用内联一份（光标处）+ fcitx 悬浮窗一份（光标附近）。窗口每按一键重排一次，
两处文字位置不完全重合，看起来就是拖影/重影。

`InputPanel` 的两个字段语义（`fcitx/inputpanel.h`）：

- `setPreedit()`：由输入法 UI 绘制（面板预编辑）；
- `setClientPreedit()`：交给客户端内联绘制（client preedit）。

引擎应当**二选一**，不能同时设置。

### 11.2 修复

```cpp
const bool clientSide =
    engine_->preeditInClient() &&
    ic_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
if (clientSide) {
    panel.setClientPreedit(text);
    panel.setPreedit(fcitx::Text());   // 面板留空，避免重复绘制
} else {
    panel.setClientPreedit(fcitx::Text());
    panel.setPreedit(text);            // 客户端不支持时回退到输入窗
}
```

效果：客户端支持 preedit 时只有内联预编辑，输入窗在组合阶段不再出现（只有候选阶段才出现）；
不支持 preedit 的客户端仍然走输入窗，不会丢预编辑。

### 11.3 待确认

修复已装到目标机。若仍有拖影，需要用户提供录屏 GIF 判断是
窗口动画/合成器模糊（KWin blur）还是别的原因。

## 12. 选择态方向键（2026-10-29 18:10）

需求：出候选后，上下键翻页、左右键移动高亮；**移动本身不能上屏**。

### 12.1 原行为

方向键不在转换表里 → `kUnknown` → 核心按「未处理键策略」提交缓冲原文并转发。
所以候选一出来，按一下左右键就把整串字母提交了。

### 12.2 改动

| 文件 | 改动 |
|---|---|
| `source/include/abc/key.h` | 新增 `kLeft/kRight/kUp/kDown`（11..14） |
| `source/fcitx5-addon/keyconv.h/.cpp` | `Left/Up/Right/Down`（0xff51..0xff54）映射为对应键码，与功能键一样不受组合修饰影响 |
| `source/core/engine_session.cpp` | `dispatchSelecting`：`kLeft/kRight` 移动 `highlight` 并夹在页内，**消费但不提交**；`kUp/kDown` 与 `kPageUp/kPageDown` 合并；`dispatchComposing` 里方向键仍按未处理键（组合中按方向键 = 提交原文并移光标，符合常规输入法习惯） |
| `docs/interfaces/engine.md` | 2.1 转换表与 5.4 转移表同步 |

### 12.3 验证

单元测试（本地）：`test_keyconv` 方向键映射；`test_session_driver::testSelectingArrowNavigation`
（右移 → highlight+1 且 `effects` 为空；到页尾夹住；左移回退；下翻页 page+1 且 highlight 归零；
首页上翻被消费但不越界；移动后空格提交的是高亮项）。

目标机端到端（GTK 客户端，rime 词库）：

| 操作 | 提交 |
|---|---|
| `nihao` 空格 空格 | 你好（首候选） |
| `nihao` 空格 **→** 空格 | **拟好**（高亮右移一位后提交） |
| `shi` 空格 空格 | 是（第 1 页首项） |
| `shi` 空格 **↓** 空格 | **始**（第 2 页首项） |

三次操作中间都没有出现「按方向键就上屏」。

## 13. 组字框样式（对齐 QQ 拼音智能ABC模式，2026-10-29 18:15）

用户提供了 QQ 拼音 ABC 模式的两张截图：

- 打字时：光标下方出现**带边框的组字框**，框里是原始字母（`zhegeshitu`），应用内不显示；
- 按空格后：组字框里换成候选词（`这个视图`），旁边出现 1..9 候选列表。

### 13.1 改动

| 位置 | 改动 |
|---|---|
| `preeditInClient` 默认值 | `true` → **`false`**（`abc_config.h`、`deploy/abc/config.ini`、`config.md` 同步）：默认走 fcitx 组字框 |
| `ui_mapping` | `mapResult(result, preeditInClient)` → `mapResult(result)`：本层只负责产出预编辑文本，放客户端还是放组字框由执行层按配置+客户端能力决定 |
| `ui_mapping` 选择态 | 组字框内容改为**当前高亮候选**（`kSelecting` 且 `highlight >= 0`），左右移动高亮时框内文字跟着变，对齐智能ABC 手感 |
| 目标机 `classicui.conf` | `Vertical Candidate List=True`（竖排候选，接近截图）；原文件备份 `classicui.conf.bak-20261029` |

组合态仍是字母缓冲，选择态是候选词——和 QQ 拼音一致。

### 13.2 验证

目标机（`preeditInClient=false` 后）：

```text
[summary] preedit_updates=0 commits=1 final_preedit="" entry_text=""
[commit #1] "你好"
```

`preedit_updates=0` 说明客户端不再内联绘制（预编辑已交给 fcitx 组字框），
`commits=1` 说明提交链路正常。

### 13.3 回退

- 想要内联预编辑：`~/.config/abc/config.ini` 改回 `preeditInClient = true`，重启 fcitx5。
- 想要横排候选：`~/.config/fcitx5/conf/classicui.conf` 改 `Vertical Candidate List=False`。

## 14. 简拼 / 混拼（2026-10-29 18:20）

需求：像 QQ 拼音 ABC 模式那样，敲 `brshuo`（b + r + shuo）能出「比如说」。

### 14.1 做法

引擎的查询是「按 code 前缀查表」，索引里存的是全拼 `birushuo`，`brshuo` 不是它的前缀，
所以要在**数据层**补码：`merge_rime_dict.py --abbr-max-syllables N` 为 2..N 音节的词
额外生成两条码：

| 码型 | `bi ru shuo` | 用途 |
|---|---|---|
| 全拼 | `birushuo` | 原样保留 |
| 全首字母 | `brs` | 纯简拼 |
| 首字母 + 末音节全拼 | `brshuo` | 混拼（用户截图里的打法） |

单音节词不生成。代价是记录数约 2.6 倍。

没有改引擎和索引格式——`code` 本来就是任意 a-z 串，多码即多记录，按 (code, text) 去重。

### 14.2 实测

```text
表体 2,286,897 行 → 2,283,643 条记录 / 1,227,100 个 code
index.abcdix 106,273,882 字节（约 102 MB），crc32 0x0FE9C483
fcitx5 RSS 约 170 MB（开启简拼前约 95 MB）
```

| 输入 | 提交 |
|---|---|
| `brshuo` | 比如说 |
| `brs` | 比如说 |
| `nh` | 女孩 |
| `zhrmin` | 中华人民 |

### 14.3 取舍

- 目标是 7.7 GB 内存、无 swap 的机器，170 MB 常驻可以接受，但确实偏重。
- 内存吃紧时：`--abbr-max-syllables 3`（只给 2..3 音节词生成，索引约 78 MB），
  或 `0` 关闭简拼。
- 更彻底的做法是让引擎按「音节」匹配（索引存音节边界，查询时做混拼展开），
  索引不会膨胀，但要改索引格式到 v1.1 并重写查询，属于后续工作。
