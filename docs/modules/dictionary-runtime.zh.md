# A3 — 运行时字典与候选检索（dictionary-runtime.md）

所属任务：A3。范围见 `tasks/A3-运行时字典与候选检索.md`。

本模块提供只读运行时索引加载器，以及冻结公共契约
（`docs/interfaces/dictionary.md`、`docs/interfaces/index-format.md`）背后的
`abc::Dictionary` 实现。

## 1. 模块概览

- **`abc::loadDictionaryFromFile(path)`** — 打开 v1.0 引擎索引，执行
  `index-format.md` 第 5 节的完整加载校验序列，成功时返回持有只读
  `Dictionary` 的 `DictionaryLoadResult`。
- **`abc::Dictionary::lookup(prefix, limit, offset)`** — 确定性的前缀查询，
  冻结排序、稳定 ID 与分页语义。

加载器不写入文件、不覆盖已有索引、不解析 Rime schema/Lua；索引仅视为数据。

## 2. 接口版本与索引格式

- 接口契约版本：**1.0.0-rc1**（`docs/interfaces/README.md`）。
- 交付提交号：`76a83eeb2558e12c7af592179ac46808c2d1b78e`。
- 索引格式：**v1.0**（`abc::kIndexFormatVersion == 0x00010000`，
  魔数 `"ABCDIX1\0"`）。

未修改 `source/include/abc/` 下任何公共头；索引布局未改；`abc::computeStableId`
共享 A0 头实现，未重写。

## 3. A3 拥有的文件

```
source/dictionary/CMakeLists.txt        构建目标 abc_dictionary（静态库）
source/dictionary/index_loader.cpp      加载器：读取 + 校验 + 构建
source/dictionary/dictionary_index.cpp  RuntimeDictionary::lookup 与辅助函数
source/dictionary/internal/bytes.hpp    字节 / UTF-8 / CRC-32 工具
source/dictionary/internal/index.hpp    私有模型与方法声明
tests/dictionary/index_builder.hpp      独立 v1.0 夹具构建器 + CRC-32
tests/dictionary/test_dictionary.cpp    功能测试主程序
tests/dictionary/bench_dictionary.cpp   性能基准主程序
docs/modules/dictionary-runtime.md      本文档
```

未改动 A3 所有权边界之外的任何文件。

## 4. 加载器行为与错误语义

加载器按 `index-format.md` 第 5 节的固定顺序校验，任一步失败即停止并返回带
定位信息的 `detail`：

1. 文件可读（`kIoError`）；大小 >= 48 字节（`kCorruptIndex`）。
2. 魔数逐字节相等（`kCorruptIndex`）。
3. 版本兼容（`kUnsupportedVersion`；major 不同或文件 minor 高于读取方即拒绝）。
4. flags / reserved0 / reserved1 全零（`kCorruptIndex`）。
5. CRC-32/IEEE 覆盖 `[0x20, EOF)`（`kCorruptIndex`）。
6. recordCount <= `kIndexMaxRecords` 且 recordCount * 24 <= 剩余字节，
   **在任何分配之前**校验（`kCorruptIndex`）。
7. 每条记录框架：长度字段先与剩余字节比较；code 无控制字符且限长；
   text/source 为合法 UTF-8、无控制字符、非空、限长；weight 有限；
   stableId 用 `computeStableId` 复核（`kCorruptIndex`）。记录区必须恰好消耗
   到 EOF（有尾随字节则 `kCorruptIndex`）。
8. 存储顺序（code 升序、weight 降序、text 升序）与 `(code, text)` 唯一
   （`kCorruptIndex`）。
9. codeCount 等于实际不同编码数（`kCorruptIndex`）。

**资源上界**：缓冲区大小来自 `fstat`，绝不来自头部字段；单次加载上限
`kMaxLoadableBytes = 3 GiB`；记录表按文件大小推导的预算预留。`std::bad_alloc`
被捕获并映射为 `kIoError` 加诊断。不写入任何文件。

## 5. 查询语义

`lookup` 为 `const` 且线程安全：对象构造后不可变，无缓存、计数或静态可变状态
被写入，因此任意数量的线程可并发查询同一共享实例（会话本身按 `engine.md`
第 7 节保持单线程）。

- 参数校验（`dictionary.md` 4.2）：前缀非空、<= 128 字节、无控制字节；
  limit 0..256；offset 任意 uint32。违反返回 `kInvalidArgument` 且批为空。
- 排序（`dictionary.md` 4.1）：精确编码组在前（`code == prefix`），其余按
  code 升序，组内 weight 降序再 text 升序。索引按该字节序存储，两组均为连续
  区间，翻页通过两次二分定位完成，**无逐查询排序**，且最多物化 `limit` 条
  候选。
- 分页（`dictionary.md` 3）：`offset` 为全量结果中的 0 基偏移；`total` 为精确
  匹配总数；`hasMore == (offset + items.size() < total)`；`limit==0` 只取计数；
  `offset >= total` 返回空批但保留 `total`。
- 确定性：同一索引 + 同一参数，在任何线程与调用顺序下结果逐字节一致。

## 6. 性能测量

环境：Windows 10 主机，MinGW g++ 16.1.0（`-O2`），单线程，本地 SSD。

代表性数据集：20 万条、8.8 MB 索引、编码互不相同、`c00000000` 风格合成编码。

| 测量项 | 结果 |
|---|---|
| 夹具构建 | 378 ms |
| 加载进入可服务状态（`loadDictionaryFromFile`） | 57 ms |
| 10 万次前缀查询，页大小 9 | 3.45 us / 次 |

方法：`tests/dictionary/bench_dictionary.cpp`（构建与运行命令见下），计时用
`std::chrono::steady_clock`。

契约基线为 <= 200 万条 / <= 200 MB。查询为每页 O(log n) 加 O(limit) 物化；
加载为 O(n)。

## 7. 构建与测试命令

本环境未安装 cmake/ninja，采用独立编译（CMake 目标仍随根构建提供）：

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isource/include -Isource/dictionary -Itests/dictionary \
  source/dictionary/index_loader.cpp source/dictionary/dictionary_index.cpp \
  tests/dictionary/test_dictionary.cpp -o test_dictionary.exe -pthread
./test_dictionary.exe
```

结果：`abc_dictionary_test: ALL PASS`（37 项检查）。覆盖精确/前缀查询、无结果、
冻结排序、分页（offset/limit/total/hasMore、limit 0、offset 越过总数）、非法
参数、边界长度（code 128、text 512、source 64、limit 0/256）、并发（8 线程 x
2000 次查询）、损坏（magic/CRC/flags/reserved/尾随/乱序/stableId/codeCount/
非有限 weight/空 source/非法 UTF-8）、截断（不足头部、记录中间、空索引），
以及版本不兼容（major 不符、minor 超前、兼容 1.0）。当
`tests/dictionary/a2_fixture.idx` 缺失时，A2 夹具交叉验证标记为跳过。

基准：

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isource/include -Isource/dictionary -Itests/dictionary \
  source/dictionary/index_loader.cpp source/dictionary/dictionary_index.cpp \
  tests/dictionary/bench_dictionary.cpp -o bench_dictionary.exe -pthread
./bench_dictionary.exe 200000
```

## 8. 线程约束

- `lookup` 允许任意数量线程并发调用同一实例（对象构造后不可变，无需加锁）。
- 加载不要求在相同路径上并发安全（每次 `loadDictionaryFromFile` 独立，契约
  不要求共享缓存）。
- 引擎会话为单线程；跨会话共享已加载词典由上述保证允许。

## 9. 已知限制

- **A2 交叉验证待办**。本仓库尚无导入器夹具 `tests/dictionary/a2_fixture.idx`，
  故 A2 产物索引的交叉验证当前报告为跳过；A2 交付后会自动运行。加载器逻辑
  已由独立构建的 v1.0 索引（含独立 CRC-32 实现）充分验证。
- stableId 复核已执行（`dictionary.md` 2.2 建议）；写入错误 stableId 的索引
  被拒绝。
- 加载器整文件读入内存（本构建未 mmap）；上界已文档化（`kMaxLoadableBytes`）。
  超大文件可流式读取但未实现。
- 非相邻且权重不同的重复 `(code, text)` 属于生产者义务（去重规则，
  `dictionary.md` 2.3）；加载器拒绝存储顺序违规与相邻重复，但无法检测
  非相邻且仍处于合法存储顺序的重复。合规导入器不会产出此类文件。

## 10. 自查声明

- 无 XIM/XTest/xdotool；不写 `/opt` 或系统 `/usr`；仓库内无部署凭据。
- 未修改公共头与索引布局；`computeStableId` 共享而非重写。