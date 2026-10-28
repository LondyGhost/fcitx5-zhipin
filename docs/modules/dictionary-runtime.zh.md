# A3 — 运行时字典与候选检索（dictionary-runtime.md）

所属任务：A3。范围见 `tasks/A3-运行时字典与候选检索.md`。

## 独立验收状态（2026-10-28）

- 本次检查源码基线：`45d5cb70a7a835359bb6ccae55d63112233a2591`。
- A3 实现交付：`76a83eeb2558e12c7af592179ac46808c2d1b78e`；本轮 A3 验收材料及
  构建测试补充修改 `tests/dictionary/CMakeLists.txt`、
  `tests/dictionary/test_dictionary.cpp` 及本文档中英文版本，提交全号随交付报告提供。
- 接口契约：**1.0.0-rc1**；索引格式：**v1.0**。
- 本次核对的 A2 路径：
  - 任务说明：`D:\workbuddy\linux智能ABC\tasks\A2-Rime词库导入器.md`
  - 导入器源码：`D:\workbuddy\linux智能ABC\source\dict-importer\`
  - 测试/fixture：`D:\workbuddy\linux智能ABC\tests\dict-importer\`
  当前基线缺少 A2 源码与测试目录，导入器 CMake 仍为占位文件。可见提交历史中
  没有新 A2 负责人、交付提交、A2 专属接口版本声明或 A2 生成的 fixture/报告。
  **A2→A3 交叉验证：等待 A2 交付。** Fixture 路径、SHA-256、生成命令、报告、
  索引校验值、加载/查询结果及重复编译比较均不可提供。A3 自造测试索引不作为
  交叉验证证据。
- A3 源码：`D:\workbuddy\linux智能ABC\source\dictionary\`；测试及 CMake：
  `D:\workbuddy\linux智能ABC\tests\dictionary\`；索引契约：
  `D:\workbuddy\linux智能ABC\docs\interfaces\index-format.md`；词典契约：
  `D:\workbuddy\linux智能ABC\docs\interfaces\dictionary.md`；英文模块记录：
  `D:\workbuddy\linux智能ABC\docs\modules\dictionary-runtime.md`；中文模块记录：
  `D:\workbuddy\linux智能ABC\docs\modules\dictionary-runtime.zh.md`；A6 所有且本轮
  未改的验收记录：`D:\workbuddy\linux智能ABC\docs\验收记录.md`。
- 原先缺少 `tests/dictionary/CMakeLists.txt`，现由 A3 添加。根 `CMakeLists.txt` 已将
  `dictionary` 列入 `ABC_TEST_MODULES`，目录存在 CMake 文件后会自动注册；测试名为
  `abc_dictionary_tests`，基准目标为 `abc_bench_dictionary`。
- 新增回归测试覆盖 `recordCount > kIndexMaxRecords` 和
  `recordCount * kIndexMinRecordBytes` 大于记录区；已有测试覆盖截断/损坏、主次版本
  不兼容、offset 超出结果及分页边界。loader 先用 u64 读取磁盘计数，先检查 10,000,000
  上限再乘 24；通过上限后乘法不会溢出 u64。预算检查先于转成 `size_t` 和 reserve，
  reserve 还受 `record_bytes / 24` 限制；文件缓冲分配另受 3 GiB 上限约束。
- 干净构建命令（仓库根目录）：
  `cmake -S . -B build-a3-clean -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`、
  `cmake --build build-a3-clean`、`ctest --test-dir build-a3-clean --output-on-failure`。
  本 Windows 会话未安装 CMake/Ninja，故 configure/build/CTest 未运行。另以 MinGW 执行
  直接编译：`g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include
  -Isource/dictionary -Itests/dictionary source/dictionary/index_loader.cpp
  source/dictionary/dictionary_index.cpp tests/dictionary/test_dictionary.cpp
  -o %TEMP%\abc_test_dictionary_a3.exe -pthread`，再运行该 exe；结果 exit 0，**41 项
  通过、0 失败、1 项跳过**（缺 A2 fixture）。直接编译不能证明 CMake/CTest 注册，干净
  CMake/CTest 完整结果仍待补；目标 Linux 上 A3 测试也待执行。
- 目标 Linux 环境资料见 `docs/目标机环境.md`：CatOS、x86_64、内核 7.2.11-arch1-1、
  4 核、GCC 16.2.1、CMake 4.4.4、Ninja 1.13.2。尚未在该设备运行 A3 基准。下文已有
  Windows 20 万条结果仅为开发测量，不能作为目标 Linux 验收证据。**目标 Linux 契约
  规模（最多 200 万条/200 MB）性能：待完成。**责任人：A3（DNP Dev）；计划是在目标机
  构建 `abc_bench_dictionary`，以 200 万条代表数据运行并归档完整 stdout、设备信息、
  索引大小、加载耗时和查询延迟。
- 脱敏 A3 构建/测试/基准日志：**待生成并交 A6 归档**。本次不写 A6 所有的 `logs/`、
  `tests/integration/`、`tests/manual/`、`artifacts/manifest.md` 或 `docs/验收记录.md`。
- 已知验收限制：新 A2 负责人/提交及交叉产物缺失；干净 CMake/CTest 与目标 Linux 性能
  未验证。现有模块自测和 Windows 合成基准不能关闭这些事项。

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

开发环境测量：Windows 主机，MinGW g++ 16.1.0（`-O2`），单线程，本地 SSD。
此结果不是目标 Linux 验收证据。

代表性数据集：20 万条、8.8 MB 索引、编码互不相同、`c00000000` 风格合成编码。

| 测量项 | 结果 |
|---|---|
| A3 合成基准夹具构建 | 500.139 ms |
| 写出索引 | 10.3174 ms |
| 加载进入可服务状态（`loadDictionaryFromFile`） | 74.0305 ms |
| 10 万次前缀查询，页大小 9 | 总计 215.223 ms；2.15223 us / 次 |

命令：`g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include
-Isource/dictionary -Itests/dictionary source/dictionary/index_loader.cpp
source/dictionary/dictionary_index.cpp tests/dictionary/bench_dictionary.cpp
-o %TEMP%\abc_bench_dictionary_a3.exe -pthread`，再运行
`%TEMP%\abc_bench_dictionary_a3.exe 200000`。
完整输出：`bench: records=200000 index_bytes=8800048 build_ms=500.139
write_ms=10.3174 load_ms=74.0305 lookup_ms_per_100k=215.223
us_per_lookup=2.15223 prefix_total=10 first_page_size=9 served=900000`。
方法：`tests/dictionary/bench_dictionary.cpp`，计时使用 `std::chrono::steady_clock`。

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
