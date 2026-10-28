// A2 — Rime 词库导入器实现（离线索引编译器）。
//
// 本文件实现 dict_importer.hpp 声明的全部行为：
//   解析（有界、逐行诊断）→ 去重（契约 2.3）→ 排序（契约 3.1）→
//   序列化（index-format.md v1.0）→ 输出自检（契约 §6）→ 写出。
//
// 依赖方向（build.md §4）：仅 abc_interfaces 公共头 + 标准库；
// 不链接 fcitx5/X11/Wayland；不读取环境变量；不执行词库内容。
//
// 与 A3 loader 的算法一致性说明：CRC-32/IEEE、严格 UTF-8 校验、控制字符
// 判定、字节序比较的参数由 index-format.md / dictionary.md 冻结，两个模块
// 各自独立实现同一契约（零依赖模块间不共享内部头）。交叉验证由
// tests/dict-importer 的 cross 测试用 A3 loadDictionaryFromFile 复核。
//
// 平台假设（与 A3 internal/bytes.hpp 相同）：宿主为小端 IEEE-754 双精度
// （x86_64 / aarch64）。weight 以 double 的位模式经小端 u64 写出。

#include "dict_importer.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abc/dictionary.h"  // computeStableId（A0 冻结实现，禁止另写算法）

namespace abc {
namespace dict_import {
namespace {

using abc::computeStableId;
using abc::kIndexFormatVersion;
using abc::kIndexMagic;
using abc::kIndexMaxCodeBytes;
using abc::kIndexMaxRecords;
using abc::kIndexMaxSourceBytes;
using abc::kIndexMaxTextBytes;

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------

constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kOffRecordCount = 0x0C;
constexpr std::size_t kOffCodeCount = 0x14;
constexpr std::size_t kOffCrc32 = 0x1C;
constexpr std::size_t kCrcBegin = 0x20;  // CRC 覆盖 [0x20, EOF)

// 输入文件上界（512 MiB）：rime-ice 全量词库 ≈ 数十 MB，此界仅为资源防御。
constexpr std::uint64_t kMaxInputBytes = std::uint64_t{512} << 20;

constexpr std::size_t kExcerptBytes = 48;  // 坏行摘录的最大原始字节数

// ---------------------------------------------------------------------------
// 字节工具（契约冻结算法的 A2 侧实现）
// ---------------------------------------------------------------------------

inline void appendU16(std::vector<unsigned char>* out, std::uint16_t v) {
  out->push_back(static_cast<unsigned char>(v & 0xFFu));
  out->push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

inline void appendU32(std::vector<unsigned char>* out, std::uint32_t v) {
  out->push_back(static_cast<unsigned char>(v & 0xFFu));
  out->push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
  out->push_back(static_cast<unsigned char>((v >> 16) & 0xFFu));
  out->push_back(static_cast<unsigned char>((v >> 24) & 0xFFu));
}

inline void appendU64(std::vector<unsigned char>* out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out->push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
  }
}

// CRC-32/IEEE：反射、poly 0xEDB88320、初值/终值 0xFFFFFFFF（契约 §2）。
struct CrcTable {
  std::uint32_t v[256];
  constexpr CrcTable() : v{} {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = ((c & 1u) != 0u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      v[i] = c;
    }
  }
};
constexpr CrcTable kCrcTable{};
static_assert(kCrcTable.v[1] == 0x77073096u, "CRC-32 table[1] vector");
static_assert(kCrcTable.v[255] == 0x2D02EF8Du, "CRC-32 table[255] vector");

std::uint32_t crc32(const unsigned char* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc = kCrcTable.v[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// 严格 UTF-8：拒绝超长编码、UTF-16 代理区（U+D800..U+DFFF）、> U+10FFFF、
// 截断序列（与 index-format.md §3 "合法 UTF-8" 同义）。
bool isValidUtf8(std::string_view s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s.data());
  const std::size_t n = s.size();
  std::size_t i = 0;
  while (i < n) {
    const unsigned char c = p[i];
    if (c < 0x80u) {
      ++i;
      continue;
    }
    std::size_t extra = 0;
    unsigned char lo = 0x80u;
    unsigned char hi = 0xBFu;
    if (c >= 0xC2u && c <= 0xDFu) {
      extra = 1;
    } else if (c >= 0xE0u && c <= 0xEFu) {
      extra = 2;
      if (c == 0xE0u) lo = 0xA0u;  // 拒绝超长
      if (c == 0xEDu) hi = 0x9Fu;  // 拒绝代理区
    } else if (c >= 0xF0u && c <= 0xF4u) {
      extra = 3;
      if (c == 0xF0u) lo = 0x90u;  // 拒绝超长
      if (c == 0xF4u) hi = 0x8Fu;  // 上限 U+10FFFF
    } else {
      return false;
    }
    if (i + extra >= n) return false;  // 截断
    if (p[i + 1] < lo || p[i + 1] > hi) return false;
    for (std::size_t k = 2; k <= extra; ++k) {
      if ((p[i + k] & 0xC0u) != 0x80u) return false;
    }
    i += extra + 1;
  }
  return true;
}

// index-format.md §3 禁止的字符串内容字节：NUL、< 0x20 的控制字符、DEL。
inline bool containsControl(std::string_view s) {
  for (const unsigned char c : s) {
    if (c < 0x20u || c == 0x7Fu) return true;
  }
  return false;
}

// 无符号字节序比较（UTF-8 字节序 memcmp 语义，契约 3.1/4.1）。
inline int compareBytes(std::string_view a, std::string_view b) {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  if (n != 0) {
    const int c = std::memcmp(a.data(), b.data(), n);
    if (c != 0) return c < 0 ? -1 : 1;
  }
  if (a.size() == b.size()) return 0;
  return a.size() < b.size() ? -1 : 1;
}

// ---------------------------------------------------------------------------
// 解析产物
// ---------------------------------------------------------------------------

struct RawRow {
  std::string code;        // 归一化后的编码（纯 a-z，1..128 字节）
  std::string text;        // 原样词条（合法 UTF-8，无控制字符）
  std::string source;      // 来源标识
  double weight = 0.0;     // 有限值
  std::uint64_t line = 0;  // 1 基源行号（诊断用）
  std::uint64_t seq = 0;   // 导入顺序（去重平局规则）
};

enum class ParserState { kStart, kHeader, kTable };

// 生成坏行摘录：0x20..0x7E 之外的字节 → \xNN，反斜杠 → \\，
// 最多 kExcerptBytes 原始字节，截断追加 "..."。
std::string makeExcerpt(std::string_view raw) {
  std::string out;
  out.reserve(raw.size() * 2 + 4);
  const std::size_t n = raw.size() < kExcerptBytes ? raw.size() : kExcerptBytes;
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (c == '\\') {
      out += "\\\\";
    } else if (c >= 0x20u && c <= 0x7Eu) {
      out += static_cast<char>(c);
    } else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\x%02X", c);
      out += buf;
    }
  }
  if (raw.size() > kExcerptBytes) out += "...";
  return out;
}

// 行内第一个非空白字节的下标；全部空白/空行 → npos。
inline std::size_t firstNonSpace(std::string_view raw) {
  return raw.find_first_not_of(" \t");
}

// 行尾裁剪（标记识别容错：`...   ` 视同 `...`）。
inline std::string_view trimmedRight(std::string_view raw) {
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) {
    raw.remove_suffix(1);
  }
  return raw;
}

std::string errnoText() { return std::string(std::strerror(errno)); }

bool readWholeFile(const std::string& path, std::vector<unsigned char>* out,
                   std::string* err) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    *err = "cannot open input: " + errnoText();
    return false;
  }
  if (std::fseek(f, 0, SEEK_END) != 0) {
    *err = "cannot seek input: " + errnoText();
    std::fclose(f);
    return false;
  }
  const long size_l = std::ftell(f);
  if (size_l < 0) {
    *err = "cannot determine input size: " + errnoText();
    std::fclose(f);
    return false;
  }
  if (static_cast<std::uint64_t>(size_l) > kMaxInputBytes) {
    *err = "input exceeds the 512 MiB limit (" +
           std::to_string(static_cast<std::uint64_t>(size_l)) + " bytes)";
    std::fclose(f);
    return false;
  }
  std::rewind(f);
  try {
    out->resize(static_cast<std::size_t>(size_l));
  } catch (const std::bad_alloc&) {
    *err = "out of memory reading input (" +
           std::to_string(static_cast<std::uint64_t>(size_l)) + " bytes)";
    std::fclose(f);
    return false;
  }
  const std::size_t got = std::fread(out->data(), 1, out->size(), f);
  const bool read_error = std::ferror(f) != 0;
  std::fclose(f);
  if (read_error || got != out->size()) {
    *err = "short read on input: got " + std::to_string(got) + " of " +
           std::to_string(out->size()) + " bytes";
    return false;
  }
  return true;
}

// 权重语法：^[+-]?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?$；
// 解析经 strtod（C 小数点）并要求整字段消费。返回值：0 = 成功，
// 1 = 空字段，2 = 语法错误，3 = 非有限（溢出）。
int parseWeight(std::string_view field, double* out) {
  if (field.empty()) return 1;
  std::size_t i = 0;
  if (field[i] == '+' || field[i] == '-') ++i;
  std::size_t digits = 0;
  while (i < field.size() && field[i] >= '0' && field[i] <= '9') {
    ++i;
    ++digits;
  }
  if (digits == 0) return 2;
  if (i < field.size() && field[i] == '.') {
    ++i;
    std::size_t frac = 0;
    while (i < field.size() && field[i] >= '0' && field[i] <= '9') {
      ++i;
      ++frac;
    }
    if (frac == 0) return 2;
  }
  if (i < field.size() && (field[i] == 'e' || field[i] == 'E')) {
    ++i;
    if (i < field.size() && (field[i] == '+' || field[i] == '-')) ++i;
    std::size_t exp_digits = 0;
    while (i < field.size() && field[i] >= '0' && field[i] <= '9') {
      ++i;
      ++exp_digits;
    }
    if (exp_digits == 0) return 2;
  }
  if (i != field.size()) return 2;  // 尾随垃圾（含空格/本地化小数点）
  const std::string tmp(field);
  char* end = nullptr;
  const double v = std::strtod(tmp.c_str(), &end);
  if (end != tmp.c_str() + tmp.size()) return 2;  // locale 防御：必须整段消费
  if (!std::isfinite(v)) return 3;
  *out = (v == 0.0) ? 0.0 : v;  // 归一化 -0.0 → +0.0，保证位模式确定
  return 0;
}

// 编码归一化：去除空格分隔符（Rime 音节间空格）、折叠 A-Z → a-z。
// 返回 false 表示归一化后为空。
bool normalizeCode(std::string_view raw, std::string* out) {
  out->clear();
  for (const char c : raw) {
    if (c == ' ') continue;  // 只剥离空格；其余字符交给合法性检查
    if (c >= 'A' && c <= 'Z') {
      out->push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out->push_back(c);
    }
  }
  return !out->empty();
}

bool codeCharsValid(const std::string& code) {
  for (const char c : code) {
    if (c < 'a' || c > 'z') return false;
  }
  return true;
}

// 解析单行表体行。成功时填充 row 并返回 true；失败时给出稳定原因。
bool parseTableRow(std::string_view raw, const std::string& source,
                   std::uint64_t line, std::uint64_t seq, RawRow* row,
                   std::string* reason) {
  // 字段切分：以 \t 分隔，保留空字段（空字段有专属诊断）。
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t tab = raw.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(raw.substr(start));
      break;
    }
    fields.push_back(raw.substr(start, tab - start));
    start = tab + 1;
  }

  if (fields.size() == 1) {
    *reason = "missing code column";
    return false;
  }
  if (fields.size() > 3) {
    *reason = "too many fields (4th column not supported)";
    return false;
  }

  const std::string_view text = fields[0];
  if (text.empty()) {
    *reason = "empty text field";
    return false;
  }
  if (text.size() > kIndexMaxTextBytes) {
    *reason = "text exceeds 512 bytes";
    return false;
  }
  if (containsControl(text)) {
    *reason = "text contains a control character";
    return false;
  }
  if (!isValidUtf8(text)) {
    *reason = "text is not valid UTF-8";
    return false;
  }

  std::string code;
  if (!normalizeCode(fields[1], &code)) {
    *reason = "code is empty after normalization";
    return false;
  }
  if (code.size() > kIndexMaxCodeBytes) {
    *reason = "code exceeds 128 bytes";
    return false;
  }
  if (!codeCharsValid(code)) {
    *reason = "code contains an unsupported character";
    return false;
  }

  double weight = 0.0;
  if (fields.size() == 3) {
    const int wr = parseWeight(fields[2], &weight);
    if (wr == 1) {
      *reason = "empty weight field";
      return false;
    }
    if (wr == 2) {
      *reason = "invalid weight syntax";
      return false;
    }
    if (wr == 3) {
      *reason = "weight is not finite";
      return false;
    }
  }

  row->code = std::move(code);
  row->text.assign(text.data(), text.size());
  row->source = source;
  row->weight = weight;
  row->line = line;
  row->seq = seq;
  return true;
}

// 忽略键名集合：排序 + 去重（报告口径稳定）。
void sortUnique(std::vector<std::string>* v) {
  std::sort(v->begin(), v->end(),
            [](const std::string& a, const std::string& b) {
              return compareBytes(a, b) < 0;
            });
  v->erase(std::unique(v->begin(), v->end()), v->end());
}

// ---------------------------------------------------------------------------
// 解析整个输入
// ---------------------------------------------------------------------------

bool fatal(const std::string& path, const std::string& what,
           ImportResult* result) {
  result->exit_code = ExitCode::kFatal;
  result->error = path + ": " + what;
  return false;
}

bool parseInput(const std::string& path, const std::string& source,
                ImportReport* report, std::vector<RawRow>* rows,
                ImportResult* result) {
  std::vector<unsigned char> bytes;
  std::string err;
  if (!readWholeFile(path, &bytes, &err)) {
    return fatal(path, err, result);
  }

  std::size_t pos = 0;
  if (bytes.size() >= 3 && bytes[0] == 0xEFu && bytes[1] == 0xBBu &&
      bytes[2] == 0xBFu) {
    pos = 3;  // UTF-8 BOM：剥离并计入报告
    report->bom_stripped = true;
  } else if (bytes.size() >= 2 &&
             ((bytes[0] == 0xFFu && bytes[1] == 0xFEu) ||
              (bytes[0] == 0xFEu && bytes[1] == 0xFFu))) {
    return fatal(path,
                 "UTF-16 BOM at start of file: UTF-16 input is not supported "
                 "(convert to UTF-8)",
                 result);
  }

  ParserState state = ParserState::kStart;
  std::uint64_t line_no = 0;
  std::uint64_t seq = 0;
  std::vector<std::string>& ignored_keys = report->ignored_header_keys;

  // 表体行的公共处理（kStart 直落纯文本码表与 kTable 共用）。
  auto handleTableRow = [&](std::string_view raw) {
    ++report->counters.table_rows;
    RawRow row;
    std::string reason;
    if (parseTableRow(raw, source, line_no, seq, &row, &reason)) {
      ++report->counters.valid_rows;
      ++seq;
      if (report->counters.valid_rows > kIndexMaxRecords) {
        return fatal(path,
                     "valid row count exceeds kIndexMaxRecords (" +
                         std::to_string(kIndexMaxRecords) + ")",
                     result);
      }
      rows->push_back(std::move(row));
    } else {
      ++report->counters.skipped_rows;
      report->issues.push_back({line_no, reason, makeExcerpt(raw)});
    }
    return true;
  };

  while (pos < bytes.size()) {
    // 找行尾（\n；行尾 \r 剥离）。文件以 \n 结尾时不产生尾随空行。
    std::size_t eol = pos;
    while (eol < bytes.size() && bytes[eol] != '\n') ++eol;
    std::size_t end = eol;
    if (end > pos && bytes[end - 1] == '\r') --end;
    const std::string_view raw(reinterpret_cast<const char*>(bytes.data()) +
                                   pos,
                               end - pos);
    ++line_no;
    ++report->counters.lines_total;

    const std::size_t first = firstNonSpace(raw);
    if (first == std::string_view::npos) {
      ++report->counters.blank_lines;  // 空行或纯空白行
      if (eol == bytes.size()) break;
      pos = eol + 1;
      continue;
    }
    const bool is_comment = (raw[first] == '#');

    switch (state) {
      case ParserState::kStart: {
        if (is_comment) {
          ++report->counters.comment_lines;
          break;
        }
        const std::string_view t = trimmedRight(raw);
        if (t == "---") {
          state = ParserState::kHeader;
        } else if (t == "...") {
          return fatal(path,
                       "unexpected document end marker '...' before any "
                       "YAML header",
                       result);
        } else {
          // 无 YAML 头的纯文本码表：本行及其后全部按表体处理。
          state = ParserState::kTable;
          if (!handleTableRow(raw)) return false;
        }
        break;
      }
      case ParserState::kHeader: {
        if (is_comment) {
          ++report->counters.comment_lines;
          break;
        }
        const std::string_view t = trimmedRight(raw);
        if (t == "...") {
          state = ParserState::kTable;
          break;
        }
        if (t == "---") {
          return fatal(path, "repeated document start marker '---'", result);
        }
        if (raw[0] == ' ' || raw[0] == '\t') {
          return fatal(
              path,
              "nested YAML block in header: this importer only supports flat "
              "'key: value' headers (v1)",
              result);
        }
        {
          const std::size_t colon = raw.find(':');
          if (colon == std::string_view::npos) {
            return fatal(
                path, "expected a 'key: value' line in the YAML header",
                result);
          }
          std::string_view key = raw.substr(0, colon);
          while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.remove_suffix(1);
          }
          if (key == "columns") {
            return fatal(
                path,
                "unsupported header key 'columns': column redefinition is not "
                "supported in v1",
                result);
          }
          if (key == "import_tables") {
            return fatal(
                path,
                "unsupported header key 'import_tables': this file references "
                "other tables; import the referenced table files directly",
                result);
          }
          if (key == "vocabulary") {
            return fatal(
                path,
                "unsupported header key 'vocabulary': external vocabulary "
                "references are not supported in v1",
                result);
          }
          // 其余键（name/version/sort/author/...）信息性：忽略并登记。
          ++report->counters.header_keys_ignored;
          ignored_keys.push_back(std::string(key));
        }
        break;
      }
      case ParserState::kTable: {
        if (is_comment) {
          ++report->counters.comment_lines;
          break;
        }
        const std::string_view t = trimmedRight(raw);
        if (t == "---" || t == "...") {
          return fatal(path,
                       std::string("YAML document marker '") +
                           (t == "---" ? "---" : "...") +
                           "' inside the table",
                       result);
        }
        if (!handleTableRow(raw)) return false;
        break;
      }
    }

    if (eol == bytes.size()) break;
    pos = eol + 1;
  }

  if (state == ParserState::kHeader) {
    return fatal(path, "YAML header is missing the '...' table start marker",
                 result);
  }

  sortUnique(&report->ignored_header_keys);
  return true;
}

// ---------------------------------------------------------------------------
// 去重 + 排序 + 序列化 + 自检
// ---------------------------------------------------------------------------

bool dedupAndSort(std::vector<RawRow>* rows, ImportReport* report) {
  // 1) 去重排序键：(code, text) 升序；组内 weight 降序、source 升序、
  //    导入顺序升序 —— 组首即契约 2.3 的保留者。
  std::sort(rows->begin(), rows->end(),
            [](const RawRow& a, const RawRow& b) {
              const int c = compareBytes(a.code, b.code);
              if (c != 0) return c < 0;
              const int t = compareBytes(a.text, b.text);
              if (t != 0) return t < 0;
              if (a.weight != b.weight) return a.weight > b.weight;
              const int s = compareBytes(a.source, b.source);
              if (s != 0) return s < 0;
              return a.seq < b.seq;
            });
  std::vector<RawRow> unique;
  unique.reserve(rows->size());
  for (std::size_t i = 0; i < rows->size();) {
    std::size_t j = i + 1;
    while (j < rows->size() && rows->at(j).code == rows->at(i).code &&
           rows->at(j).text == rows->at(i).text) {
      ++j;
    }
    unique.push_back(std::move(rows->at(i)));
    report->counters.duplicates_removed += (j - i) - 1;
    i = j;
  }

  // 2) 存储排序（契约 3.1）：code 升序；weight 降序；text 升序。
  //    去重后 (code, text) 唯一，故该序为全序。
  std::sort(unique.begin(), unique.end(),
            [](const RawRow& a, const RawRow& b) {
              const int c = compareBytes(a.code, b.code);
              if (c != 0) return c < 0;
              if (a.weight != b.weight) return a.weight > b.weight;
              return compareBytes(a.text, b.text) < 0;
            });
  rows->swap(unique);
  return true;
}

// 序列化 + 输出自检（index-format.md §6：CRC、顺序、stableId、记录数与
// 头部一致；任一失败为导入器缺陷）。
bool serialize(const std::vector<RawRow>& rows, ImportReport* report,
               std::vector<unsigned char>* out, std::string* err) {
  out->clear();
  out->reserve(kHeaderSize + rows.size() * 32);

  // 头部骨架（CRC 占位，稍后回填）。
  for (const unsigned char c : kIndexMagic) out->push_back(c);
  appendU32(out, kIndexFormatVersion);
  appendU64(out, rows.size());  // recordCount 占位（自检后确认一致）
  appendU64(out, 0);            // codeCount 占位
  appendU32(out, 0);            // crc32 占位
  appendU32(out, 0);            // flags
  appendU32(out, 0);            // reserved0
  appendU64(out, 0);            // reserved1
  if (out->size() != kHeaderSize) {
    *err = "self-check failed: header size mismatch";
    return false;
  }

  // 记录区。
  for (const RawRow& r : rows) {
    if (r.code.empty() || r.code.size() > kIndexMaxCodeBytes ||
        r.text.empty() || r.text.size() > kIndexMaxTextBytes ||
        r.source.empty() || r.source.size() > kIndexMaxSourceBytes) {
      *err = "self-check failed: field length out of contract bounds";
      return false;
    }
    appendU16(out, static_cast<std::uint16_t>(r.code.size()));
    out->insert(out->end(), r.code.begin(), r.code.end());
    appendU16(out, static_cast<std::uint16_t>(r.text.size()));
    out->insert(out->end(), r.text.begin(), r.text.end());
    std::uint64_t bits = 0;
    const double w = r.weight;
    std::memcpy(&bits, &w, sizeof(bits));
    appendU64(out, bits);
    out->push_back(static_cast<unsigned char>(r.source.size()));
    out->insert(out->end(), r.source.begin(), r.source.end());
    appendU64(out, computeStableId(r.code, r.text, r.source));
  }

  // 自检 1：存储顺序（与 A3 loader 相同的谓词）+ codeCount。
  std::uint64_t distinct_codes = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i == 0) {
      ++distinct_codes;
    } else {
      const RawRow& prev = rows[i - 1];
      const RawRow& cur = rows[i];
      const int c = compareBytes(prev.code, cur.code);
      if (c > 0) {
        *err = "self-check failed: code out of order at record " +
               std::to_string(i);
        return false;
      }
      if (c == 0) {
        if (prev.weight < cur.weight) {
          *err = "self-check failed: weight out of order at record " +
                 std::to_string(i);
          return false;
        }
        if (prev.weight == cur.weight &&
            compareBytes(prev.text, cur.text) >= 0) {
          *err = "self-check failed: text not strictly ascending at record " +
                 std::to_string(i);
          return false;
        }
      } else {
        ++distinct_codes;
      }
    }
  }

  // 自检 2：逐条字段约束 + stableId。
  for (const RawRow& r : rows) {
    if (!std::isfinite(r.weight)) {
      *err = "self-check failed: non-finite weight";
      return false;
    }
    if (containsControl(r.code) || containsControl(r.text) ||
        containsControl(r.source) || !codeCharsValid(r.code)) {
      *err = "self-check failed: forbidden byte in string field";
      return false;
    }
    if (!isValidUtf8(r.text) || !isValidUtf8(r.source)) {
      *err = "self-check failed: invalid UTF-8 in string field";
      return false;
    }
  }

  // 头部回填：codeCount、CRC（对 [0x20, EOF)）。
  for (int i = 0; i < 8; ++i) {
    (*out)[kOffCodeCount + static_cast<std::size_t>(i)] =
        static_cast<unsigned char>((distinct_codes >> (8 * i)) & 0xFFu);
  }
  const std::uint32_t crc =
      crc32(out->data() + kCrcBegin, out->size() - kCrcBegin);
  (*out)[kOffCrc32] = static_cast<unsigned char>(crc & 0xFFu);
  (*out)[kOffCrc32 + 1] = static_cast<unsigned char>((crc >> 8) & 0xFFu);
  (*out)[kOffCrc32 + 2] = static_cast<unsigned char>((crc >> 16) & 0xFFu);
  (*out)[kOffCrc32 + 3] = static_cast<unsigned char>((crc >> 24) & 0xFFu);

  // 自检 3：CRC 复算、recordCount/codeCount 与头部一致。
  if (crc32(out->data() + kCrcBegin, out->size() - kCrcBegin) != crc) {
    *err = "self-check failed: crc mismatch after header patch";
    return false;
  }
  report->counters.records = rows.size();
  report->counters.codes = distinct_codes;
  report->crc32 = crc;
  report->index_bytes = out->size();
  return true;
}

// ---------------------------------------------------------------------------
// 文件写出
// ---------------------------------------------------------------------------

bool writeIndexFile(const std::string& path,
                    const std::vector<unsigned char>& bytes,
                    ImportResult* result) {
  // 不覆盖既有文件（index-format.md §7 精神；亦覆盖 input == output 的情形）。
  if (std::FILE* probe = std::fopen(path.c_str(), "rb")) {
    std::fclose(probe);
    return fatal(path,
                 "output file already exists (the importer never overwrites)",
                 result);
  }
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    return fatal(path, "cannot open output: " + errnoText(), result);
  }
  const std::size_t written =
      bytes.empty() ? 0u : std::fwrite(bytes.data(), 1, bytes.size(), f);
  const bool write_error = std::ferror(f) != 0;
  if (std::fclose(f) != 0 || write_error || written != bytes.size()) {
    std::remove(path.c_str());  // 不留下残缺索引
    return fatal(path,
                 "short write: wrote " + std::to_string(written) + " of " +
                     std::to_string(bytes.size()) + " bytes",
                 result);
  }
  // 回读校验长度。
  if (std::FILE* check = std::fopen(path.c_str(), "rb")) {
    bool ok = std::fseek(check, 0, SEEK_END) == 0;
    if (ok) {
      const long size_l = std::ftell(check);
      ok = size_l >= 0 &&
           static_cast<std::uint64_t>(size_l) ==
               static_cast<std::uint64_t>(bytes.size());
    }
    std::fclose(check);
    if (ok) {
      return true;
    }
  }
  std::remove(path.c_str());
  return fatal(path, "written file verification failed (size mismatch)",
               result);
}

std::string basenameOf(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool validateSourceLabel(const std::string& source, std::string* err) {
  if (source.empty() || source.size() > kIndexMaxSourceBytes) {
    *err = "source label is empty or exceeds 64 bytes";
    return false;
  }
  if (containsControl(source)) {
    *err = "source label contains a control character";
    return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 公共 API
// ---------------------------------------------------------------------------

ImportResult compileIndexToBuffer(const std::string& input_path,
                                  const ImportOptions& options,
                                  std::vector<unsigned char>* out_index) {
  ImportResult result;
  ImportReport& report = result.report;
  report.input_path = input_path;

  std::string source = options.source.empty() ? basenameOf(input_path)
                                              : options.source;
  std::string err;
  if (!validateSourceLabel(source, &err)) {
    fatal(input_path, err, &result);
    return result;
  }
  report.source = source;

  std::vector<RawRow> rows;
  if (!parseInput(input_path, source, &report, &rows, &result)) {
    return result;
  }
  if (!dedupAndSort(&rows, &report)) {
    fatal(input_path, "internal dedup failure", &result);
    return result;
  }
  if (!serialize(rows, &report, out_index, &err)) {
    out_index->clear();  // 契约：失败时 out_index 不被赋值
    result.exit_code = ExitCode::kSelfCheckFailed;
    result.error = input_path + ": " + err;
    return result;
  }

  result.exit_code = report.counters.skipped_rows == 0 ? ExitCode::kOk
                                                       : ExitCode::kSkippedLines;
  return result;
}

ImportResult compileIndexFile(const std::string& input_path,
                              const std::string& output_path,
                              const ImportOptions& options) {
  std::vector<unsigned char> index;
  ImportResult result = compileIndexToBuffer(input_path, options, &index);
  if (result.exit_code != ExitCode::kOk &&
      result.exit_code != ExitCode::kSkippedLines) {
    return result;  // 致命/自检失败：未写出任何文件
  }
  result.report.output_path = output_path;
  if (!writeIndexFile(output_path, index, &result)) {
    result.report.output_path.clear();  // 未成功写出
    return result;
  }
  return result;
}

}  // namespace dict_import
}  // namespace abc
