# 引擎会话接口契约（engine.md）

对应公共头文件：`source/include/abc/key.h`、`source/include/abc/engine.h`。
实现方：A1（`source/core/**`）。消费方：A4（addon 适配层）。
本契约冻结于接口版本 1.0.0（状态见 [README.md](README.md)）。

## 1. 总览

```text
addon (A4)                          core (A1)
──────                              ─────────────────────────────
fcitx5 KeyEvent
  │ 转换（仅适配层知道 fcitx5 类型）
  ▼
KeyCommand ────────────────────────► EngineSession::handle(KeyCommand)
                                        │ 状态机 + 注入的 Dictionary::lookup
                                        ▼
EngineResult ◄────────────────────── EngineResult（快照 + 副作用命令）
  │ 按 effects 顺序执行，再按快照同步 UI
  ▼
fcitx5 commit / preedit / candidate list
```

- 核心不包含任何 fcitx5 类型；按键语义在适配层一次性翻译成 `KeyCommand`。
- `EngineSession` 是输入缓冲与组合状态的**唯一**所有者；addon 无缓冲。
- 核心通过构造时注入的 `Dictionary*` 查询候选，不解析词库文件、不读配置、不访问文件系统。

## 2. KeyCommand（`abc/key.h`）

```cpp
enum class KeyCode {
  kUnknown = 0,      // 其余一切按键；按“未处理键策略”处理（见 4.3）
  kCharacter = 1,    // 缓冲字符；unicode 携带 UTF-32 码点
  kDigit = 2,        // 候选选择数字；digit 携带 1..9
  kPunctuation = 3,  // 标点；unicode 携带 UTF-32 码点
  kBackspace = 4,
  kEnter = 5,
  kSpace = 6,
  kEscape = 7,
  kPageUp = 8,
  kPageDown = 9,
  kModeSwitch = 10,  // 配置的中英文切换热键，由适配层识别后发出
};

struct KeyCommand {
  KeyCode code = KeyCode::kUnknown;
  char32_t unicode = 0;  // 仅 kCharacter / kPunctuation 使用
  uint8_t digit = 0;     // 仅 kDigit 使用，取值 1..9
};
```

### 2.1 适配层（A4）转换表

fcitx5 键事件到 `KeyCommand` 的唯一合法映射（键名按目标机 fcitx5 键语法）：

| fcitx5 KeyEvent | KeyCommand |
|---|---|
| 可打印字母 `a..z`、`A..Z` | `kCharacter`，unicode = 对应小写码点 |
| 数字 `1..9`（无 Ctrl/Alt 修饰） | `kDigit`，digit = 1..9 |
| 数字 `0`、无组合修饰的其他可打印标点 | `kPunctuation`，unicode = 码点 |
| `Backspace` | `kBackspace` |
| `Return` / `KP_Enter` | `kEnter` |
| `space` | `kSpace` |
| `Escape` | `kEscape` |
| `Prior` / `KP_Prior`（PageUp） | `kPageUp` |
| `Next` / `KP_Next`（PageDown） | `kPageDown` |
| `Left` / `Right`（方向键） | `kLeft` / `kRight`：选择态移动高亮，不提交 |
| `Up` / `Down`（方向键） | `kUp` / `kDown`：选择态翻页，等价 `kPageUp` / `kPageDown` |
| 配置的中英文切换热键（`config.md` 的 `modeSwitchKey`） | `kModeSwitch` |
| 其余全部（Home/End、Tab、组合键、带 Ctrl/Alt 的键等） | `kUnknown` |

规则：

- 大小写：适配层**可以**直接传大写；核心会把 `A..Z` 规范化为 `a..z` 入缓冲（见 4.1）。适配层传小写是推荐形式。
- `kCharacter` 携带非字母码点时，核心按 `kPunctuation` 对待（宽容降级，见 4.2）。
- 带修饰键（Ctrl/Alt/Super）的字母、数字、标点一律映射 `kUnknown`，不得映射成缓冲输入。
- 转换表覆盖不了的键必须落为 `kUnknown`，禁止适配层自行消费。

### 2.2 命令字段合法性

| KeyCode | unicode | digit |
|---|---|---|
| `kCharacter`/`kPunctuation` | 必须 ≠ 0 | 忽略 |
| `kDigit` | 忽略 | 1..9 |
| 其他 | 忽略 | 忽略 |

`kDigit` 携带 `digit` 超出 1..9 时按 `EngineError::kInvalidCommand` 返回（4.5）。

## 3. EngineConfig（`abc/engine.h`）

构造会话时注入的不可变行为参数（来源见 [config.md](config.md)）：

```cpp
enum class SpaceNoCandidatePolicy { kCommitRawAndForward = 0, kKeepBufferAndForward = 1 };
enum class ModeSwitchCompositionPolicy { kCommitRaw = 0, kClearComposition = 1 };

struct EngineConfig {
  Mode initialMode = Mode::kChineseEmpty;          // 仅允许 kEnglish / kChineseEmpty
  uint32_t pageSize = 9;                           // 候选页大小，1..9
  uint32_t maxBufferChars = 64;                    // 缓冲字符上限，1..256
  SpaceNoCandidatePolicy spaceNoCandidatePolicy = SpaceNoCandidatePolicy::kCommitRawAndForward;
  ModeSwitchCompositionPolicy modeSwitchCompositionPolicy = ModeSwitchCompositionPolicy::kCommitRaw;
};
```

- `pageSize` 上限为 9：候选用数字 1–9 选择，页内元素必须能被数字索引。
- `maxBufferChars` 溢出策略：超限字符**不入缓冲**，按键不消费（转发给应用），状态不变。字符不会静默丢失（它以原文形式出现在应用中），缓冲保持可用。
- 工厂校验：`pageSize ∉ [1,9]`、`maxBufferChars ∉ [1,256]`、`initialMode` 为组合态、或策略枚举值非法时，`createEngineSession` 返回 `nullptr`。

## 4. EngineResult（`abc/engine.h`）

```cpp
enum class Mode { kEnglish = 0, kChineseEmpty = 1, kComposing = 2, kSelecting = 3 };
// “停用/禁用”不是核心模式：addon 在输入法停用或焦点丢失时调用 reset()。

enum class EngineError {
  kNone = 0,
  kDictionaryUnavailable = 1,  // 保留值：会话未绑定可用词典（防御性，v1.0 不应出现）
  kDictionaryError = 2,        // 词典查询返回错误；按无候选处理并标注
  kInvalidCommand = 3,         // 命令字段非法（如 kDigit 越界）
};

struct Effect {
  enum class Kind { kCommitText = 0, kClearComposition = 1 };
  Kind kind = Kind::kClearComposition;
  std::string text;  // 仅 kCommitText 使用，UTF-8
};

struct EngineResult {
  bool consumed = false;              // 是否消费该按键（false 时 addon 必须把键交还 fcitx5）
  Mode mode = Mode::kEnglish;
  std::string preedit;                // 预编辑 = 当前规范化缓冲；空 = 无 preedit
  std::vector<Candidate> candidates;  // 当前页候选（lookup 顺序的副本）
  uint32_t page = 0;                  // 当前页，0 基
  uint32_t pageCount = 0;             // 总页数；0 = 当前无候选页
  int32_t highlight = -1;             // 页内高亮 0 基；-1 = 无高亮
  std::vector<Effect> effects;        // 必须按序执行的副作用命令
  EngineError error = EngineError::kNone;
};
```

### 4.1 不变量（A1 测试必须覆盖）

1. `preedit == ""` ⟺ `mode ∈ {kEnglish, kChineseEmpty}`；否则 `preedit` 为缓冲本身（小写字母）。
2. `pageCount > 0` ⟹ `candidates.size() == min(pageSize, total - page*pageSize)` 且 `0 < candidates.size() <= pageSize`。
3. `pageCount == 0` ⟹ `candidates` 为空且 `page == 0` 且 `highlight == -1`。
4. `mode == kSelecting` ⟹ `highlight ∈ [0, candidates.size())`；`mode == kComposing` 且有候选 ⟹ `highlight == -1`（预览但不激活）。
5. `effects` 中出现 `kCommitText` ⟹ 本结果同时把组合清空（`preedit == ""`、`pageCount == 0`、`mode == kChineseEmpty` 或 `kEnglish`）。
6. `consumed == false` 时模式与快照内容不变，但 `effects` 仍可能非空（未处理键策略：先提交原文再转发）——**addon 对未消费按键也必须执行 effects**。
7. 每个被 `kCharacter` 送入的字符最终满足其一：进入缓冲、被 commit、或因不消费而原样转发；不存在静默丢弃路径（溢出与无候选策略均已显式定义）。

### 4.2 preedit 内容

- 内容 = 规范化缓冲（小写字母，如 `nihao`），无页码、无装饰。
- 用户输入的大写字母规范化为小写后进入缓冲；原文提交（Enter、未处理键）提交的是**规范化后的缓冲**。保留原始大小写的 v1 明确不做（已知限制）。

### 4.3 未处理键策略（kPunctuation / kUnknown / Composing|Selecting 下的 kDigit / kPageUp / kPageDown）

统一冻结为**提交原文后转发**：

- `Composing` / `Selecting`：`effects = [CommitText{缓冲}]`，组合清空，`mode = kChineseEmpty`，`consumed = false`（键交还 fcitx5 转发到应用）。
- `ChineseEmpty` / `English`：`consumed = false`，无副作用。
- 依据：整体方案第 5 节“不得静默丢键”；“先提交原文，再让 fcitx5 决定是否转发”。

### 4.4 effects 执行顺序（A4 义务）

1. 按数组顺序执行 `effects`（先提交文本后清窗口：fcitx5 的 commit 与 preedit 清除按此顺序调用）。
2. 然后按快照同步 preedit / 候选列表 / 高亮 / 页码。
3. `consumed == false` 时最后把按键交还 fcitx5。

## 5. 冻结按键转移表

规范缓冲记为 `B`（空串或小写字母串）。`lookup()` 失败时（`error = kDictionaryError`）一律按“无候选”分支处理，缓冲与 preedit 保留。

### 5.1 English

| 输入 | 行为 |
|---|---|
| `kModeSwitch` | 消费；`mode = kChineseEmpty`，无 effects |
| 其他一切 | 不消费；无变化（适配层转交 fcitx5） |

### 5.2 ChineseEmpty

| 输入 | 行为 |
|---|---|
| `kCharacter`（字母） | 消费；`B = 规范化(字符)`；`mode = kComposing`；lookup；preedit = `B`；有匹配则展示候选页（`pageCount > 0`，`highlight = -1`） |
| `kCharacter`（非字母） | 按 `kPunctuation` 处理 |
| `kPunctuation` / `kSpace` / `kDigit` / `kBackspace` / `kEnter` / `kEscape` / `kPageUp` / `kPageDown` / `kUnknown` | 全部不消费；无变化（由应用或 fcitx5 处理） |
| `kModeSwitch` | 消费；`mode = kEnglish` |

### 5.3 Composing（`B` 非空，候选页未激活，`highlight = -1`）

| 输入 | 行为 |
|---|---|
| `kCharacter`（字母） | 消费；`B += 规范化(字符)`；若超 `maxBufferChars`：字符不入缓冲，**不消费**，状态不变（溢出策略）；否则重新 lookup，`page = 0` |
| `kCharacter`（非字母） | 按 `kPunctuation` 处理 |
| `kSpace`，有候选 | 消费；`mode = kSelecting`；`highlight = 0`；页不变 |
| `kSpace`，无候选 | 按 `spaceNoCandidatePolicy`：默认 `kCommitRawAndForward` = 不消费 + `effects=[CommitText{B}]` + `mode = kChineseEmpty`；或 `kKeepBufferAndForward` = 不消费 + 保留组合 |
| `kDigit` 1..9 | 未处理键策略（4.3）：提交原文后转发 |
| `kBackspace` | 消费；删除 `B` 末字符；`B` 空 → `mode = kChineseEmpty`，候选清空；否则重新 lookup，`page = 0` |
| `kEnter` | 消费；提交原文：`effects = [CommitText{B}]`；组合清空；`mode = kChineseEmpty`（不提交猜测候选） |
| `kEscape` | 消费；`effects = [ClearComposition]`；组合清空；不提交；`mode = kChineseEmpty` |
| `kPunctuation` / `kUnknown` / `kPageUp` / `kPageDown` | 未处理键策略（4.3） |
| `kModeSwitch` | 消费；按 `modeSwitchCompositionPolicy` 处理 `B`（默认提交原文）；`mode = kEnglish` |

### 5.4 Selecting（候选页激活，`highlight ∈ [0, 页大小)`）

| 输入 | 行为 |
|---|---|
| `kCharacter`（字母） | 消费；`B += 规范化(字符)`（溢出策略同上）；重新 lookup；`page = 0`；有匹配 → `mode = kComposing`（回到预览态），无匹配 → `mode = kComposing` 且 `pageCount = 0` |
| `kSpace` | 消费；提交**当前高亮候选**（默认页首）：`effects = [CommitText{候选.text}]`；组合清空；`mode = kChineseEmpty`；`error` 置 lookup 结果 |
| `kDigit` d，页内第 d 项存在 | 消费；提交该候选（同上），与高亮位置无关 |
| `kDigit` d，页内第 d 项不存在 | 不消费；状态不变（键转发；缓冲保留以便继续编辑） |
| `kBackspace` | 消费；删除 `B` 末字符；`B` 空 → `kChineseEmpty`；否则重新 lookup，`page = 0`，`mode = kComposing`，`highlight = -1`（本行为是对方案表格空缺处的冻结补充） |
| `kEnter` | 消费；提交**原文缓冲**（不是候选）；组合清空；`mode = kChineseEmpty` |
| `kEscape` | 消费；清空组合，不提交；`mode = kChineseEmpty` |
| `kPageUp` / `kUp`，`page > 0` | 消费；`page -= 1`；`highlight = 0`；候选页刷新 |
| `kPageUp` / `kUp`，`page == 0` | 消费；无变化（页边界） |
| `kPageDown` / `kDown`，`page + 1 < pageCount` | 消费；`page += 1`；`highlight = 0`；候选页刷新 |
| `kPageDown` / `kDown`，已到末页 | 消费；无变化（页边界） |
| `kLeft` | 消费；`highlight = max(0, highlight - 1)`；**不提交**、不退出选择态 |
| `kRight` | 消费；`highlight = min(页内末项, highlight + 1)`；**不提交**、不退出选择态 |
| `kPunctuation` / `kUnknown` | 未处理键策略（4.3）：提交原文后转发 |
| `kModeSwitch` | 消费；按 `modeSwitchCompositionPolicy` 处理 `B`；`mode = kEnglish` |

### 5.5 补充规则

- **lookup 时机**：每次缓冲变化（增/删字符）触发一次 `lookup(prefix = B, limit = pageSize, offset = 0)`；`pageCount = ceil(total / pageSize)`。翻页时 `lookup(prefix = B, limit = pageSize, offset = page * pageSize)`。
- **无候选的 Selecting 不存在**：缓冲变化导致无匹配时状态回落 `kComposing`。
- **lookup 错误**：`error = kDictionaryError`，按无候选分支执行；缓冲、preedit、组合状态保留（不丢数据）。同一按键内 lookup 错误不重试。
- **焦点/停用**：`reset()`，见第 6 节。

## 6. EngineSession 与工厂（`abc/engine.h`）

```cpp
class EngineSession {
 public:
  virtual ~EngineSession() = default;
  virtual EngineResult handle(const KeyCommand& command) = 0;  // 处理一个按键
  virtual EngineResult reset() = 0;                            // 清空组合，不提交
  virtual EngineResult snapshot() const = 0;                   // 当前状态快照，无副作用
  virtual const EngineConfig& config() const = 0;              // 构造配置的副本
};

// A1 提供实现。
std::unique_ptr<EngineSession> createEngineSession(const EngineConfig& config,
                                                   Dictionary* dictionary);
```

- `createEngineSession`：`dictionary` 必须非空；`config` 校验失败返回 `nullptr`（第 3 节）。会话**借用**词典，不拥有它；`dictionary` 必须比会话活得久。
- `reset()`：焦点丢失、输入法停用、输入上下文销毁前由 addon 调用。清空组合，不提交任何文本，`mode = config.initialMode`，返回值仅含清空语义的快照（`effects` 为空或仅 `ClearComposition`）。
- `snapshot()`：addon 需要无按键重绘（如焦点恢复）时使用。
- 实现要求：无全局/静态可变状态；多个会话完全独立；会话不派生线程。

## 7. 生命周期与线程约束（冻结）

| 对象 | 生命周期 | 线程约束 |
|---|---|---|
| `Dictionary` | 先于会话创建；必须晚于所有借用它的会话销毁 | 实现必须允许并发 `lookup()`（只读保证）；`loadDictionaryFromFile` 结果对象可跨线程共享 |
| `EngineSession` | 每个输入上下文一个；`unique_ptr` 独占销毁 | **非线程安全**：`handle/reset/snapshot/config` 只能在拥有该上下文的单个线程（fcitx5 事件循环线程）串行调用；禁止并发或跨线程调用 |
| `EngineResult` / `Candidate` / `CandidateBatch` | 纯值类型，无隐藏共享 | 可自由拷贝、移动 |
| `KeyCommand` | 纯值类型 | 同上 |

典型时序（A4）：addon 初始化 → `loadDictionaryFromFile` 成功 → 每个输入上下文 `createEngineSession` → 按键循环 `handle` → 上下文销毁或停用 → `reset()` → 析构会话；addon 卸载时最后析构词典。

## 8. 错误处理策略（冻结）

1. 不使用异常跨越模块边界；错误一律通过 `EngineError` / `DictionaryStatus` 返回值表达。
2. `handle` 对非法命令字段（2.2）返回 `error = kInvalidCommand`、`consumed = false`、状态不变——键交还 fcitx5，不丢数据。
3. lookup 失败按 5.5 处理；词典损坏在加载期暴露（`dictionary.md`），运行期查询不允许因词典数据崩溃。
4. 任何路径不得：终止进程、写文件、弹 UI、记录用户输入文本。
5. 诊断由调用方（addon）负责；核心不打印日志。

## 9. 调用示例

```cpp
// A4 适配层示意（省略 fcitx5 细节）
abc::EngineConfig cfg;                       // 由 A5 解析用户配置得到
cfg.pageSize = 9;
abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(indexPath);
if (d.status != abc::DictionaryStatus::kOk) return;   // 加载期错误，见 dictionary.md

auto session = abc::createEngineSession(cfg, d.dictionary.get());
if (!session) return;                        // 配置非法

abc::KeyCommand cmd;
cmd.code = abc::KeyCode::kCharacter;
cmd.unicode = U'n';
abc::EngineResult r = session->handle(cmd);
// r.consumed == true, r.mode == kComposing, r.preedit == "n"

cmd.code = abc::KeyCode::kModeSwitch;        // 用户按切换热键
r = session->handle(cmd);
// r.mode == kEnglish；此前若有缓冲，默认策略先 CommitText{B}

session->reset();                            // 焦点丢失：清组合，不提交
```

```cpp
// A1 测试可用的 Fake Dictionary 骨架
class FakeDictionary final : public abc::Dictionary {
 public:
  abc::LookupResult lookup(std::string_view prefix,
                           uint32_t limit, uint32_t offset) const override {
    // 按契约排序规则返回固定候选；支持 limit/offset 分页语义
  }
};
```
