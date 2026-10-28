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
//   For a fixed prefix P the matching records split into two disjoint groups:
//     exact = code == P            (a first contiguous run of the record array)
//     sub   = P is a proper prefix  (a second contiguous run)
//   Emitting exact first and then sub preserves the frozen order
//       exact code first -> remaining codes ascending -> weight descending -> text ascending
//   because both groups are already contiguous runs of one sorted sequence.
//   So no per-query sort is needed: locate the two runs, then emit the requested
//   [offset, offset+limit) slice. A page materialises at most `limit` records.

#ifndef ABC_DICTIONARY_INTERNAL_INDEX_HPP_
#define ABC_DICTIONARY_INTERNAL_INDEX_HPP_

#include <cstddef>
#include <cstdint>
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

  std::vector<unsigned char> bytes_;  // immutable after construction
  std::vector<RecordRef> refs_;       // storage order (index-format.md 3.1)
  std::size_t code_count_ = 0;
};

}  // namespace dictionary_internal
}  // namespace abc

#endif  // ABC_DICTIONARY_INTERNAL_INDEX_HPP_
