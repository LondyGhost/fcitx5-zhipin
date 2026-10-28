# A1 模块说明 — 输入状态机核心（core）

> 实现方：A1。本文档面向集成验收负责人、A4（addon 适配层）与后续维护者。
> 契约依据：`docs/interfaces/engine.md` v1.0.0（rc1）、`docs/interfaces/dictionary.md`（Dictionary 抽象）、`docs/interfaces/build.md`。

## 1. 范围与文件清单

纯逻辑输入状态机：不依赖 fcitx5、不读词库文件、不读配置文件、不访问文件系统、无日志、无全局/静态可变状态。候选查询只经构造时注入的 `abc::Dictionary` 抽象（借用，不拥有）。

| 文件 | 内容 |
|---|---|
| `source/core/engine_session.cpp` | 全部实现：`EngineSessionImpl`（`abc::EngineSession` 具体类）+ 工厂 `abc::createEngineSession` |
| `source/core/CMakeLists.txt` | 库目标 `abc_core`（静态库，PIC；仅链接 `abc_interfaces` + `abc_build_options`） |
| `tests/core/CMakeLists.txt` | 测试目标 `abc_test_core`，CTest 测试名 `abc_core_tests` |
| `tests/core/fake_dictionary.hpp` | 契约保真 Fake Dictionary（冻结排序 4.1、精确 total、offset 回显、脚本注入） |
| `tests/core/test_harness.hpp` | 零依赖断言框架 |
| `tests/core/test_util.hpp` | 快照断言、契约不变量检查（4.1 不变量 1–6）、字符核算探针（不变量 7） |
| `tests/core/test_letters_commit.cpp` | 字母/规范化/Enter/Backspace/Esc/溢出/空态转发/查询形态（16 用例） |
| `tests/core/test_candidates.cpp` | 候选选择/翻页/页边界/pageCount/冻结排序/无效数字/词典错误/错误不粘滞（17 用例） |
| `tests/core/test_modes_forward.cpp` | 中英文模式/英文态转发/未处理键策略/切换策略选项（14 用例） |
| `tests/core/test_factory_reset.cpp` | 工厂校验/reset/非法命令字段（10 用例） |
| `tests/core/test_invariants.cpp` | 字符核算/会话隔离/确定性/全状态×全按键扫描（11 用例） |

## 2. 状态模型

模式即状态（`abc::Mode`）：`kEnglish`、`kChineseEmpty`、`kComposing`、`kSelecting`。
“停用”不在核心建模——addon 在输入法停用/焦点丢失时调用 `reset()`。

会话内状态：规范化缓冲 `B`（小写字母，唯一所有者）、当前页候选、`page`（0 基）、`pageCount`、`highlight`。

实现维护的快照不变量（engine.md 4.1，均有测试）：

1. `preedit == ""` ⟺ `mode ∈ {kEnglish, kChineseEmpty}`；
2. `pageCount > 0` ⟹ `0 < candidates.size() <= pageSize`；
3. `pageCount == 0` ⟹ candidates 空、`page == 0`、`highlight == -1`；
4. `kSelecting` ⟹ `highlight ∈ [0, candidates.size())`；`kComposing` 有候选 ⟹ `highlight == -1`（预览不激活）；
5. `effects` 含 `kCommitText` ⟹ 组合已清空（preedit 空、pageCount 0、mode ∈ {kChineseEmpty, kEnglish}）；
6. `consumed == false` 且无 effects ⟹ 快照与按键前一致；
7. 每个字母字符：进入缓冲、被提交、或因不消费而转发——无静默丢弃路径（由核算探针逐步验证）。

## 3. 按键转移表（实现与冻结表逐行对应）

对照 engine.md 第 5 节，此处只列实现要点与边界补充：

| 状态 | 按键 | 行为摘要 |
|---|---|---|
| English | `kModeSwitch` | 消费 → `kChineseEmpty`，无 effects |
| English | 其他一切 | 不消费（适配层转交 fcitx5），不查词典 |
| ChineseEmpty | `kCharacter` 字母 | 消费；规范化小写入缓冲 → `kComposing`；lookup(offset=0) |
| ChineseEmpty | 其他一切（含退格/回车/Esc/空格/数字/标点） | 不消费、无变化 |
| Composing | 字母 | 消费；追加（超 `maxBufferChars` 不入缓冲、**不消费**、状态不变）；lookup |
| Composing | `kSpace` 有候选 | 消费 → `kSelecting`，`highlight=0`，页不变 |
| Composing | `kSpace` 无候选 | 按 `spaceNoCandidatePolicy`：默认 `kCommitRawAndForward`=**不消费** + `[CommitText{B}]` → `kChineseEmpty`；或 `kKeepBufferAndForward`=不消费 + 保留组合 |
| Composing | `kBackspace` | 消费；删末字符；空 → `kChineseEmpty`；否则 lookup |
| Composing | `kEnter` | 消费；`[CommitText{B}]`（原文，非候选）→ `kChineseEmpty` |
| Composing | `kEscape` | 消费；`[ClearComposition]`；不提交 → `kChineseEmpty` |
| Composing | `kDigit`/`kPageUp`/`kPageDown`/`kPunctuation`/`kUnknown` | 未处理键策略 4.3：`[CommitText{B}]` + **不消费**（键与原文均不丢） |
| Composing/Selecting | `kModeSwitch` | 消费；按 `modeSwitchCompositionPolicy`：默认提交原文 `[CommitText{B}]`，或 `[ClearComposition]` → `kEnglish` |
| Selecting | `kDigit` d，页内第 d 项存在 | 消费；提交该项（与高亮无关）→ `kChineseEmpty` |
| Selecting | `kDigit` d，无对应项 | **不消费**；状态不变；键转发；缓冲保留 |
| Selecting | `kSpace` | 消费；提交当前高亮候选（v1 恒页首） |
| Selecting | 字母 | 消费；追加 → `kComposing`（无候选的 Selecting 不存在） |
| Selecting | `kBackspace` | 消费；删末字符 → `kComposing` 或 `kChineseEmpty` |
| Selecting | `kPageUp`/`kPageDown` | 消费；页边界内移动并 lookup(offset=page×pageSize)、`highlight=0`；边界上消费但无变化 |
| Selecting | `kEnter`/`kEscape` | 同 Composing |
| Selecting | `kPunctuation`/`kUnknown` | 未处理键策略 |
| 任意态 | 非法命令字段 | `error=kInvalidCommand`、不消费、状态不变（engine.md 8.2） |

规范化：`A..Z` → 小写入缓冲；原文提交（Enter/未处理键/切换默认策略）提交的是**规范化后的小写缓冲**（契约 4.2，保留原始大小写 v1 明确不做）。

## 4. 候选查询与分页

- 缓冲每次变化（增/删）→ 一次 `lookup(B, pageSize, 0)`；翻页 → `lookup(B, pageSize, page×pageSize)`。
- `pageCount = ceil(total / pageSize)`，total 取词典返回的**精确总数**。
- 翻页守卫：`page+1 < pageCount`（下）/ `page > 0`（上）；越界为消费但无变化。
- lookup `status != kOk` → `error=kDictionaryError`，按无候选处理：缓冲与组合保留；若处于 `kSelecting` 则回落 `kComposing`（不变量 4）。同一按键内不重试。
- 防御：若本页 items 为空（词典不一致），同样回落 `kComposing` 并清零页状态，保证不变量 2/3/4 恒成立。

## 5. 错误处理

- 无异常跨越模块边界；错误经 `EngineError` 返回。
- `kInvalidCommand`：`kCharacter/kPunctuation` 的 `unicode==0`，或 `kDigit` 的 `digit ∉ 1..9`。
- `kDictionaryError`：见第 4 节。`kDictionaryUnavailable` 为契约保留值，本实现不产生（工厂对空词典返回 `nullptr`）。
- 工厂校验（`createEngineSession`）：`pageSize ∈ [1,9]`、`maxBufferChars ∈ [1,256]`、`initialMode ∈ {kEnglish, kChineseEmpty}`、两个策略枚举值合法、词典非空；违规返回 `nullptr`。

## 6. 构建、测试命令与结果

### 6.1 标准构建（Linux，交付验收用；遵循 build.md 第 5 节）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build            # 含目标 abc_core 与 abc_test_core
ctest --test-dir build --output-on-failure   # 测试名 abc_core_tests
```

### 6.2 实际执行的本地验证（Windows 10, MinGW g++ 16.1.0，无 CMake 环境）

核心为纯逻辑，本地以与 `abc_build_options` 等价的告警集直接编译验证：

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic \
    -Isource/include -Itests/core \
    source/core/engine_session.cpp \
    tests/core/test_main.cpp tests/core/test_letters_commit.cpp \
    tests/core/test_candidates.cpp tests/core/test_modes_forward.cpp \
    tests/core/test_factory_reset.cpp tests/core/test_invariants.cpp \
    -o abc_test_core
./abc_test_core
```

结果：`ran 68 test cases, 0 failed assertions`，退出码 0，编译零告警（2026-10-28，本机；含新增回归用例 DictionaryErrorNotStickyAcrossKeystrokes）。
**限制披露**：本机无 CMake/Ninja，6.1 的 CMake 路径未在本机执行；两个模块 CMakeLists 按 build.md 契约编写（目标名、`abc_interfaces`/`abc_build_options` 链接、`add_test`），已由下节目标机复跑验证。C++17、仅标准库依赖，编译器差异风险低。

### 6.3 目标机干净环境复跑（2026-10-28，负责人更替后复验）

提交 `15a3d9f`（A1 修正）随全树 `f35d867`（含 A2 交付）以 `git archive` 导出
（归档 sha256 `fd71fe0d2f4d1f386d8b551be5c8ed7f24a8515bf595b7eb71ed87ade9ef5d38`），
在 `192.168.1.250:47672`（CatOS，GCC 16.2.1，CMake 4.4.4，Ninja 1.13.2）全新目录
`~/abc-verify-f35d867/` 复跑（仅构建，不安装）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build          # 45/45 目标，零告警
ctest --test-dir build --output-on-failure
```

结果：**100% tests passed out of 14**（含 `abc_interface_smoke`、`abc_core_tests`
（68 用例 0 失败）、`abc_dict_import_tests`（47 用例）、A3/A4/A5 各测试与部署脚本语法检查）。
`/usr`、`/opt` 只读审计无本项目产物（仅历史 abcabbr 试验残留，与轮次 1 记录一致）。
完整输出：`D:\workbuddy\abc-build-local\target-verify-f35d867.log`（仓库外，脱敏）。

## 7. 测试对验收标准的覆盖映射

| A1 验收项 | 覆盖 |
|---|---|
| 方案按键表正常/边界/错误路径 | 全部 5 个测试文件的用例；`InvariantSweepAllStatesAllKeys`（4 起点 × 11 键型） |
| fake Dictionary：有候选/无候选/多页/重复候选 | `populateNi/populateMa` 词条表 + 脚本模式（重复候选、错误状态、空结果） |
| 字符不静默丢失 | `Probe` 核算：`typed == committed + preedit + removed + forwarded`，每个混合序列每步校验（不变量 7） |
| 候选高亮/页码一致 | `highlight` 恒等断言 + 不变量 4 检查 + 翻页 offset/边界用例 |
| 核心独立构建（无 fcitx5 运行环境） | 仅标准库 + `abc_interfaces`；冒烟测试的依赖方向扫描同样适用 |
| 无全局状态、可重复测试 | `SameInputSequenceIsDeterministic`、`TwoSessionsHoldIndependentBuffers`；实现无任何静态可变状态 |

## 8. 契约确认结论（A1，2026-10-28 复验更新）

**结论：确认（附一条待 A0 评估的措辞澄清请求）。** 对照 A1 任务书“接口边界/硬性要求”逐条复核：转移表完整可测、溢出/无候选/未处理键策略明确、fake Dictionary 接口够用、`EngineSession` 单点持有缓冲、核心不反向依赖 addon——均满足。

1. **（措辞澄清请求，待 A0 评估转录）engine.md 4.1 不变量 6 与 4.3 的字面矛盾**：不变量 6 开头写“`consumed == false` 时模式与快照内容不变”，但 4.3 的未处理键策略在 `consumed == false` 时会清空组合并变更模式（快照确实变了）。当前 f314103 文本已在句中补充“但 `effects` 仍可能非空……addon 对未消费按键也必须执行 effects”，实现按此执行（未处理键：先 `CommitText{B}`、清组合、模式切换、键不消费）；本实现的测试不变量 6 检查采用等价语义（“未消费且无 effects ⟹ 快照不变”）。请 A0 将不变量 6 改写为“`consumed == false` 且 `effects` 为空时快照不变；`effects` 非空时 addon 对未消费按键也必须执行 effects”，消除字面矛盾（纯措辞澄清，PATCH 级，无行为变更）。同时请 A0 将本行确认结论转录至 `docs/interfaces/confirmations.md`（该文件属 A0 所有权）。
2. **（确认备注，建议文档化）`maxBufferChars` 上限 256 与词典 `codePrefix ≤ 128` 字节的组合**：缓冲超过 128 字节时 lookup 按契约 4.2 得 `kInvalidArgument`，核心映射为 `kDictionaryError` 按无候选处理（数据不丢，Enter 可提交原文）。行为已定义且安全，建议 A0 在 engine.md 或 dictionary.md 注明该交互，避免 A3/A4 误判为缺陷。

> 复验备注：本次复验未发现除 §11 记录的 error 语义问题外的其它实现缺陷；冻结契约（f314103）与公共头自 a6d95ac 以来零变更（git diff 确认），A1 实现与当前基线契约完全一致。

## 9. 已知限制

1. **highlight 不可导航**：v1 高亮恒为当前页首（进入 Selecting/翻页时置 0），无左右键移动高亮——契约未定义高亮移动键，待后续版本。
2. **原文提交为规范化小写**：用户输入的大写字母不保留原始大小写（契约 4.2 明确 v1 不做）。
3. **缓冲 > 128 字节时无候选**（见第 8 节第 2 条），Enter 仍可提交原文。
4. **Composing 下数字/翻页键会提交原文并转发**：这是冻结转移表 5.3 的行为（数字选择仅在 Selecting 定义）。若集成验收认为 UX 不可接受，属契约行为变更，须经集成负责人审批后升版本。
5. **无候选空格默认提交原文且空格转发**：应用将收到空格字符（契约 4.3/5.3 冻结值）；`kKeepBufferAndForward` 策略可改为保留组合。
6. **未处理键的替代策略**（结束组合但不提交原文）未实现——冻结策略只有“提交原文后转发”一种。
7. ~~本机为 Windows/MinGW 验证（见 6.2），Linux 干净环境构建与 CTest 复跑待集成验收~~ 已于 2026-10-28 完成：目标机干净目录 CMake/Ninja 全树构建零告警、CTest 14/14 通过（见 6.3）。

## 10. 下游集成方式（A4）

- 构造：`abc::createEngineSession(config, dictionaryPtr)`；每输入上下文一个会话；词典生命期须覆盖会话。
- 按键循环：按 engine.md 2.1 转换表将 fcitx5 KeyEvent 翻译为 `KeyCommand` → `handle()`。
- 结果应用顺序（engine.md 4.4）：先按序执行 `effects`（`kCommitText` → fcitx5 commit；`kClearComposition` → 清组合窗），再按快照同步 preedit/candidates/page/highlight；`consumed == false` 时把原始按键交还 fcitx5（**也要先执行完 effects**——未处理键策略会带提交效果）。
- 焦点丢失/停用/上下文销毁：`reset()`（清空组合不提交）。
- 线程：会话方法只能在 fcitx5 事件线程串行调用；词典 lookup 需支持并发（A3 义务）。

## 11. 交付记录（2026-10-28，负责人更替后复验）

| 项 | 值 |
|---|---|
| 任务 | A1 输入状态机核心 |
| 负责人 | A1 代理（新任，与 A2 同一负责人合并承担；按验收标准 §1 披露自验风险，请集成负责人复核关键结论） |
| 提交号 | 实现 `a6d95ac`（原交付，基线契约未变）→ 修正 `15a3d9f`（error 语义）+ 本文档登记提交；修正提交变更仅 `source/core/engine_session.cpp`、`tests/core/test_candidates.cpp` |
| 接口契约版本 | 1.0.0-rc1（`abc/version.h`；契约基线 `f314103`，自 a6d95ac 以来零变更） |
| 变更文件清单（复验轮） | `source/core/engine_session.cpp`、`tests/core/test_candidates.cpp`、`docs/modules/core.md`（均在 A1 所有权内；未触碰 A0/A3–A6 文件） |
| 依赖与许可证 | 仅标准库 + `abc_interfaces`；无第三方依赖；无词库数据 |
| 构建与测试命令 | 见 §6.1（CMake/CTest）与 §6.2（本地 MinGW 直接编译） |
| 完整结果 | 本地：`ran 68 test cases, 0 failed assertions`，零告警。目标机干净目录：45/45 目标构建零告警，CTest **14/14 通过**（`abc_core_tests` 68 用例 0 失败），§6.3 |
| 脚本/日志 | `D:\workbuddy\abc-build-local\target-verify-f35d867.log`（目标机完整输出，仓库外，无凭据无真实用户输入） |
| 错误处理说明 | 见 §5；本轮修正：`error` 字段为每次调用语义（§11 上文） |
| 已知限制 | 见 §9（highlight 不可导航、原文提交为规范化小写等，均与轮次 1 一致） |
| 对验收记录的影响 | 轮次 1 的 A1「通过（任务级）」结论在 `15a3d9f` 上维持有效；修正为非阻断语义修正，新增 1 用例，不影响已验收行为 |
