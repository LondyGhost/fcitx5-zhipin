// A3 - engine index read-only loader (index-format.md v1.0).
//
// Public entry: abc::loadDictionaryFromFile. The declaration is owned by A0
// (abc/dictionary.h); this file only provides its definition.
//
// Validation follows index-format.md section 5 in order:
//   1. readable and >= 48 bytes                   kIoError / kCorruptIndex
//   2. magic byte-by-byte equal                   kCorruptIndex
//   3. version compatibility (section 4)         kUnsupportedVersion
//   4. flags / reserved0 / reserved1 are zero     kCorruptIndex
//   5. CRC-32/IEEE over [0x20, EOF)               kCorruptIndex
//   6. recordCount limit and frame budget         kCorruptIndex (before reserving)
//   7. per-record frame, every length checked against the remaining bytes
//   8. storage order (3.1) and (code, text) uniqueness
//   9. codeCount equals the number of distinct codes hint
//
// Resource bounds: the buffer size comes from fstat, never from a header field.
// kMaxLoadableBytes is a fixed ceiling and the record table is reserved with a cap
// derived from the file size. No file is written. No exception leaves this module
// (std::bad_alloc maps to kIoError with a diagnostic).

#if !defined(_WIN32)
#define ABC_DICTIONARY_POSIX 1
#endif

#if defined(ABC_DICTIONARY_POSIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc/dictionary.h"
#include "abc/index_format.h"
#include "internal/bytes.hpp"
#include "internal/index.hpp"

namespace abc {
namespace {

using dictionary_internal::compareBytes;
using dictionary_internal::containsControl;
using dictionary_internal::crc32;
using dictionary_internal::isValidUtf8;
using dictionary_internal::readF64;
using dictionary_internal::readU16;
using dictionary_internal::readU32;
using dictionary_internal::readU64;

// Header layout, index-format.md section 2.
constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kOffMagic = 0x00;
constexpr std::size_t kOffVersion = 0x08;
constexpr std::size_t kOffRecordCount = 0x0C;
constexpr std::size_t kOffCodeCount = 0x14;
constexpr std::size_t kOffCrc32 = 0x1C;
constexpr std::size_t kOffFlags = 0x20;
constexpr std::size_t kOffReserved0 = 0x24;
constexpr std::size_t kOffReserved1 = 0x28;
constexpr std::size_t kCrcBegin = kOffFlags;

// Hard ceiling for one loaded index (3 GiB). The v1 target is 2,000,000 records
// and <= 200 MB, so the largest possible allocation stays a documented constant
// instead of "whatever the file says".
constexpr std::size_t kMaxLoadableBytes =
    static_cast<std::size_t>(std::uint64_t{3} << 30);

// Upper bound for a single diagnostics field preview.
constexpr std::size_t kMaxDetailField = 32;
std::string hexOffset(std::uint64_t off) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx",
                static_cast<unsigned long long>(off));
  return buf;
}

std::string decimal(std::uint64_t v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
  return buf;
}

std::string versionText(std::uint32_t v) {
  return decimal(v >> 16) + "." + decimal(v & 0xFFFFu);
}

DictionaryLoadResult makeFailure(DictionaryStatus status,
                                  const std::string& path,
                                  const std::string& where,
                                  const std::string& why) {
  DictionaryLoadResult r;
  r.status = status;
  r.detail = path + ": " + where + ": " + why;
  return r;
}

// Reads the whole file into memory. The size comes from fstat.
bool readWholeFile(const std::string& path, std::vector<unsigned char>* out,
                   std::string* why) {
#if defined(ABC_DICTIONARY_POSIX)
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    *why = std::string("open failed: ") + std::strerror(errno);
    return false;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0) {
    *why = std::string("fstat failed: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  if (!S_ISREG(st.st_mode)) {
    *why = "not a regular file";
    ::close(fd);
    return false;
  }
  if (st.st_size < 0) {
    *why = "negative file size";
    ::close(fd);
    return false;
  }
  const std::uint64_t size64 = static_cast<std::uint64_t>(st.st_size);
  if (size64 > static_cast<std::uint64_t>(kMaxLoadableBytes)) {
    *why = "file too large: " + decimal(size64) + " bytes, limit " +
           decimal(kMaxLoadableBytes);
    ::close(fd);
    return false;
  }
  const std::size_t size = static_cast<std::size_t>(size64);
  try {
    out->resize(size);
  } catch (const std::bad_alloc&) {
    *why = "out of memory allocating " + decimal(size) + " bytes";
    ::close(fd);
    return false;
  }
  std::size_t got = 0;
  while (got < size) {
    const ssize_t n = ::read(fd, out->data() + got, size - got);
    if (n < 0) {
      if (errno == EINTR) continue;
      *why = std::string("read failed: ") + std::strerror(errno);
      ::close(fd);
      return false;
    }
    if (n == 0) break;
    got += static_cast<std::size_t>(n);
  }
  ::close(fd);
  if (got != size) {
    *why = "short read: got " + decimal(got) + " of " + decimal(size) +
           " bytes (file truncated while loading)";
    return false;
  }
  return true;
#else
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    *why = "open failed";
    return false;
  }
  if (std::fseek(f, 0, SEEK_END) != 0) {
    *why = "seek failed";
    std::fclose(f);
    return false;
  }
  const long size_l = std::ftell(f);
  if (size_l < 0 || static_cast<unsigned long>(size_l) >
                        static_cast<unsigned long>(kMaxLoadableBytes)) {
    *why = "file too large or unseekable";
    std::fclose(f);
    return false;
  }
  const std::size_t size = static_cast<std::size_t>(size_l);
  std::rewind(f);
  try {
    out->resize(size);
  } catch (const std::bad_alloc&) {
    *why = "out of memory allocating " + decimal(size) + " bytes";
    std::fclose(f);
    return false;
  }
  const std::size_t got = std::fread(out->data(), 1, size, f);
  std::fclose(f);
  if (got != size) {
    *why = "short read: got " + decimal(got) + " of " + decimal(size) +
           " bytes (file truncated while loading)";
    return false;
  }
  return true;
#endif
}

// One parsed record frame; views point into file_bytes.
struct ParsedRecord {
  std::uint16_t code_len = 0;
  std::uint16_t text_len = 0;
  double weight = 0.0;
  std::uint8_t source_len = 0;
  std::uint64_t stable_id = 0;
  std::size_t frame_len = 0;   // total bytes of this record
  std::string_view code;
  std::string_view text;
  std::string_view source;
};

bool failRecordAt(std::string* why, std::size_t off, const std::string& what) {
  *why = "record at " + hexOffset(off) + ": " + what;
  return false;
}

// Parses one record starting at offset o. limit is the first byte past the record
// area (the file size). Every length is checked against the remaining bytes.
bool parseRecordAt(const std::vector<unsigned char>& b, std::size_t o,
                   std::size_t limit, ParsedRecord* rec, std::string* why) {
  if (limit - o < 2) return failRecordAt(why, o, "truncated code length");
  rec->code_len = readU16(b.data() + o);
  o += 2;
  if (rec->code_len == 0) return failRecordAt(why, o, "empty code");
  if (rec->code_len > kIndexMaxCodeBytes)
    return failRecordAt(why, o, "code length exceeds limit");
  if (limit - o < rec->code_len) return failRecordAt(why, o, "truncated code");
  if (containsControl(std::string_view(
          reinterpret_cast<const char*>(b.data() + o), rec->code_len)))
    return failRecordAt(why, o, "code contains a control byte");
  rec->code = std::string_view(
      reinterpret_cast<const char*>(b.data() + o), rec->code_len);
  o += rec->code_len;

  if (limit - o < 2) return failRecordAt(why, o, "truncated text length");
  rec->text_len = readU16(b.data() + o);
  o += 2;
  if (rec->text_len == 0) return failRecordAt(why, o, "empty text");
  if (rec->text_len > kIndexMaxTextBytes)
    return failRecordAt(why, o, "text length exceeds limit");
  if (limit - o < rec->text_len) return failRecordAt(why, o, "truncated text");
  const std::string_view text_view(
      reinterpret_cast<const char*>(b.data() + o), rec->text_len);
  if (containsControl(text_view)) return failRecordAt(why, o, "text has control byte");
  if (!isValidUtf8(text_view)) return failRecordAt(why, o, "text is not valid UTF-8");
  rec->text = text_view;
  o += rec->text_len;

  if (limit - o < 8) return failRecordAt(why, o, "truncated weight");
  rec->weight = readF64(b.data() + o);
  if (!std::isfinite(rec->weight))
    return failRecordAt(why, o, "weight is not finite");
  o += 8;

  if (limit - o < 1) return failRecordAt(why, o, "truncated source length");
  rec->source_len = b[o];
  ++o;
  if (rec->source_len == 0) return failRecordAt(why, o, "empty source");
  if (rec->source_len > kIndexMaxSourceBytes)
    return failRecordAt(why, o, "source length exceeds limit");
  if (limit - o < rec->source_len) return failRecordAt(why, o, "truncated source");
  const std::string_view source_view(
      reinterpret_cast<const char*>(b.data() + o), rec->source_len);
  if (containsControl(source_view)) return failRecordAt(why, o, "source has control byte");
  if (!isValidUtf8(source_view)) return failRecordAt(why, o, "source is not valid UTF-8");
  rec->source = source_view;
  o += rec->source_len;

  if (limit - o < 8) return failRecordAt(why, o, "truncated stable id");
  rec->stable_id = readU64(b.data() + o);
  o += 8;

  // Recompute the stable id and reject a mismatch (dictionary.md 2.2, the
  // loader "should" recheck; a wrong id breaks cross-session stability).
  if (rec->stable_id != computeStableId(rec->code, rec->text, rec->source))
    return failRecordAt(why, o, "stable id mismatch");

  rec->frame_len = o;
  return true;
}

}  // namespace

// Builds the runtime dictionary after all index-format.md section 5 checks.
// Returns kOk on success; otherwise a failure result with a located detail.
DictionaryLoadResult parseIndexFile(const std::string& path,
                                    std::vector<unsigned char> file) {
  const std::size_t size = file.size();
  const std::string where = "header";

  // 5.1 size >= 48 bytes.
  if (size < kHeaderSize) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "file is " + decimal(size) + " bytes, below the " +
                           decimal(kHeaderSize) + "-byte header");
  }

  // 5.2 magic, byte by byte.
  const auto* p = file.data();
  for (std::size_t i = 0; i < 8; ++i) {
    if (p[kOffMagic + i] != kIndexMagic[i]) {
      return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                         "bad magic at byte " + decimal(kOffMagic + i));
    }
  }

  // 5.3 version compatibility (index-format.md section 4).
  const std::uint32_t file_version = readU32(p + kOffVersion);
  const std::uint32_t file_major = file_version >> 16;
  const std::uint32_t file_minor = file_version & 0xFFFFu;
  const bool compatible =
      (file_major == kIndexFormatVersionMajor) &&
      (file_minor <= kIndexFormatVersionMinor);
  if (!compatible) {
    return makeFailure(
        DictionaryStatus::kUnsupportedVersion, path, where,
        "version " + versionText(file_version) + " is not compatible with " +
            versionText(kIndexFormatVersion));
  }

  // 5.4 reserved fields must be zero.
  if (readU32(p + kOffFlags) != 0u)
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where, "flags != 0");
  if (readU32(p + kOffReserved0) != 0u)
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where, "reserved0 != 0");
  if (readU64(p + kOffReserved1) != 0u)
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where, "reserved1 != 0");

  // 5.5 CRC-32/IEEE over [0x20, EOF).
  const std::uint32_t stored_crc = readU32(p + kOffCrc32);
  const std::uint32_t actual_crc = crc32(p + kCrcBegin, size - kCrcBegin);
  if (stored_crc != actual_crc) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "crc mismatch: stored 0x" + hexOffset(stored_crc) +
                           ", computed 0x" + hexOffset(actual_crc));
  }

  // 5.6 recordCount bound and frame budget, checked before any allocation.
  const std::uint64_t record_count64 = readU64(p + kOffRecordCount);
  const std::uint64_t code_count64 = readU64(p + kOffCodeCount);
  if (record_count64 > kIndexMaxRecords) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "recordCount " + decimal(record_count64) + " exceeds " +
                           decimal(kIndexMaxRecords));
  }
  const std::uint64_t record_bytes = size - kHeaderSize;
  if (record_count64 > 0 &&
      record_count64 * kIndexMinRecordBytes > record_bytes) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "recordCount budget: " + decimal(record_count64) +
                           " records need " +
                           decimal(record_count64 * kIndexMinRecordBytes) +
                           " bytes, record area is " + decimal(record_bytes));
  }
  const std::size_t record_count = static_cast<std::size_t>(record_count64);

  // Reserve the record table with the budget-derived bound (never the raw
  // header field alone beyond the max), so allocation stays bounded.
  const std::size_t budget_records = record_bytes / kIndexMinRecordBytes;
  const std::size_t reserve_cap =
      record_count < budget_records ? record_count : budget_records;

  std::vector<dictionary_internal::RecordRef> refs;
  std::vector<Candidate> prev;
  std::size_t distinct_codes = 0;
  try {
    refs.reserve(reserve_cap);
  } catch (const std::bad_alloc&) {
    return makeFailure(DictionaryStatus::kIoError, path, where,
                       "out of memory reserving " + decimal(reserve_cap) +
                           " records");
  }

  // 5.7 + 5.8 + 5.9 parse every record, check storage order and code count.
  std::size_t off = kHeaderSize;
  for (std::uint64_t idx = 0; idx < record_count; ++idx) {
    const std::size_t rec_start = off;
    ParsedRecord rec;
    std::string why;
    if (!parseRecordAt(file, off, size, &rec, &why)) {
      return makeFailure(DictionaryStatus::kCorruptIndex, path,
                         "record " + decimal(idx), why);
    }
    off = rec.frame_len;

    // Storage order and (code, text) uniqueness (index-format.md 3.1).
    if (!prev.empty()) {
      const int code_cmp = compareBytes(prev[0].code, rec.code);
      if (code_cmp > 0) {
        return makeFailure(DictionaryStatus::kCorruptIndex, path,
                           "record " + decimal(idx), "code out of order");
      }
      if (code_cmp == 0) {
        if (prev[0].weight < rec.weight) {
          return makeFailure(DictionaryStatus::kCorruptIndex, path,
                             "record " + decimal(idx), "weight out of order");
        }
        if (prev[0].weight == rec.weight) {
          const int text_cmp = compareBytes(prev[0].text, rec.text);
          if (text_cmp >= 0) {
            return makeFailure(DictionaryStatus::kCorruptIndex, path,
                               "record " + decimal(idx),
                               "text not strictly ascending (duplicate pair)");
          }
        }
      } else {
        ++distinct_codes;
      }
    } else {
      ++distinct_codes;
    }

    // Stash decoded fields for the next comparison.
    prev.clear();
    Candidate c;
    c.code.assign(rec.code.data(), rec.code.size());
    c.text.assign(rec.text.data(), rec.text.size());
    c.weight = rec.weight;
    c.source.assign(rec.source.data(), rec.source.size());
    c.stableId = rec.stable_id;
    prev.push_back(std::move(c));

    dictionary_internal::RecordRef rf;
    rf.offset = static_cast<std::uint32_t>(rec_start);
    rf.length = static_cast<std::uint32_t>(rec.frame_len - rec_start);
    rf.code_len = rec.code_len;
    refs.push_back(rf);
  }

  // The record area must consume the file exactly (no trailing bytes).
  if (off != size) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "trailing " + decimal(size - off) + " bytes after records");
  }

  // 5.9 codeCount must equal the actual number of distinct codes.
  if (distinct_codes != code_count64) {
    return makeFailure(DictionaryStatus::kCorruptIndex, path, where,
                       "codeCount " + decimal(code_count64) +
                           " != actual " + decimal(distinct_codes));
  }

  auto dict = std::make_unique<dictionary_internal::RuntimeDictionary>(
      std::move(file), std::move(refs), distinct_codes);
  DictionaryLoadResult r;
  r.status = DictionaryStatus::kOk;
  r.dictionary = std::move(dict);
  return r;
}

DictionaryLoadResult loadDictionaryFromFile(const std::string& path) {
  std::vector<unsigned char> file;
  std::string why;
  if (!readWholeFile(path, &file, &why)) {
    return makeFailure(DictionaryStatus::kIoError, path, "open", why);
  }
  return parseIndexFile(path, std::move(file));
}

}  // namespace abc
