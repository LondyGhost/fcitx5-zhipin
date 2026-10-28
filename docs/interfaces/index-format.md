# 引擎索引格式契约（index-format.md）

版本：**索引格式 1.0**（`abc/index_format.h` 常量）。
写入方：A2 导入器。读取方：A3 运行时 loader。A0 维护本契约与常量头文件。
关联接口版本 1.0.0（状态见 [README.md](README.md)）。

设计目标：确定性（同输入同字节输出）、可校验（magic/版本/CRC/记录框架）、
防恶意输入（一切长度先验边界）、无需随机访问外存结构即可前缀二分查找。

## 1. 全局编码约定

- 所有整数 **小端 (little-endian)**，与目标架构无关。
- 字符串一律 **UTF-8**，不带 BOM，不含 NUL 字节。
- 文件由 **头部（固定 48 字节）+ 记录区（紧凑、无对齐填充）** 构成，别无其他内容；
  出现额外尾随字节按损坏处理。
- 换行/缩进/注释等只存在于 Rime 源文件；引擎索引内无任何文本语法。

## 2. 头部（48 字节）

| 偏移 | 长度 | 字段 | 值/说明 |
|---|---|---|---|
| 0x00 | 8 | `magic` | `"ABCDIX1\0"`（`kIndexMagic`，逐字节比较） |
| 0x08 | 4 | `version` | `u32`，`(major << 16) \| minor`；v1.0 = `0x00010000` |
| 0x0C | 8 | `recordCount` | `u64`，记录条数 |
| 0x14 | 8 | `codeCount` | `u64`，不同 `code` 的个数（信息/校验字段） |
| 0x1C | 4 | `crc32` | `u32`，CRC-32/IEEE（反射，poly `0xEDB88320`，初值/终值 `0xFFFFFFFF`），对 **`[0x20, EOF)`** 计算 |
| 0x20 | 4 | `flags` | `u32`，必须为 0（保留） |
| 0x24 | 4 | `reserved0` | `u32`，必须为 0（保留） |
| 0x28 | 8 | `reserved1` | `u64`，必须为 0（保留） |

常量由 `abc/index_format.h` 提供：`kIndexMagic`、`kIndexFormatVersionMajor/Minor`、
`kIndexFormatVersion`、`kIndexMaxCodeBytes=128`、`kIndexMaxTextBytes=512`、
`kIndexMaxSourceBytes=64`、`kIndexMaxRecords=10000000`。

## 3. 记录区

记录紧凑排列，每条结构（字段间无填充）：

| 类型 | 字段 | 说明 |
|---|---|---|
| `u16` | `codeLen` | 1..`kIndexMaxCodeBytes` |
| `u8[]` | `code` | `codeLen` 字节 UTF-8；不含控制字符/NUL |
| `u16` | `textLen` | 1..`kIndexMaxTextBytes` |
| `u8[]` | `text` | `textLen` 字节 UTF-8；合法 UTF-8；不含控制字符/NUL |
| `f64` | `weight` | IEEE-754 双精度小端；必须有限（拒绝 NaN/±Inf） |
| `u8` | `sourceLen` | 1..`kIndexMaxSourceBytes` |
| `u8[]` | `source` | `sourceLen` 字节 UTF-8；不含控制字符/NUL |
| `u64` | `stableId` | 必须等于 `computeStableId(code, text, source)`（[dictionary.md](dictionary.md) 2.2） |

单条记录最小 24 字节；`recordCount × 24` 不得超过记录区长度（见第 5 节校验）。

### 3.1 存储顺序（冻结）

记录区按以下**全序**升序物理存储（也是 [`dictionary.md`](dictionary.md) 4.1 排序的基础）：

1. `code` 升序（UTF-8 字节序 memcmp）；
2. 同 `code` 内 `weight` 降序；
3. 同 `code` 同 `weight` 内 `text` 升序（UTF-8 字节序）。

由去重规则（`dictionary.md` 2.3：索引内 `(code, text)` 唯一）可知该序为全序、无并列。
相同输入重复编译**必须**产出逐字节相同的文件（A2 golden test 义务）。

### 3.2 确定性要求（A2）

- 权重解析自文本时固定为 IEEE-754 双精度的十进制严格转换，不依赖 locale。
- 排序比较使用字节序，不使用本地化排序。
- `source` 等元数据在重复编译间不得因时间戳、路径展开、遍历顺序等因素变化。
- 重复记录数、跳过数等统计可变项只写入导入报告（stdout/报告文件），**不得**进入索引。

## 4. 版本策略（冻结）

- `version` 高 16 位为 major，低 16 位为 minor。
- **major 不同 → 不兼容**：loader 必须 `kUnsupportedVersion` 拒绝，禁止猜测解析。
- **major 相同、文件 minor ≤ 读取方 minor → 兼容**：读取方必须接受。
- **major 相同、文件 minor > 读取方 minor → 拒绝**（`kUnsupportedVersion`）：
  读取方不得对未知布局做宽容解析，避免静默误读。
- minor 升级用于向后兼容的追加（如启用保留字段、附加可选数据）；任何布局破坏性修改（字段增删、顺序调整、编码变更）必须升 major。
- 导入器只能写出当前 `kIndexFormatVersion`；不支持“降级写出”。
- 版本不兼容错误必须可定位：`detail` 应含文件版本与读取方版本（十进制）。

## 5. 加载器校验义务（A3 必须全部执行）

按序执行，任一失败即停止并返回对应 `DictionaryStatus`，不分配无界内存：

1. 文件可读（否则 `kIoError`）；大小 ≥ 48 字节（否则 `kCorruptIndex`）。
2. `magic` 逐字节相等（否则 `kCorruptIndex`）。
3. `version` 兼容性（第 4 节；不兼容 `kUnsupportedVersion`）。
4. `flags/reserved0/reserved1` 全零（否则 `kCorruptIndex`）。
5. `crc32` 与实际计算一致（否则 `kCorruptIndex`）。
6. `recordCount ≤ kIndexMaxRecords`，且 `recordCount × 24 ≤ 文件剩余长度`
   （否则 `kCorruptIndex`）——**先校验再分配**。
7. 逐条解析记录框架：长度字段先与剩余字节比较；字符串内容按第 3 节约束校验
   （UTF-8 合法性、控制字符、长度上限、`weight` 有限、`stableId` 复核）；
   记录区必须恰好消耗到 EOF（否则 `kCorruptIndex`）。
8. 存储顺序校验（3.1 的全序；违反 → `kCorruptIndex`）——保证查询语义。
9. `codeCount` 与实际不同编码数一致（违反 → `kCorruptIndex`；可在顺序校验时顺带完成）。

失败时 `detail` 必须含可定位信息（字段名、记录序号或字节偏移）。

## 6. 生产者义务（A2）

- 只写出本文件定义的布局与当前版本常量；禁止实验字段（扩展走版本流程）。
- 输出前自检：CRC、顺序、stableId、记录数与头部一致（自检失败为导入器缺陷，非零退出）。
- 大文件流式或有界内存（A2 任务书）；本格式支持顺序写出，无需常驻全量内存。
- 不把导入报告、时间戳、主机信息写入索引。

## 7. 诊断与安全

- 损坏/不兼容索引：明确报错（状态码 + 定位 detail），不得崩溃、不得自动重建、
  不得覆盖既有文件。
- 索引内容视为**数据**，永不执行；不与 Rime schema/Lua/Processor 交互。
- 路径一律来自显式参数；导入器与 loader 均不读取环境变量决定输入/输出位置。
