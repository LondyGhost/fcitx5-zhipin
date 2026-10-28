// A3 test fixture: builds v1.0 engine index files from (code, text, weight,
// source) entries, plus a bit-at-a-time CRC-32 that is independent of the
// library's table-driven crc32 so the loader is cross-checked against a second
// implementation. Tests build their own small indexes; this avoids depending on
// the A2 importer before it is delivered.
#ifndef ABC_A3TEST_INDEX_BUILDER_HPP_
#define ABC_A3TEST_INDEX_BUILDER_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "abc/index_format.h"

namespace a3test {

struct Entry {
  std::string code;
  std::string text;
  std::string source;
  double weight = 0.0;
};

// Independent CRC-32/IEEE: bit-at-a-time, reflected, poly 0xEDB88320,
// init and final xor 0xFFFFFFFF. Computes over buf[from, to).
inline std::uint32_t crc32Bits(const std::uint8_t* buf, std::size_t from,
                               std::size_t to) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = from; i < to; ++i) {
    crc ^= buf[i];
    for (int k = 0; k < 8; ++k) {
      const std::uint32_t mask =
          0u - static_cast<std::uint32_t>(crc & 1u);  // all-ones when LSB set
      crc = (crc >> 1) ^ (mask & 0xEDB88320u);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

namespace detail {

constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kOffVersion = 0x08;
constexpr std::size_t kOffRecordCount = 0x0C;
constexpr std::size_t kOffCodeCount = 0x14;
constexpr std::size_t kOffCrc32 = 0x1C;
constexpr std::size_t kOffFlags = 0x20;
constexpr std::size_t kOffReserved0 = 0x24;
constexpr std::size_t kOffReserved1 = 0x28;
constexpr std::size_t kCrcBegin = kOffFlags;

inline void putU16(std::vector<std::uint8_t>* b, std::uint16_t v) {
  b->push_back(static_cast<std::uint8_t>(v & 0xFFu));
  b->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}
inline void putU32(std::vector<std::uint8_t>* b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b->push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
inline void putU64(std::vector<std::uint8_t>* b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i)
    b->push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
inline void putF64(std::vector<std::uint8_t>* b, double d) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  putU64(b, bits);
}

}  // namespace detail

// Serialises one record frame (no header). The stored order (code ascending,
// then weight descending, then text ascending) is applied by buildIndex.
inline std::vector<std::uint8_t> serialiseRecord(const Entry& e,
                                                 std::uint64_t stable_id) {
  std::vector<std::uint8_t> r;
  r.reserve(24 + e.code.size() + e.text.size() + e.source.size());
  detail::putU16(&r, static_cast<std::uint16_t>(e.code.size()));
  r.insert(r.end(), e.code.begin(), e.code.end());
  detail::putU16(&r, static_cast<std::uint16_t>(e.text.size()));
  r.insert(r.end(), e.text.begin(), e.text.end());
  detail::putF64(&r, e.weight);
  r.push_back(static_cast<std::uint8_t>(e.source.size()));
  r.insert(r.end(), e.source.begin(), e.source.end());
  detail::putU64(&r, stable_id);
  return r;
}

// Builds a complete v1.0 index. Entries are copied, sorted into storage order,
// deduplicated by (code, text) keeping the highest weight, and the header is
// filled with the correct recordCount / codeCount / CRC.
inline std::vector<std::uint8_t> buildIndex(const std::vector<Entry>& in) {
  std::vector<Entry> es = in;
  // Group identical (code, text) together, highest weight first, so adjacent
  // dedup keeps the highest-weight entry regardless of storage-order position.
  std::sort(es.begin(), es.end(), [](const Entry& a, const Entry& b) {
    if (a.code != b.code) return a.code < b.code;
    if (a.text != b.text) return a.text < b.text;
    return a.weight > b.weight;
  });
  // Deduplicate by (code, text) keeping the highest weight.
  std::vector<Entry> uniq;
  for (const Entry& e : es) {
    if (!uniq.empty() && uniq.back().code == e.code &&
        uniq.back().text == e.text) {
      continue;
    }
    uniq.push_back(e);
  }
  // Final storage order: code ascending, weight descending, text ascending.
  std::sort(uniq.begin(), uniq.end(), [](const Entry& a, const Entry& b) {
    if (a.code != b.code) return a.code < b.code;
    if (a.weight != b.weight) return a.weight > b.weight;
    return a.text < b.text;
  });
  es.swap(uniq);

  std::size_t distinct_codes = 0;
  std::string last_code;
  std::vector<std::uint8_t> body;
  for (std::size_t i = 0; i < es.size(); ++i) {
    if (i == 0 || es[i].code != es[i - 1].code) ++distinct_codes;
    const std::uint64_t sid =
        abc::computeStableId(es[i].code, es[i].text, es[i].source);
    std::vector<std::uint8_t> frame = serialiseRecord(es[i], sid);
    body.insert(body.end(), frame.begin(), frame.end());
  }

  std::vector<std::uint8_t> out;
  out.reserve(detail::kHeaderSize + body.size());
  const char magic[8] = {'A', 'B', 'C', 'D', 'I', 'X', '1', '\0'};
  out.insert(out.end(), magic, magic + 8);
  detail::putU32(&out, abc::kIndexFormatVersion);
  detail::putU64(&out, static_cast<std::uint64_t>(es.size()));
  detail::putU64(&out, static_cast<std::uint64_t>(distinct_codes));
  detail::putU32(&out, 0u);  // crc placeholder
  detail::putU32(&out, 0u);  // flags
  detail::putU32(&out, 0u);  // reserved0
  detail::putU64(&out, 0u);  // reserved1
  out.insert(out.end(), body.begin(), body.end());

  const std::uint32_t crc =
      crc32Bits(out.data(), detail::kCrcBegin, out.size());
  // Patch the CRC field at kOffCrc32.
  out[detail::kOffCrc32 + 0] = static_cast<std::uint8_t>(crc & 0xFFu);
  out[detail::kOffCrc32 + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
  out[detail::kOffCrc32 + 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFFu);
  out[detail::kOffCrc32 + 3] = static_cast<std::uint8_t>((crc >> 24) & 0xFFu);
  return out;
}

// Low-level builder for corruption tests: takes already-serialised record
// frames (any order, any stableId) and writes a header with the given fields,
// computing the CRC over [0x20, EOF). This lets a test craft an out-of-order
// record area, a wrong codeCount, a wrong version, or non-zero reserved fields.
inline std::vector<std::uint8_t> buildRaw(
    const std::vector<std::vector<std::uint8_t>>& frames,
    std::uint64_t record_count, std::uint64_t code_count,
    std::uint32_t version, std::uint32_t flags = 0u,
    std::uint32_t reserved0 = 0u, std::uint64_t reserved1 = 0u) {
  std::vector<std::uint8_t> body;
  for (const auto& f : frames) {
    body.insert(body.end(), f.begin(), f.end());
  }
  std::vector<std::uint8_t> out;
  out.reserve(detail::kHeaderSize + body.size());
  const char magic[8] = {'A', 'B', 'C', 'D', 'I', 'X', '1', '\0'};
  out.insert(out.end(), magic, magic + 8);
  detail::putU32(&out, version);
  detail::putU64(&out, record_count);
  detail::putU64(&out, code_count);
  detail::putU32(&out, 0u);  // crc placeholder
  detail::putU32(&out, flags);
  detail::putU32(&out, reserved0);
  detail::putU64(&out, reserved1);
  out.insert(out.end(), body.begin(), body.end());
  const std::uint32_t crc =
      crc32Bits(out.data(), detail::kCrcBegin, out.size());
  out[detail::kOffCrc32 + 0] = static_cast<std::uint8_t>(crc & 0xFFu);
  out[detail::kOffCrc32 + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
  out[detail::kOffCrc32 + 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFFu);
  out[detail::kOffCrc32 + 3] = static_cast<std::uint8_t>((crc >> 24) & 0xFFu);
  return out;
}

}  // namespace a3test

#endif  // ABC_A3TEST_INDEX_BUILDER_HPP_