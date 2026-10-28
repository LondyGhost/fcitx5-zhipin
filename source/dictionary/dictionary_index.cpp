// A3 runtime dictionary: abc::Dictionary implementation over the loaded index.
//
// Contract: docs/interfaces/dictionary.md sections 2..4.
//   - lookup is const and thread-safe (4.3): it only reads owned immutable vectors;
//   - parameter validation (4.2);
//   - frozen ordering (4.1): exact code first, then code ascending,
//     weight descending, text ascending;
//   - paging (3): offset/limit/total/hasMore; limit == 0 returns counts only.
//
// Threading: the object is immutable after construction. No cache, no counter
// and no static mutable state is written by lookup(), so any number of threads
// may query one instance concurrently.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc/dictionary.h"
#include "abc/index_format.h"
#include "internal/bytes.hpp"
#include "internal/index.hpp"

namespace abc {
namespace dictionary_internal {
namespace {

// Frozen validation ranges (dictionary.md 4.2).
constexpr std::size_t kMaxPrefixBytes = kIndexMaxCodeBytes;
constexpr std::uint32_t kMaxLimit = 256;

// True if s contains NUL, another ASCII control character, or DEL.
bool hasForbiddenByte(std::string_view s) {
  for (const char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20u || u == 0x7Fu) return true;
  }
  return false;
}

// Writes the byte-lexicographic successor of the (non-empty) prefix into buf
// and returns its length, or 0 when no successor exists (prefix is all 0xFF).
// The successor is the smallest byte string strictly greater than every string
// that has `prefix` as a byte prefix, so lowerBound_(successor) is exactly the
// first record whose code does not start with `prefix`. All codes are UTF-8
// without NUL or control bytes, so every byte is in [0x20, 0x7E] or a valid
// UTF-8 continuation; carrying from the last byte forward still works because
// no byte equals 0xFF.
std::size_t prefixSuccessor(std::string_view prefix, unsigned char* buf,
                            std::size_t cap) {
  const std::size_t n = prefix.size();
  if (n == 0 || n > cap) return 0;
  std::size_t i = n;
  while (i > 0) {
    --i;
    const unsigned char u = static_cast<unsigned char>(prefix[i]);
    if (u < 0xFFu) {
      buf[i] = static_cast<unsigned char>(u + 1u);
      for (std::size_t j = 0; j < i; ++j) {
        buf[j] = static_cast<unsigned char>(prefix[j]);
      }
      return i + 1;
    }
  }
  return 0;
}

}  // namespace

RuntimeDictionary::RuntimeDictionary(std::vector<unsigned char> bytes,
                                     std::vector<RecordRef> refs,
                                     std::size_t code_count)
    : bytes_(std::move(bytes)),
      refs_(std::move(refs)),
      code_count_(code_count) {}

std::string_view RuntimeDictionary::codeAt_(std::size_t i) const {
  const RecordRef& ref = refs_[i];
  return std::string_view(
      reinterpret_cast<const char*>(bytes_.data()) + ref.offset + kCodeOffset,
      ref.code_len);
}

void RuntimeDictionary::decodeAt_(std::size_t i, Candidate* out) const {
  const RecordRef& ref = refs_[i];
  const auto* base = bytes_.data() + ref.offset;
  const std::size_t code_len = ref.code_len;
  std::size_t p = ref.offset + kCodeOffset;
  out->code.assign(reinterpret_cast<const char*>(base) + kCodeOffset, code_len);
  p += code_len;
  const std::uint16_t text_len = readU16(base + kCodeOffset + code_len);
  p += 2;
  out->text.assign(reinterpret_cast<const char*>(bytes_.data() + p), text_len);
  p += text_len;
  out->weight = readF64(bytes_.data() + p);
  p += 8;
  const std::uint8_t source_len = bytes_[p];
  ++p;
  out->source.assign(reinterpret_cast<const char*>(bytes_.data() + p),
                     source_len);
  p += source_len;
  out->stableId = readU64(bytes_.data() + p);
}

std::size_t RuntimeDictionary::lowerBound_(std::string_view target) const {
  std::size_t lo = 0;
  std::size_t hi = refs_.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (compareBytes(codeAt_(mid), target) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

std::size_t RuntimeDictionary::upperBound_(std::string_view target) const {
  std::size_t lo = 0;
  std::size_t hi = refs_.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (compareBytes(codeAt_(mid), target) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

// Append records [lo, hi) to the batch, skipping the first *skip of them, until
// `take` records are present. *skip is reduced in place. The batch is assumed to
// hold fewer than `take` items on entry.
void RuntimeDictionary::emit_(std::size_t lo, std::size_t hi,
                              std::uint64_t* skip, std::uint64_t take,
                              CandidateBatch* batch) const {
  const std::uint64_t available = static_cast<std::uint64_t>(hi - lo);
  if (*skip >= available) {
    *skip -= available;
    return;
  }
  const std::size_t begin = lo + static_cast<std::size_t>(*skip);
  *skip = 0;
  std::uint64_t remaining =
      take - static_cast<std::uint64_t>(batch->items.size());
  std::size_t i = begin;
  while (i < hi && remaining != 0) {
    Candidate c;
    decodeAt_(i, &c);
    batch->items.push_back(std::move(c));
    ++i;
    --remaining;
  }
}

LookupResult RuntimeDictionary::lookup(std::string_view codePrefix,
                                       uint32_t limit, uint32_t offset) const {
  LookupResult result;
  if (codePrefix.empty() || codePrefix.size() > kMaxPrefixBytes ||
      limit > kMaxLimit || hasForbiddenByte(codePrefix)) {
    result.status = DictionaryStatus::kInvalidArgument;
    return result;
  }

  // Records whose code equals the prefix form [exact_lo, exact_hi).
  const std::size_t exact_lo = lowerBound_(codePrefix);
  const std::size_t exact_hi = upperBound_(codePrefix);

  // Records whose code has the prefix as a proper prefix are the run
  // [exact_hi, sub_hi) where sub_hi is the first code that does not start with
  // the prefix. Because the array is byte-ordered, sub_hi is lowerBound_ of the
  // prefix's byte successor (see prefixSuccessor).
  std::size_t sub_hi = refs_.size();
  unsigned char succ[kMaxPrefixBytes];
  const std::size_t succ_len = prefixSuccessor(codePrefix, succ, kMaxPrefixBytes);
  if (succ_len != 0) {
    sub_hi = lowerBound_(std::string_view(
        reinterpret_cast<const char*>(succ), succ_len));
  }

  const std::uint64_t exact_total =
      static_cast<std::uint64_t>(exact_hi - exact_lo);
  const std::uint64_t sub_total = static_cast<std::uint64_t>(sub_hi - exact_hi);
  const std::uint64_t total = exact_total + sub_total;

  result.batch.offset = offset;
  if (limit == 0) {
    result.batch.total = total;
    result.batch.hasMore =
        static_cast<std::uint64_t>(offset) < total;
    return result;
  }

  std::uint64_t skip = static_cast<std::uint64_t>(offset);
  const std::size_t exact_count = exact_hi - exact_lo;
  result.batch.items.reserve(exact_count < limit ? exact_count : limit);
  emit_(exact_lo, exact_hi, &skip, limit, &result.batch);
  if (result.batch.items.size() < limit) {
    emit_(exact_hi, sub_hi, &skip, limit, &result.batch);
  }

  result.batch.total = total;
  result.batch.hasMore =
      static_cast<std::uint64_t>(offset) +
          static_cast<std::uint64_t>(result.batch.items.size()) <
      total;
  return result;
}

}  // namespace dictionary_internal
}  // namespace abc