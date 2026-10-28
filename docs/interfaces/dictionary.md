# 词典与候选接口契约（dictionary.md）

对应公共头文件：`source/include/abc/dictionary.h`、常量 `source/include/abc/index_format.h`。
实现方：A3（`source/dictionary/**`，运行时 loader 与 `Dictionary` 实现）。
索引生产方：A2（`source/dict-importer/**`，离线编译器，写入 [`index-format.md`](index-format.md) 定义的格式）。
消费方：A1（核心经注入的抽象查询）、A4（装配）。
本契约冻结于接口版本 1.0.0（状态见 [README.md](README.md)）。

## 1. 总览

```text
A2 导入器（离线 CLI）                     A3 运行时
──────────────────────                ─────────────────────────────
Rime 词库文件（显式路径）
  │ 解析/规范化/去重/排序
  ▼
引擎索引文件（index-format.md v1.0）──► loadDictionaryFromFile(path)
                                          │ 校验 magic/version/crc/记录
                                          ▼
                                      std::unique_ptr<Dictionary>
                                          │
A1 核心会话 ──────────────────────────► Dictionary::lookup(prefix, limit, offset)
                                          ▼
                                      LookupResult{status, CandidateBatch}
```

- 词典运行时接口**只读**；写侧只在离线导入器。
- 核心只认识 `Dictionary` 抽象；A3 的具体类型不进入核心。

## 2. Candidate

```cpp
struct Candidate {
  std::string text;      // 词条，UTF-8，非空，如 "你好"
  std::string code;      // 编码，UTF-8，非空，如 "nihao"（索引内规范化编码）
  double weight = 0.0;   // 词频权重，有限值，越大越常用；仅用于排序，不承诺含义
  std::string source;    // 来源标识（导入器记录的输入来源，如 "cn_dicts/base"），非空
  uint64_t stableId = 0; // 稳定 ID，见 2.2
};
```

### 2.1 字段约束

| 字段 | 约束（索引生产方必须满足，loader 必须校验） |
|---|---|
| `text` | 非空；合法 UTF-8；不含控制字符（< 0x20、0x7F）；≤ `kIndexMaxTextBytes`(512) 字节 |
| `code` | 非空；不含控制字符与 NUL；≤ `kIndexMaxCodeBytes`(128) 字节；字符集由 A2 格式文档定义（通常 `a-z` 与可选分隔符） |
| `weight` | 有限（拒绝 NaN/±Inf）；无符号范围限制 |
| `source` | 非空；≤ `kIndexMaxSourceBytes`(64) 字节；不含控制字符 |

### 2.2 稳定 ID（stableId）

```cpp
// dictionary.h 内联实现，A2/A3/测试共用，禁止各自另写算法
uint64_t computeStableId(std::string_view code, std::string_view text,
                         std::string_view source);
```

- 定义：对 UTF-8 字节序列 `code 0x1F text 0x1F source` 计算 **FNV-1a 64**（offset basis `0xcbf29ce484222325`，prime `0x100000001b3`）。
- 已知向量（冒烟测试断言）：
  - `computeStableId("ni", "你", "base") == 0xdab5c10fa400390e`
  - `computeStableId("nihao", "你好", "cn_dicts/base") == 0xe8f6edb01cdcb459`
- 唯一性：索引内 `(code, text)` 去重后唯一（2.3），故 stableId 在单个索引内唯一。跨索引/跨版本**不承诺**稳定，不得持久化到索引之外。
- 校验：导入器必须写入正确值；loader **应该**复核，不一致按 `kCorruptIndex` 拒绝。

### 2.3 去重规则（A2 必须执行）

- 去重键：`(code, text)`。
- 同键多条时保留权重最高者；权重相同保留 `source` 字节序最小者；再相同保留导入顺序最早者。
- 被去掉的计入导入报告的重复数（报告语义见 A2 任务书）。
- 同一 `text` 挂在不同 `code` 下（多编码词条）**不是**重复，正常保留。

## 3. CandidateBatch 与查询分页

```cpp
struct CandidateBatch {
  std::vector<Candidate> items;  // 本次返回的候选，顺序 = 3.1 冻结排序
  uint32_t offset = 0;           // items[0] 在全量结果中的全局偏移（请求回显）
  uint64_t total = 0;            // 该前缀匹配的精确总数（非页内数）
  bool hasMore = false;          // offset + items.size() < total
};
```

- 分页语义：`offset` 为全量结果中的 0 基偏移；`limit` 为本次最大条数。`offset >= total` ⟹ `items` 空、`total` 照常返回、`hasMore == false`。
- `limit == 0` 合法：只取计数（`items` 空，`total`/`hasMore` 有效）。
- `total` 必须是**精确**总数（不是“至少 N”）；A3 的索引需支持低成本计数。
- `hasMore == (offset + items.size() < total)`，loader 与核心必须一致。

## 4. Dictionary 接口

```cpp
enum class DictionaryStatus : int32_t {
  kOk = 0,
  kInvalidArgument = 1,     // 参数非法（见 4.2）
  kIoError = 2,             // 文件打不开/读取失败（仅加载期）
  kCorruptIndex = 3,        // 校验失败/记录畸形/顺序错误（见 index-format.md）
  kUnsupportedVersion = 4,  // 格式版本不兼容（见 index-format.md 第 4 节）
};

class Dictionary {
 public:
  virtual ~Dictionary() = default;
  Dictionary() = default;
  Dictionary(const Dictionary&) = delete;             // 只读抽象，禁止复制/赋值
  Dictionary& operator=(const Dictionary&) = delete;

  virtual LookupResult lookup(std::string_view codePrefix,
                              uint32_t limit, uint32_t offset) const = 0;
};

struct LookupResult {
  DictionaryStatus status = DictionaryStatus::kOk;
  CandidateBatch batch;   // status != kOk 时 batch 为空且 total == 0
};
```

### 4.1 冻结排序（lookup 结果顺序）

设查询前缀 `P`，记录匹配 ⟺ `record.code` 以 `P` 的字节序列为前缀。结果顺序冻结为：

1. **精确编码优先**：`code == P` 的记录排在任何更长编码之前。
   依据：用户输入完整编码时期望精确候选在前（IME 基本语义）。
2. 其余按 `code` 升序（UTF-8 字节序 memcmp）分组。
3. 组内按 `weight` 降序，再按 `text` 升序（UTF-8 字节序）。
4. 完全相同时按索引存储序（导入顺序），与 [`index-format.md`](index-format.md) 的存储排序一致。

查询结果必须**确定**：同一索引、同一参数，任何线程、任何次序调用，结果与顺序逐字节一致。

### 4.2 参数校验（违反返回 `kInvalidArgument`，batch 为空）

| 参数 | 合法范围 |
|---|---|
| `codePrefix` | 非空；≤ 128 字节；不含控制字符/NUL |
| `limit` | `0..256` |
| `offset` | 任意 uint32 |

### 4.3 线程约束（冻结）

- `lookup` 为 `const`；实现**必须**允许多线程并发查询（索引共享给多个会话时无锁只读或等效保护）。
- loader 构建出的 `Dictionary` 对象在构造完成后不可变；禁止提供任何写接口。
- 核心会话为单线程（[engine.md](engine.md) 第 7 节），共享同一词典对象跨会话线程使用是允许的。

### 4.4 错误语义

- 查询期错误只经 `LookupResult.status` 返回；不得抛异常、不得终止进程、不得返回部分畸形数据。
- 核心收到非 `kOk` 按无候选处理并标注 `EngineError::kDictionaryError`（[engine.md](engine.md) 5.5），不重试、不降级到文件。

## 5. 加载器（A3 提供实现，声明由 A0 冻结）

```cpp
struct DictionaryLoadResult {
  DictionaryStatus status = DictionaryStatus::kOk;
  std::string detail;   // 失败诊断（可定位到字段/偏移）；不得包含词条内容以外的敏感数据
  std::unique_ptr<Dictionary> dictionary;  // status == kOk 时非空
};

DictionaryLoadResult loadDictionaryFromFile(const std::string& path);
```

- 语义：打开 `path`（显式路径，UTF-8），完整校验 [`index-format.md`](index-format.md) 的加载器义务，成功则返回可用词典。
- 失败时：`status` 指明类别，`detail` 给出定位信息（如 `crc mismatch at header`、`record 12345: textLen overflow`）；**不得**返回半可用的对象。
- 加载失败不产生任何文件写入；不得删除或覆盖已存在的有效索引。
- `path` 不存在/无权限 → `kIoError`；同一 `path` 重复加载相互独立（无共享缓存要求）。
- 实现必须防御恶意/截断文件：所有长度先对文件剩余字节校验再分配（[index-format.md](index-format.md) 第 5 节），任何畸形输入有界处理。

## 6. 性能与规模要求（v1.0 基线）

- 加载：典型规模（≤ 200 万条记录、≤ 200 MB 索引）加载进入可服务状态的时间 A3 必须实测并在其交付文档报告（设备、数据量、方法）。
- 查询：单次 `lookup`（页大小 ≤ 9）在真实词库上 A3 报告实测延迟目标；实现不得以扫描全文件为常规路径（索引格式已按编码有序存储，支持二分定位）。
- 内存：加载副本有界；是否 mmap/压缩由 A3 决定，但不得改 v1.0 索引格式（格式演进见 index-format.md 第 4 节）。

## 7. 调用示例

```cpp
// A4：启动时装载，按需传给会话
auto d = abc::loadDictionaryFromFile(userIndexPath);
if (d.status != abc::DictionaryStatus::kOk) {
  // d.detail 供脱敏日志；不覆盖旧索引，不退出进程
  return;
}
auto session = abc::createEngineSession(cfg, d.dictionary.get());

// A1：核心内通过抽象查询（分页取第 2 页）
abc::LookupResult r =
    dict->lookup("nihao", /*limit=*/9, /*offset=*/9);
if (r.status == abc::DictionaryStatus::kOk) {
  // r.batch.total  : 前缀 nihao 的精确总数
  // r.batch.items  : 第 2 页候选，冻结排序
  // r.batch.hasMore: 是否还有后续页
}
```
