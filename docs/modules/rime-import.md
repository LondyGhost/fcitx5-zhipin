# A2 模块说明 — Rime 词库导入器（dict-importer）

> 实现方：A2。本文档面向集成验收负责人、A3（runtime loader 消费方）与后续维护者。
> 契约依据：`docs/interfaces/index-format.md` v1.0、`docs/interfaces/dictionary.md`
> （字段/去重/stableId）、`docs/interfaces/build.md`、任务书 `tasks/A2-Rime词库导入器.md`。
> 关联提交：实现 `f35d867`（2026-10-28）。负责人更替后按当前仓库基线全新交付。

## 1. 范围与文件清单

离线 CLI 工具：把明确支持的 Rime/rime-ice 词库文本格式编译为引擎索引
（index-format.md v1.0），供 A3 `loadDictionaryFromFile` 消费。不执行
Rime Lua/Processor/词库内容；不读环境变量；不写 `/usr`、`/opt`。

| 文件 | 内容 |
|---|---|
| `source/dict-importer/dict_importer.hpp` | 核心 API：`compileIndexToBuffer` / `compileIndexFile` + 报告类型 |
| `source/dict-importer/dict_importer.cpp` | 全部实现：解析 → 归一化 → 去重 → 冻结排序 → 序列化 → 输出自检 → 写出 |
| `source/dict-importer/main.cpp` | CLI 入口（参数、`--help`/`--version`、退出码、报告输出） |
| `source/dict-importer/CMakeLists.txt` | `abc_dict_import`（可执行）+ `abc_dict_import_core`（静态库） |
| `tests/dict-importer/`（7 文件 + `fixtures/` 8 样本） | 47 用例：解析/编译 golden/CLI e2e/A3 交叉验证 |
| `docs/modules/rime-import.md` | 本文档（含交付记录） |

依赖方向（build.md §4）：`abc_dict_import*` 仅链接 `abc_interfaces` + 标准库；
禁止 fcitx5/X11/Wayland。测试目标 `abc_test_dict_import` 另链接 `abc_dictionary`
（A3 公共 API），用于交叉验证——build.md 允许的"CMake 目标链接"复用方式，
未修改 A3 任何文件。

## 2. 支持的输入格式子集（v1 冻结）

格式以真实 rime-ice 样本核实（2026-10-28 抓取 `cn_dicts/others.dict.yaml` 全文与
`rime_ice.dict.yaml` 聚合文件核对头部结构；另核对了 Rime 作者 lotem 的码表字段
说明 gist：字段顺序 = 文字、编码、权重（可选））。**证据仅为格式核实，任何第三方
词库数据均不入仓库。**

| 项 | v1 规则 |
|---|---|
| 编码 | UTF-8；文件头 UTF-8 BOM 剥离并计入报告；UTF-16 BOM → 致命错误 |
| 换行 | `\n`；行尾 `\r` 剥离（CRLF 与 LF 等价，产出索引逐字节相同）；单独 `\r` 视为内容字节（将触发诊断） |
| YAML 头 | 可选；`---` 与 `...` 之间仅平铺 `key: value` 行；`#`/缩进 `#` 注释与空行忽略 |
| 头部键 | `columns` / `import_tables` / `vocabulary` → **致命错误**（不得静默曲解；聚合文件请直接导入被引用的表文件）；其余键（name/version/sort/author…）忽略并登记进报告 |
| 表体行 | `text \t code [\t weight]`；2 或 3 个字段；1 字段（如 rime-ice 叠字行）→ 跳过并报告；≥4 字段（stem 列）→ 跳过并报告 |
| 注释 | 首个非空白字符为 `#` 的行（头部区与表体一致）；表体行**不做首尾空白裁剪** |
| code 归一化 | **去除空格分隔符并折叠 `A-Z` → `a-z`**；归一化后必须为 1..128 字节的纯 `a-z`。理由：rime-ice 编码列是音节空格分隔（如 `kong luo luo`），而引擎缓冲（A1）只容纳 `a-z`（engine.md 4.2/5.3），契约 dictionary.md 2.1 授权 A2 定义编码字符集；去除分隔符保持前缀补全语义 |
| text | 非空；严格合法 UTF-8；无控制字符（<0x20、0x7F）；≤512 字节 |
| weight | 可选，缺省 0.0；语法 `^[+-]?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?$`（前导/尾随点不支持）；strtod 以 C 小数点解析并要求整字段消费（调用方不得 setlocale）；结果必须有限；`-0` 归一化为 `+0`（位模式确定） |
| 转义 | **不支持转义序列**，反斜杠为字面字符（与 Rime 实际行为一致；`\n` 原样保留） |
| 诊断 | 全部报告 ASCII 文本 + 转义摘录（≤48 字节，非可打印字节 → `\xNN`），含行号，不静默吞行 |

## 3. 错误处理与退出码

| 退出码 | 含义 | 举例 |
|---|---|---|
| `0` | 成功且无跳过行 | minimal 样本 |
| `1` | 致命错误；**不写出任何索引** | 文件不可读；UTF-16；`import_tables`/`columns`/`vocabulary`；嵌套 YAML；缺 `...`；输出已存在；source 标签非法 |
| `2` | 索引已写出但有跳过行（报告含行号/原因/摘录） | 坏行样本（9 跳过行） |
| `3` | 输出自检失败（导入器缺陷；不写出） | CRC/顺序/stableId/计数不一致 |

报告（stdout，`--report` 可另存）为确定性文本：计数（行/空白/注释/忽略键/表体/有效/跳过/重复/记录/编码）、索引字节数与 CRC32、逐条跳过行定位。不含时间戳、主机信息、路径展开。

写侧义务（index-format.md §6）：输出前对缓冲做完整自检——冻结排序谓词与 A3 loader 相同、`(code,text)` 唯一、stableId 逐条复核、CRC 复算、recordCount/codeCount 与头部一致；任一失败 = 退出码 3，不写出。写出后回读校验长度；**绝不覆盖既有输出文件**（含输入即输出的情形）。

## 4. 确定性与资源上界

- 同一输入 + 同一 `--source` 重复编译 → **逐字节相同**（跨平台亦然：Windows MinGW 与 Linux GCC 16.2.1 产出 SHA-256 一致，见 §7）。
- 排序为字节序比较（无符号 memcmp 语义）；权重 IEEE-754 严格解析；报告无环境变量。
- 资源上界（有界解析，任务书"大文件流式或有明确内存上限"取后者）：输入 ≤ 512 MiB；有效词条 ≤ `kIndexMaxRecords`(10M)；词条常驻内存去重排序（离线工具，rime-ice 全量 ≈ 百万级可容纳）。

## 5. 构建、测试命令与结果

### 5.1 标准构建（遵循 build.md §5）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build            # 产出 build/source/dict-importer/abc_dict_import
ctest --test-dir build --output-on-failure   # 测试名 abc_dict_import_tests
```

### 5.2 实际执行的验证（两轮）

**本地（Windows 10，MinGW g++ 16.1.0，`-std=c++17 -Wall -Wextra -Wpedantic` 零告警）：**

```
ran 47 test cases (0 skipped), 0 failed assertions
```

（含 CLI e2e 与 A3 交叉验证；本地直接编译命令同 A1 惯例，测试以
`argv[1]=夹具目录 argv[2]=导入器路径` 传参。）

**目标机干净环境（2026-10-28，`192.168.1.250:47672`，CatOS，GCC 16.2.1，CMake 4.4.4，Ninja 1.13.2）：**

提交 `f35d867`（含 A1 修正 `15a3d9f`）以 `git archive` 导出（归档 sha256
`fd71fe0d2f4d1f386d8b551be5c8ed7f24a8515bf595b7eb71ed87ade9ef5d38`），全新目录
`~/abc-verify-f35d867/` 复跑（仅构建，不安装）：

- configure/build：**45/45 目标，零告警**；
- `ctest`：**100% tests passed out of 14**（`abc_dict_import_tests` 47 用例 0 失败、0 跳过；
  同时通过 smoke/A1/A3/A4/A5 全部 14 项）；
- CLI 实跑（目标机二进制）：
  - `--version` → `index format: 1.0 (abc/index_format.h)` / `interface: 1.0.0`；
  - minimal 样本两次导入 → `DETERMINISTIC`（`cmp` 相同），
    `sha256 = 26a78bd779bc1d16705764ab2cc89eefbca73f6d1bbe0fb23f4c7b8c3a6c5b6e`
    **与 Windows 构建逐字节一致（跨平台确定性）**；
  - bad_lines → `EXIT=2`，9 条跳过行逐行定位（`line 13: text exceeds 512 bytes` 等）；
  - import_tables → `EXIT=1` + `NO_INDEX_WRITTEN`；UTF-16 → `EXIT=1`；
- `/usr`、`/opt` 只读审计：无本项目产物。
- 完整输出（仓库外，脱敏，无凭据）：`D:\workbuddy\abc-build-local\target-verify-f35d867.log`、`target-verify-cli-f35d867.log`。

## 6. Fixtures、校验值与查询预期

### 6.1 Fixture 清单（全部自造最小样本；无第三方词库数据；随仓库分发，测试用途自由使用）

| 文件（`tests/dict-importer/fixtures/`） | 用途 | SHA-256 |
|---|---|---|
| `minimal.dict.yaml` | 头部/注释/权重/去重/golden 主样本 | `96bb578920b04cb0ec9cea9ce2a2cdae0d1e072cee14dda6900cf541164b1523` |
| `bad_lines.dict.yaml` | 9 类坏行 + 重复行（exit 2 路径） | `2f715e035d792dd18bf2251581be91cbedf7e85cf202faea99f738ca1b45e526` |
| `bounds_ok.dict.yaml` | code 恰 128 字节 + text 恰 512 字节 | `e53b34568bc90151963a40e365d1ee3c88b95a73c729b7c08859c6b6632830e2` |
| `no_header.txt` | 无 YAML 头纯码表（custom_phrase 风格） | `ad4f3a50945cd3f5530d52bfa95e3d260f93b60c7fe458acc1c28ff1f2da1053` |
| `columns_key.dict.yaml` | 致命：`columns` 键 | `b1d735a9445e30aa064d4a855770156a376e4792d70d51de75c883025079e9ef` |
| `import_tables.dict.yaml` | 致命：`import_tables` 聚合文件 | `9c6e8893a6159e9a25d611242037a88ccfe01467367514a840b867447bb983d0` |
| `no_table_end.dict.yaml` | 致命：缺 `...` 表体起点 | `6d60baca591e8c0e60223b51aa059aa442226a61ddf6e9f4e34c1863734d1d36` |
| `empty.dict.yaml` | 仅注释 → 合法空索引（48 字节） | `911b645bd19d02c42ec9e6f05ade53c5628183b162819dd50dddbee3c6cd6d3e` |

BOM/CRLF/UTF-16/非法 UTF-8 四类字节级样本由测试在临时目录合成（避免夹具依赖不可见字节）。

### 6.2 Golden 索引校验值（A2 实际生成的产物）

```bash
abc_dict_import --input tests/dict-importer/fixtures/minimal.dict.yaml \
                --output golden.ix --source cn_dicts/base
```

- 字节数 384；CRC32（对 `[0x20,EOF)`）`0xC044ED9C`；
- **SHA-256 `26a78bd779bc1d16705764ab2cc89eefbca73f6d1bbe0fb23f4c7b8c3a6c5b6e`**（Windows 与 Linux 一致）；
- 内容：8 记录 / 6 编码，存储序 = `ceshi(测试,55.5) → ma(马,70) → ma(妈,65) → ma(骂,60) → mama(妈妈,100) → ne(呢,10) → nihao(你好,100) → shijie(世界,0)`。

### 6.3 查询预期（A3 loader，契约 4.1 冻结排序）

| 查询 | 预期（test_cross.cpp 全部断言通过） |
|---|---|
| `lookup("nihao",9,0)` | total=1；`你好`/100/`cn_dicts/base`/stableId=`computeStableId(...)` |
| `lookup("ma",9,0)` | total=4；精确组在前：`马`(70) `妈`(65) `骂`(60)，随后更长编码 `mama`→`妈妈`(100) |
| `lookup("ma",2,0/2,4)` | 分页 `[马,妈]`→`[骂,妈妈]`；offset 回显；`hasMore` 正确；`lookup("ma",0,0)` 只取计数 total=4 |
| `lookup("ceshi",9,0)` | total=1（音节空格归一化后可直接命中） |
| `lookup("",9,0)` / `lookup(129×'a',9,0)` | `kInvalidArgument`（契约 4.2） |
| 损坏样本 ×7 | 记录字节翻转/magic/截断/主版本/次版本/保留字段/尾随字节 → 全部被 A3 以 `kCorruptIndex`/`kUnsupportedVersion` 拒绝且 `detail` 可定位 |

## 7. A2↔A3 交叉验证记录

| 项 | 值 |
|---|---|
| A2 提交 | `f35d867`（索引生产方，本文档登记提交为其后续 docs 提交） |
| A3 提交 | `76a83ee`（loader 实现，交付登记 `f10c879`；HEAD `a037b81` 注册其测试） |
| 索引格式版本 | 1.0（`abc/index_format.h`：`kIndexFormatVersion = 0x00010000`） |
| fixture 路径 | `tests/dict-importer/fixtures/minimal.dict.yaml`（SHA-256 见 §6.1） |
| A2 生成索引 SHA-256 | `26a78bd779bc1d16705764ab2cc89eefbca73f6d1bbe0fb23f4c7b8c3a6c5b6e`（384 字节，CRC `0xC044ED9C`） |
| 验证方式 | `tests/dict-importer/test_cross.cpp`（链接 A3 `loadDictionaryFromFile`，47 用例内含）+ §6.3 查询预期 + 损坏样本矩阵；在本地与目标机均执行 |
| 验证结果 | **全部通过**（本地 47/47；目标机 CTest `abc_dict_import_tests` Passed） |

独立 Python 复核（交付证据，脚本在仓库外 `D:\workbuddy\abc-build-local\verify_golden.py`）：
以 `binascii.crc32`（zlib，独立实现）复核头部 CRC；以手写 FNV-1a 64 复核每条 stableId；
以 `struct` 逐条解析记录帧并比对预期 8 条记录、冻结排序、权重位模式——**全部一致**。

## 8. 契约确认结论（A2）

**结论：确认。** 对照 A2 任务书"接口边界/硬性要求"逐条核对：索引布局可顺序写出
（48 字节头 + 紧凑记录帧）；去重/排序/确定性可满足且已测试；报告与索引内容分离
（可变统计只进报告）；loader 校验义务（index-format.md §5）全部可在生产端自检对齐。
请 A0 将本行确认结论转录至 `docs/interfaces/confirmations.md` 的 A2 行（该文件属
A0 所有权，A2 不直接修改）。

## 9. 已知限制

1. **单文件导入**：v1 每次处理一个输入文件；多文件合并（如 rime-ice 的
   `import_tables` 聚合）不在 v1 范围——聚合文件被明确拒绝，需逐个导入被引用表。
   跨文件去重的 source 平局规则（契约 2.3）因此不可达，已由单文件内"同权重保最早"
   覆盖。
2. **`columns`/`vocabulary`/嵌套 YAML 不支持**：一律致命报错（无静默曲解）。
   rime-ice 的 `cn_dicts/*` 已核实不使用这些键；en_dicts（英文词库）不在 v1 承诺内。
3. **无编码行跳过**（如 rime-ice `others.dict.yaml` 的叠字段）：契约要求 code 非空，
   此类行无法成为记录，按"跳过 + 逐行定位"报告；与 Rime 依赖 Lua 生成编码的行为
   不同，Lua 动态逻辑本就不在兼容承诺内（整体方案 §10）。
4. **内存常驻去重**：词条全部驻留内存（上界 §4）；极端大词库（> 数 GB 索引）超出
   A3 loader 文档化的 3 GiB 载入上限，v1 不支持。
5. **`# no comment` 标记不支持**：以 `#` 开头的词条文本无法表达（Rime 文本码表的
   该特性不在 v1 子集）。
6. **路径含非 ASCII 时本地 Windows 运行需 ASCII 工作目录**：MinGW libstdc++ 的
   `std::filesystem` 窄字符路径转换限制（仅测试工装受影响；导入器 I/O 走 cstdio，
   Linux 目标平台原生 UTF-8 无此问题）。

## 10. 交付记录（2026-10-28）

| 项 | 值 |
|---|---|
| 任务 | A2 Rime 词库导入器（原 DEF-1：任务未启动 → 本轮全新交付） |
| 负责人 | A2 代理（新任，与 A1 同一负责人合并承担；按验收标准 §1 披露自验风险，请集成负责人复核关键结论） |
| 提交号 | `f35d867`（实现 + 测试 + fixtures）+ 本文档登记提交 |
| 接口/索引格式版本 | 接口 1.0.0-rc1；索引格式 1.0（`kIndexFormatVersion`） |
| 变更文件清单 | `source/dict-importer/**`（4 文件）、`tests/dict-importer/**`（8 测试文件 + 8 fixtures）、`docs/modules/rime-import.md`（均在 A2 所有权内；未触碰 A0/A1/A3–A6 文件；A4 并行会话的未提交改动保持原样） |
| 依赖与许可证 | 仅标准库 + `abc_interfaces`（`computeStableId` 为 A0 冻结内联实现）；fixtures 全部自造，无第三方词库/未经授权数据/真实用户输入/凭据 |
| 干净环境构建与测试 | 见 §5.1/§5.2：本地 47 用例 0 失败零告警；目标机 45/45 构建、CTest 14/14、跨平台 SHA-256 一致 |
| fixture 来源及校验值 | §6.1（自造最小样本 + SHA-256）；产物校验值 §6.2 |
| 脱敏日志路径 | `D:\workbuddy\abc-build-local\target-verify-f35d867.log`、`target-verify-cli-f35d867.log`（仓库外；无凭据/token/真实用户输入） |
| 错误处理说明 | §3（退出码 0/1/2/3、逐行定位、绝不覆盖既有文件、自检失败不写出） |
| 禁止事项自查 | 无 XIM/XTest/xdotool（纯离线工具）；无 `/opt`/系统 `/usr` 写入（目标机只读审计通过）；仓库/日志/提交无凭据 |
| 对验收记录的影响 | 关闭 DEF-1；A6 复验轮次的"导入 → 加载 → 查询"真实链路已具备 A2 侧 fixture 与索引产物 |
