// abc/dictionary.h — 只读词典与候选类型（A0 维护）
// 契约：docs/interfaces/dictionary.md（字段约束、排序、去重、stableId、
// 分页语义、线程约束、错误语义）。索引格式常量见 abc/index_format.h。
// 实现方：A3（loadDictionaryFromFile 与 Dictionary 具体类）。
#ifndef ABC_DICTIONARY_H_
#define ABC_DICTIONARY_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace abc {

// 一条候选词条。字段约束见契约 2.1；索引内 (code, text) 唯一。
struct Candidate {
  std::string text;      // 词条，UTF-8，非空
  std::string code;      // 编码，UTF-8，非空
  double weight = 0.0;   // 词频权重，有限值，越大越常用
  std::string source;    // 来源标识，UTF-8，非空
  uint64_t stableId = 0; // 稳定 ID，见 computeStableId
};

// 稳定 ID：对 "code 0x1F text 0x1F source" 的 UTF-8 字节序列做 FNV-1a 64。
// A2 导入器与 A3 loader 共用此实现，禁止另写算法；契约 2.2 附已知测试向量。
inline uint64_t computeStableId(std::string_view code, std::string_view text,
                                std::string_view source) {
  constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
  uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a 64 offset basis
  const std::string_view parts[3] = {code, text, source};
  for (std::size_t i = 0; i < 3; ++i) {
    for (char c : parts[i]) {
      h ^= static_cast<unsigned char>(c);
      h *= kFnvPrime;
    }
    if (i < 2) {  // 分隔符 0x1F
      h ^= static_cast<unsigned char>(0x1F);
      h *= kFnvPrime;
    }
  }
  return h;
}

// 前缀查询结果页。分页语义见契约第 3 节：offset 为全量结果中的 0 基偏移，
// total 为该前缀匹配的精确总数，hasMore == (offset + items.size() < total)。
struct CandidateBatch {
  std::vector<Candidate> items;  // 本页候选，顺序 = 契约 4.1 冻结排序
  uint32_t offset = 0;           // items[0] 的全局偏移（请求回显）
  uint64_t total = 0;            // 精确匹配总数
  bool hasMore = false;
};

// 词典错误码。查询与加载共用；类别与产生位置见契约 4.4 与 5 节。
enum class DictionaryStatus : int32_t {
  kOk = 0,
  kInvalidArgument = 1,     // 参数非法（契约 4.2）
  kIoError = 2,             // 文件打不开/读取失败（仅加载期）
  kCorruptIndex = 3,        // 校验失败/记录畸形/顺序错误（index-format.md 5）
  kUnsupportedVersion = 4,  // 格式版本不兼容（index-format.md 4）
};

// lookup 的返回类型。status != kOk 时 items 为空且 total == 0。
struct LookupResult {
  DictionaryStatus status = DictionaryStatus::kOk;
  CandidateBatch batch;
};

// 只读词典抽象。lookup 必须线程安全（契约 4.3）；结果确定性。
class Dictionary {
 public:
  Dictionary() = default;
  virtual ~Dictionary() = default;
  Dictionary(const Dictionary&) = delete;
  Dictionary& operator=(const Dictionary&) = delete;

  // 前缀查询：codePrefix 非空且 ≤ 128 字节；limit 0..256（0 = 只取计数）；
  // offset 任意。违反返回 kInvalidArgument 且 batch 为空。
  virtual LookupResult lookup(std::string_view codePrefix, uint32_t limit,
                              uint32_t offset) const = 0;
};

// 索引加载结果。失败时 dictionary 为空，detail 含可定位诊断（契约第 5 节）。
struct DictionaryLoadResult {
  DictionaryStatus status = DictionaryStatus::kOk;
  std::string detail;
  std::unique_ptr<Dictionary> dictionary;  // status == kOk 时非空
};

// 加载引擎索引文件（index-format.md v1.0）。由 A3 提供实现；声明由 A0 冻结。
// 加载失败不覆盖/删除既有文件，不终止进程。
DictionaryLoadResult loadDictionaryFromFile(const std::string& path);

}  // namespace abc

#endif  // ABC_DICTIONARY_H_
