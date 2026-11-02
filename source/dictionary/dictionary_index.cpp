// A3 runtime dictionary: abc::Dictionary implementation over the loaded index.
//
// Contract: docs/interfaces/dictionary.md sections 2..4.
//   - lookup is const and thread-safe (4.3): it only reads owned immutable vectors;
//   - parameter validation (4.2);
//   - frozen ordering (4.1): exact code first, then 首音节单字 (智能ABC 词后
//     回落单字), then code ascending / weight descending / text ascending;
//     更长编码的联想词仅在无精确词时兜底；
//   - paging (3): offset/limit/total/hasMore; limit == 0 returns counts only.
//
// Threading: the object is immutable after construction. No cache, no counter
// and no static mutable state is written by lookup(), so any number of threads
// may query one instance concurrently.

#include <algorithm>
#include <array>
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

// 音节码最大字节数（"zhuang"/"chuang" 为 6）。
constexpr std::size_t kMaxSyllableBytes = 6;
// DP 切分支持的最大缓冲长度；超出则不启用单字回落（安全兜底）。
constexpr std::size_t kMaxSegmentBytes = 64;

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
      code_count_(code_count) {
  buildSyllables_();
}

// 单字词条判定：text 恰好一个 UTF-8 码位。
bool RuntimeDictionary::isSingleCharText_(std::size_t i) const {
  const RecordRef& ref = refs_[i];
  const std::size_t len_off = ref.offset + kCodeOffset + ref.code_len;
  if (len_off + 2 > bytes_.size()) return false;
  const std::uint16_t len = readU16(bytes_.data() + len_off);
  if (len == 0) return false;
  const unsigned char* p = bytes_.data() + len_off + 2;
  if (len == 1) return p[0] < 0x80u;                    // ASCII
  if (len == 2) return (p[0] & 0xE0u) == 0xC0u;         // 2 字节
  if (len == 3) return (p[0] & 0xF0u) == 0xE0u;         // 3 字节（汉字）
  if (len == 4) return (p[0] & 0xF8u) == 0xF0u;         // 4 字节（扩展区）
  return false;
}

// 构造期一次扫描：收集所有「纯 a-z、长度 1..6、该码下含单字词条」的码。
// refs_ 按 code 升序，故按码分组顺序推进即可，结果天然升序去重。
void RuntimeDictionary::buildSyllables_() {
  syllables_.clear();
  syllables_.reserve(512);
  std::size_t i = 0;
  const std::size_t n = refs_.size();
  while (i < n) {
    const std::string_view code = codeAt_(i);
    bool has_single = false;
    std::size_t j = i;
    while (j < n && codeAt_(j) == code) {
      if (!has_single && isSingleCharText_(j)) has_single = true;
      ++j;
    }
    if (has_single && !code.empty() && code.size() <= kMaxSyllableBytes) {
      bool pure_lower = true;
      for (const char c : code) {
        if (c < 'a' || c > 'z') {
          pure_lower = false;
          break;
        }
      }
      // 单字母码只接受合法单韵母音节；否则 "b"/"r" 这类字母码会被误判成
      // 音节，让 "brshuo" 这种简拼额外冒出一条单字。
      const bool one_letter_ok =
          code.size() >= 2 || code == "a" || code == "o" || code == "e";
      if (pure_lower && one_letter_ok) syllables_.emplace_back(code);
    }
    i = j;
  }
}

bool RuntimeDictionary::isSyllable_(std::string_view s) const {
  const auto it = std::lower_bound(
      syllables_.begin(), syllables_.end(), s,
      [](const std::string& a, std::string_view b) {
        return std::string_view(a) < b;
      });
  return it != syllables_.end() && std::string_view(*it) == s;
}

// 把 code 切成音节数最少的方案，返回首音节；并列时取更长首音节
// （"xian" 保持整音节，而不是拆成 xi|an）。切不开返回 false。
bool RuntimeDictionary::firstSyllable_(std::string_view code,
                                       std::string_view* out) const {
  constexpr std::size_t kInf = static_cast<std::size_t>(-1);
  const std::size_t n = code.size();
  if (n < 2 || n > kMaxSegmentBytes) return false;

  std::array<std::size_t, kMaxSegmentBytes + 1> best{};
  std::array<std::size_t, kMaxSegmentBytes + 1> pick{};
  best.fill(kInf);
  pick.fill(0);
  best[n] = 0;
  for (std::size_t i = n; i-- > 0;) {
    for (std::size_t len = 1; len <= kMaxSyllableBytes && i + len <= n; ++len) {
      if (!isSyllable_(code.substr(i, len))) continue;
      if (best[i + len] == kInf) continue;
      const std::size_t cand = best[i + len] + 1;
      if (cand < best[i] || (cand == best[i] && len > pick[i])) {
        best[i] = cand;
        pick[i] = len;
      }
    }
  }
  if (best[0] == kInf || pick[0] == 0) return false;
  *out = code.substr(0, pick[0]);
  return true;
}

std::uint64_t RuntimeDictionary::countSingleCharRun_(std::size_t lo,
                                                     std::size_t hi) const {
  std::uint64_t n = 0;
  for (std::size_t i = lo; i < hi; ++i) {
    if (isSingleCharText_(i)) ++n;
  }
  return n;
}

void RuntimeDictionary::emitSingleCharRun_(std::size_t lo, std::size_t hi,
                                           std::uint64_t* skip,
                                           std::uint64_t take,
                                           CandidateBatch* batch) const {
  for (std::size_t i = lo; i < hi; ++i) {
    if (batch->items.size() >= take) return;
    if (!isSingleCharText_(i)) continue;
    if (*skip > 0) {
      --*skip;
      continue;
    }
    Candidate c;
    decodeAt_(i, &c);
    batch->items.push_back(std::move(c));
  }
}

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

  // 智能ABC 语义：词语候选用尽后回落「首音节单字」，而不是罗列 P 开头的
  // 更长联想词（智能大楼 / 智能机器人 …）。首音节只在 P 可切成 ≥2 个音节
  // 时启用；单音节输入（如 xian）不会额外插入单字段。
  std::size_t syl_lo = 0;
  std::size_t syl_hi = 0;
  std::uint64_t syl_total = 0;
  std::string_view first_syl;
  if (firstSyllable_(codePrefix, &first_syl) &&
      first_syl.size() < codePrefix.size()) {
    syl_lo = lowerBound_(first_syl);
    syl_hi = upperBound_(first_syl);
    syl_total = countSingleCharRun_(syl_lo, syl_hi);
  }

  // 前缀扩展段仅在没有精确词语时兜底（部分拼音输入仍可命中完整词）。
  const bool include_sub = (exact_total == 0);
  const std::uint64_t effective_sub = include_sub ? sub_total : 0;
  const std::uint64_t total = exact_total + syl_total + effective_sub;

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
    emitSingleCharRun_(syl_lo, syl_hi, &skip, limit, &result.batch);
  }
  if (include_sub && result.batch.items.size() < limit) {
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