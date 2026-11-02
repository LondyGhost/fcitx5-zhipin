// A3 private internals: read-only index model and runtime lookup.
// Public headers live in source/include/abc/ and are owned by A0 (not modified here).
//
// Threading (contract: docs/interfaces/dictionary.md section 4.3):
//   - RuntimeDictionary is immutable after construction: no lazy caches,
//     no global counters, no static mutable state;
//   - every public member is const and only reads the owned vectors;
//   - therefore any number of threads may call lookup() concurrently on the same
//     object, and one object may be shared by multiple engine sessions (each
//     session itself stays single-threaded, engine.md section 7).
//
// Lookup core (contract: docs/interfaces/dictionary.md section 4.1):
//   Storage order is (code ascending, then weight descending, then text ascending).
//   For a fixed prefix P the matching records split into disjoint groups:
//     exact = code == P                    (a contiguous run of the record array)
//     syl   = code == 首音节(P) 且 text 为单字（智能ABC 的“词后回落单字”）
//     sub   = P is a proper prefix          (a second contiguous run)
//   Emission order is exact -> syl -> sub. The first two groups are already
//   ordered runs of one sorted sequence; syl is a filtered view of its run, so
//   its relative order (weight descending, then text ascending) is preserved.
//   `sub` (P 开头的更长编码，即“智能大楼”这类联想词) 仅在 exact 为空时作为
//   部分拼音输入的兜底出现，避免把联想长词排在单字之前。
//   No per-query sort is needed: locate the runs, then emit the requested
//   [offset, offset+limit) slice. A page materialises at most `limit` records.

#ifndef ABC_DICTIONARY_INTERNAL_INDEX_HPP_
#define ABC_DICTIONARY_INTERNAL_INDEX_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "abc/dictionary.h"

namespace abc {
namespace dictionary_internal {

// Byte offsets inside one stored record (index-format.md section 3).
constexpr std::size_t kCodeOffset = 2;

// Position of one record inside the immutable byte blob. code_len is cached so
// a lookup can compare a code without parsing the whole frame.
struct RecordRef {
  std::uint32_t offset = 0;  // byte offset of codeLen within the blob
  std::uint32_t length = 0;  // total record length in bytes
  std::uint16_t code_len = 0;
};

// Read-only dictionary built by the loader (index_loader.cpp). The type is internal
// to this module; callers obtain it only through abc::loadDictionaryFromFile.
class RuntimeDictionary final : public Dictionary {
 public:
  RuntimeDictionary(std::vector<unsigned char> bytes,
                    std::vector<RecordRef> refs, std::size_t code_count);
  ~RuntimeDictionary() override = default;

  // See dictionary.md 4.1/4.2/4.3. Never throws, never allocates more than a page.
  LookupResult lookup(std::string_view codePrefix, uint32_t limit,
                      uint32_t offset) const override;

  std::size_t recordCount() const { return refs_.size(); }
  std::size_t codeCount() const { return code_count_; }
  const std::vector<unsigned char>& storage() const { return bytes_; }

 private:
  std::string_view codeAt_(std::size_t i) const;
  void decodeAt_(std::size_t i, Candidate* out) const;
  std::size_t lowerBound_(std::string_view target) const;
  std::size_t upperBound_(std::string_view target) const;
  void emit_(std::size_t lo, std::size_t hi, std::uint64_t* skip,
             std::uint64_t take, CandidateBatch* batch) const;

  // ---- 智能ABC 首音节单字回落 ----
  //
  // syllables_ 是“单字音节码”集合：由索引中所有「码为纯 a-z、长度 1..6、
  // 且该码下至少有一条单字词条」的码组成，构造期一次扫描得到，之后只读。
  // 用 DP 把缓冲切成最少的音节数（并列时取更长首音节），从而把
  // “zhineng” 切成 zhi|neng 而不是 zhi|nen|g，把 “xian” 保持为单音节。
  void buildSyllables_();
  bool isSingleCharText_(std::size_t i) const;
  bool isSyllable_(std::string_view s) const;
  bool firstSyllable_(std::string_view code, std::string_view* out) const;
  std::uint64_t countSingleCharRun_(std::size_t lo, std::size_t hi) const;
  void emitSingleCharRun_(std::size_t lo, std::size_t hi, std::uint64_t* skip,
                          std::uint64_t take, CandidateBatch* batch) const;

  std::vector<unsigned char> bytes_;  // immutable after construction
  std::vector<RecordRef> refs_;       // storage order (index-format.md 3.1)
  std::size_t code_count_ = 0;
  std::vector<std::string> syllables_;  // 升序去重的单字音节码
};

}  // namespace dictionary_internal
}  // namespace abc

#endif  // ABC_DICTIONARY_INTERNAL_INDEX_HPP_
