// A3 private helpers (not part of the public contract).
//
// Only source/dictionary/** includes this file. Public headers live in
// source/include/abc/ and are owned by A0; A3 does not add anything there.
//
// Contents:
//   - little-endian scalar reads (index-format.md section 1 is
//     architecture-independent little endian); byte-by-byte, so no strict
//     aliasing / alignment / host endianness assumptions;
//   - CRC-32/IEEE (reflected, poly 0xEDB88320, init and final xor 0xFFFFFFFF);
//   - strict UTF-8 validation (rejects overlong forms, UTF-16 surrogate code
//     points U+D800..U+DFFF, code points above U+10FFFF and truncated sequences);
//   - control-character test used by index-format.md section 3.
//
// Threading: every function here is pure (depends only on its arguments and has
// no mutable global state), so any thread may call it concurrently. The only
// table is constexpr and lives in read-only storage.

#ifndef ABC_DICTIONARY_INTERNAL_BYTES_HPP_
#define ABC_DICTIONARY_INTERNAL_BYTES_HPP_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace abc {
namespace dictionary_internal {

// Little-endian scalar reads.
// Callers must guarantee that [p, p + N) is inside an already validated buffer;
// every call site in this module checks the remaining byte count first.
inline std::uint8_t readU8(const std::uint8_t* p) { return p[0]; }

inline std::uint16_t readU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(p[0]) |
      (static_cast<std::uint32_t>(p[1]) << 8));
}

inline std::uint32_t readU32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t readU64(const std::uint8_t* p) {
  return static_cast<std::uint64_t>(readU32(p)) |
         (static_cast<std::uint64_t>(readU32(p + 4)) << 32);
}

// IEEE-754 binary64 little endian weight. Requires a 64-bit double with a
// 64-bit unsigned integer of the same width (true for x86_64 / aarch64).
inline double readF64(const std::uint8_t* p) {
  static_assert(sizeof(double) == 8, "index format requires 64-bit double");
  static_assert(sizeof(std::uint64_t) == 8, "index format requires 64-bit u64");
  const std::uint64_t bits = readU64(p);
  double v = 0.0;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

// Byte-wise (UTF-8) comparison. C++17 string_view::operator< compares chars as
// signed on some platforms; this is the explicit unsigned memcmp semantics the
// storage-order contract fixes (index-format.md 3.1).
inline int compareBytes(std::string_view a, std::string_view b) {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  if (n != 0) {
    const int c = std::memcmp(a.data(), b.data(), n);
    if (c != 0) return c < 0 ? -1 : 1;
  }
  if (a.size() == b.size()) return 0;
  return a.size() < b.size() ? -1 : 1;
}

// CRC-32/IEEE (zlib polynomial, reflected, init = final xor = 0xFFFFFFFF).
namespace crc_detail {

constexpr std::uint32_t makeEntry(std::uint32_t index) {
  std::uint32_t c = index;
  for (int k = 0; k < 8; ++k) {
    c = ((c & 1u) != 0u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
  }
  return c;
}

struct Table {
  std::uint32_t v[256];
  constexpr Table() : v{} {
    for (std::uint32_t i = 0; i < 256; ++i) v[i] = makeEntry(i);
  }
};

constexpr Table kTable{};
static_assert(kTable.v[0] == 0x00000000u, "CRC-32 table[0]");
static_assert(kTable.v[1] == 0x77073096u, "CRC-32 table[1]");
static_assert(kTable.v[255] == 0x2D02EF8Du, "CRC-32 table[255]");

}  // namespace crc_detail

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc = crc_detail::kTable.v[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// Strict UTF-8 validation.
inline bool isValidUtf8(std::string_view s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  const std::size_t n = s.size();
  std::size_t i = 0;
  while (i < n) {
    const unsigned char c = p[i];
    if (c < 0x80u) {
      ++i;
      continue;
    }
    std::size_t extra = 0;      // number of continuation bytes
    unsigned char lo = 0x80u;  // allowed range of the first continuation byte
    unsigned char hi = 0xBFu;
    if (c >= 0xC2u && c <= 0xDFu) {
      extra = 1;
    } else if (c >= 0xE0u && c <= 0xEFu) {
      extra = 2;
      if (c == 0xE0u) lo = 0xA0u;  // reject overlong
      if (c == 0xEDu) hi = 0x9Fu;  // reject UTF-16 surrogates
    } else if (c >= 0xF0u && c <= 0xF4u) {
      extra = 3;
      if (c == 0xF0u) lo = 0x90u;  // reject overlong
      if (c == 0xF4u) hi = 0x8Fu;  // cap at U+10FFFF
    } else {
      return false;  // 0x80..0xC1 and 0xF5..0xFF cannot start a sequence
    }
    if (i + extra >= n) return false;  // truncated tail
    const unsigned char c1 = p[i + 1];
    if (c1 < lo || c1 > hi) return false;
    for (std::size_t k = 2; k <= extra; ++k) {
      if ((p[i + k] & 0xC0u) != 0x80u) return false;
    }
    i += extra + 1;
  }
  return true;
}

// True if s contains a byte that index-format.md section 3 forbids:
// NUL, ASCII control characters (< 0x20) or DEL (0x7F).
// (C0 / DEL are the control characters that can occur inside a valid UTF-8
// string; C1 code points are encoded as two bytes whose first byte is >= 0xC2
// and are therefore not matched here. See docs/modules/dictionary-runtime.md.)
inline bool containsControl(std::string_view s) {
  for (const unsigned char c : s) {
    if (c < 0x20u || c == 0x7Fu) return true;
  }
  return false;
}

}  // namespace dictionary_internal
}  // namespace abc

#endif  // ABC_DICTIONARY_INTERNAL_BYTES_HPP_
