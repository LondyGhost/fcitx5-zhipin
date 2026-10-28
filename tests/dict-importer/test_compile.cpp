// A2 测试：去重、存储排序、序列化结构 golden、确定性、权重编码、边界。
// 结构校验用手写小端读取（独立于导入器实现）；CRC 用按位独立实现交叉校验；
// stableId 用 A0 冻结的 abc::computeStableId 复核。

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dict_importer.hpp"
#include "abc/dictionary.h"  // computeStableId（契约 2.2 冻结实现）
#include "abc/index_format.h"
#include "test_util.hpp"

namespace {

using abc::computeStableId;
using abc::dict_import::ExitCode;
using abc::dict_import::ImportOptions;
using abc::dict_import::ImportResult;

ImportResult compileFixture(const std::string& fixture_name,
                            const std::string& source,
                            std::vector<unsigned char>* out_index) {
  ImportOptions options;
  options.source = source;
  return abc::dict_import::compileIndexToBuffer(
      abc_test::fixtureFile(fixture_name), options, out_index);
}

ImportResult compileSynthetic(const std::string& content,
                              const std::string& source,
                              std::vector<unsigned char>* out_index) {
  abc_test::TempDir dir;
  const std::string in = dir.file("input.dict.yaml");
  abc_test::writeFileBytes(in, content);
  ImportOptions options;
  options.source = source;
  return abc::dict_import::compileIndexToBuffer(in, options, out_index);
}

// golden 预期：minimal.dict.yaml 去重后的 8 条记录及其冻结存储顺序
// （code 升序 → weight 降序 → text 升序；契约 3.1）。
struct ExpectedRecord {
  const char* code;
  const char* text;
  double weight;
};
const ExpectedRecord kGoldenRecords[8] = {
    {"ceshi", "\xE6\xB5\x8B\xE8\xAF\x95", 55.5},   // 测试
    {"ma", "\xE9\xA9\xAC", 70.0},                  // 马
    {"ma", "\xE5\xA6\x88", 65.0},                  // 妈
    {"ma", "\xE9\xAA\x82", 60.0},                  // 骂
    {"mama", "\xE5\xA6\x88\xE5\xA6\x88", 100.0},   // 妈妈
    {"ne", "\xE5\x91\xA2", 10.0},                  // 呢
    {"nihao", "\xE4\xBD\xA0\xE5\xA5\xBD", 100.0},  // 你好
    {"shijie", "\xE4\xB8\x96\xE7\x95\x8C", 0.0},   // 世界
};

// 记录帧解析（独立手写；含 weight 原始位模式供确定性断言）。
struct ParsedRecord {
  std::string code;
  std::string text;
  double weight;
  std::uint64_t weight_bits = 0;
  std::string source;
  std::uint64_t stableId = 0;
};

bool parseRecords(const std::string& bytes, std::size_t record_count,
                  std::vector<ParsedRecord>* out) {
  std::size_t off = 48;
  for (std::size_t i = 0; i < record_count; ++i) {
    if (off + 2 > bytes.size()) return false;
    const std::uint16_t code_len = abc_test::leU16(bytes, off);
    off += 2;
    if (off + code_len > bytes.size()) return false;
    ParsedRecord rec;
    rec.code = bytes.substr(off, code_len);
    off += code_len;
    if (off + 2 > bytes.size()) return false;
    const std::uint16_t text_len = abc_test::leU16(bytes, off);
    off += 2;
    if (off + text_len > bytes.size()) return false;
    rec.text = bytes.substr(off, text_len);
    off += text_len;
    if (off + 8 > bytes.size()) return false;
    rec.weight = abc_test::leF64(bytes, off);
    rec.weight_bits = abc_test::leU64(bytes, off);
    off += 8;
    if (off + 1 > bytes.size()) return false;
    const std::uint8_t source_len =
        static_cast<std::uint8_t>(static_cast<unsigned char>(bytes[off]));
    off += 1;
    if (off + source_len > bytes.size()) return false;
    rec.source = bytes.substr(off, source_len);
    off += source_len;
    if (off + 8 > bytes.size()) return false;
    rec.stableId = abc_test::leU64(bytes, off);
    off += 8;
    out->push_back(std::move(rec));
  }
  return off == bytes.size();  // 记录区必须恰好消耗到 EOF
}

}  // namespace

ABC_TEST(StableIdContractVectors) {
  // dictionary.md 2.2 的已知向量（A0 冻结实现，冒烟测试同样断言）。
  EXPECT_EQ(computeStableId("ni", "\xE4\xBD\xA0", "base"),
            0xdab5c10fa400390eULL);
  EXPECT_EQ(computeStableId("nihao", "\xE4\xBD\xA0\xE5\xA5\xBD",
                            "cn_dicts/base"),
            0xe8f6edb01cdcb459ULL);
}

ABC_TEST(GoldenStructureMinimal) {
  // minimal.dict.yaml + source "golden" 的精确预期索引：
  // 记录帧 = 2+codeLen + 2+textLen + 8 + 1+6(source "golden") + 8 = 27+长度；
  // 8 条帧长 = 38+32+32+32+37+32+38+39 = 280（shijie 6 字节）；索引 = 48+280 = 328。
  std::vector<unsigned char> buf;
  const ImportResult r = compileFixture("minimal.dict.yaml", "golden", &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  const std::string index(buf.begin(), buf.end());

  EXPECT_EQ(index.size(), 328u);
  EXPECT_TRUE(std::memcmp(index.data(), "ABCDIX1\0", 8) == 0);
  EXPECT_EQ(abc_test::leU32(index, 0x08), 0x00010000u);  // 索引 v1.0
  EXPECT_EQ(abc_test::leU64(index, 0x0C), 8u);           // recordCount
  EXPECT_EQ(abc_test::leU64(index, 0x14), 6u);           // codeCount
  EXPECT_EQ(abc_test::leU32(index, 0x20), 0u);           // flags
  EXPECT_EQ(abc_test::leU32(index, 0x24), 0u);           // reserved0
  EXPECT_EQ(abc_test::leU64(index, 0x28), 0u);           // reserved1
  const std::uint32_t crc = abc_test::crc32Independent(
      reinterpret_cast<const unsigned char*>(index.data()) + 0x20,
      index.size() - 0x20);
  EXPECT_EQ(abc_test::leU32(index, 0x1C), crc);
  EXPECT_EQ(r.report.crc32, crc);

  std::vector<ParsedRecord> records;
  EXPECT_TRUE(parseRecords(index, 8, &records));
  for (std::size_t i = 0; i < 8 && i < records.size(); ++i) {
    EXPECT_EQ(records[i].code, kGoldenRecords[i].code);
    EXPECT_EQ(records[i].text, kGoldenRecords[i].text);
    EXPECT_TRUE(records[i].weight == kGoldenRecords[i].weight);
    EXPECT_EQ(records[i].source, "golden");
    EXPECT_EQ(records[i].stableId,
              computeStableId(records[i].code, records[i].text,
                              records[i].source));
  }
}

ABC_TEST(DeterminismTwoRunsByteIdentical) {
  std::vector<unsigned char> a;
  std::vector<unsigned char> b;
  const ImportResult ra =
      compileFixture("minimal.dict.yaml", "det", &a);
  const ImportResult rb =
      compileFixture("minimal.dict.yaml", "det", &b);
  EXPECT_TRUE(ra.exit_code == ExitCode::kOk);
  EXPECT_TRUE(rb.exit_code == ExitCode::kOk);
  EXPECT_EQ(a.size(), b.size());
  EXPECT_TRUE(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

ABC_TEST(DeterminismAcrossBomAndCrlfVariants) {
  // 同一逻辑内容：LF 无 BOM 与 BOM + CRLF 两个物理变体必须产出相同索引
  // （source 显式固定，避免基名差异）。
  const std::string body = "\xE5\xAD\x97\tzi\t7\n\xE8\xAF\x8D\tci\n";
  const std::string variant =
      std::string("\xEF\xBB\xBF", 3) + "\xE5\xAD\x97\tzi\t7\r\n\xE8\xAF\x8D\tci\r\n";
  std::vector<unsigned char> a;
  std::vector<unsigned char> b;
  const ImportResult r1 = compileSynthetic(body, "t", &a);
  const ImportResult r2 = compileSynthetic(variant, "t", &b);
  EXPECT_TRUE(r1.exit_code == ExitCode::kOk);
  EXPECT_TRUE(r2.exit_code == ExitCode::kOk);
  EXPECT_TRUE(r2.report.bom_stripped);
  EXPECT_EQ(a.size(), b.size());
  EXPECT_TRUE(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

ABC_TEST(DedupKeepsHighestWeightThenEarliest) {
  // 同 (code, text)：权重最高者保留；权重相同保留导入顺序最早者（契约 2.3）。
  std::vector<unsigned char> buf;
  const ImportResult r = compileSynthetic(
      "\xE5\xAD\x97\tzi\t10\n\xE5\xAD\x97\tzi\t30\n\xE5\xAD\x97\tzi\t20\n"
      "\xE8\xAF\x8D\tci\t5\n\xE8\xAF\x8D\tci\t5\n",
      "t", &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.duplicates_removed, 3u);
  EXPECT_EQ(r.report.counters.records, 2u);
  std::vector<ParsedRecord> records;
  EXPECT_TRUE(parseRecords(std::string(buf.begin(), buf.end()), 2, &records));
  EXPECT_EQ(records[0].code, "ci");   // "ci" < "zi"
  EXPECT_TRUE(records[0].weight == 5.0);
  EXPECT_EQ(records[1].code, "zi");
  EXPECT_TRUE(records[1].weight == 30.0);  // 保留最高权重，而非最早行
}

ABC_TEST(WeightGrammarAndEncoding) {
  // 合法：符号/小数/指数；非法：前导点/尾随点/溢出；-0 归一化为 +0 位模式。
  std::vector<unsigned char> buf;
  const ImportResult r = compileSynthetic(
      "A\twa\t+5\n"
      "B\twb\t-2.5\n"
      "C\twc\t1e2\n"
      "D\twd\t1E+2\n"
      "E\twe\t.5\n"      // 非法：前导点
      "F\twf\t5.\n"      // 非法：尾随点
      "G\twg\t-0\n"      // -0.0 → +0.0（位模式 0x0）
      "H\twh\t1e999\n",  // 溢出 → 非有限
      "t", &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  EXPECT_EQ(r.report.counters.skipped_rows, 3u);
  EXPECT_EQ(r.report.counters.records, 5u);  // A/B/C/D 合法 + G（-0 归一化）
  std::vector<ParsedRecord> records;
  EXPECT_TRUE(parseRecords(std::string(buf.begin(), buf.end()), 5, &records));
  EXPECT_EQ(records[0].code, "wa");
  EXPECT_TRUE(records[0].weight == 5.0);
  EXPECT_EQ(records[1].code, "wb");
  EXPECT_TRUE(records[1].weight == -2.5);
  EXPECT_EQ(records[2].code, "wc");
  EXPECT_TRUE(records[2].weight == 100.0);
  EXPECT_EQ(records[3].code, "wd");
  EXPECT_TRUE(records[3].weight == 100.0);
  EXPECT_EQ(records[4].code, "wg");
  EXPECT_TRUE(records[4].weight == 0.0);
  // 跳过原因核对。
  EXPECT_EQ(r.report.issues[0].reason, "invalid weight syntax");      // .5
  EXPECT_EQ(r.report.issues[1].reason, "invalid weight syntax");      // 5.
  EXPECT_EQ(r.report.issues[2].reason, "weight is not finite");       // 1e999

  // -0 单独验证位模式。
  std::vector<unsigned char> buf2;
  const ImportResult z = compileSynthetic("Z\tzero\t-0\n", "t", &buf2);
  EXPECT_TRUE(z.exit_code == ExitCode::kOk);
  std::vector<ParsedRecord> rec2;
  EXPECT_TRUE(parseRecords(std::string(buf2.begin(), buf2.end()), 1, &rec2));
  EXPECT_EQ(rec2[0].code, "zero");
  EXPECT_TRUE(rec2[0].weight == 0.0);
  EXPECT_EQ(rec2[0].weight_bits, 0u);  // +0.0 的 IEEE-754 位模式
}

ABC_TEST(BoundsAcceptedAtExactLimits) {
  std::vector<unsigned char> buf;
  const ImportResult r = compileFixture("bounds_ok.dict.yaml", "b", &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.records, 2u);
  EXPECT_EQ(r.report.counters.codes, 2u);
  std::vector<ParsedRecord> records;
  EXPECT_TRUE(parseRecords(std::string(buf.begin(), buf.end()), 2, &records));
  EXPECT_EQ(records[0].code, std::string(128, 'a'));  // 恰好 128 字节
  EXPECT_EQ(records[0].text, std::string(512, 'x'));  // 恰好 512 字节
  EXPECT_EQ(records[1].code, "duan");
}

ABC_TEST(EmptyIndexStructure) {
  std::vector<unsigned char> buf;
  const ImportResult r = compileFixture("empty.dict.yaml", "e", &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.records, 0u);
  EXPECT_EQ(r.report.counters.codes, 0u);
  EXPECT_EQ(buf.size(), 48u);
  // 头部 CRC 覆盖 [0x20, 48) 的 16 个零字节。
  const std::uint32_t crc = abc_test::crc32Independent(
      reinterpret_cast<const unsigned char*>(buf.data()) + 0x20, 16);
  EXPECT_EQ(abc_test::leU32(std::string(buf.begin(), buf.end()), 0x1C), crc);
}
