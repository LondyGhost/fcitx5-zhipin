// A2 测试：输入解析（头部/表体/注释/编码/坏行诊断/致命结构错误）。
// 全部经 abc::dict_import::compileIndexToBuffer 触达真实解析路径；
// 字节级输入（BOM/CRLF/UTF-16/非法 UTF-8）在临时目录合成。

#include <string>
#include <vector>

#include "dict_importer.hpp"
#include "test_util.hpp"

namespace {

using abc::dict_import::ExitCode;
using abc::dict_import::ImportOptions;
using abc::dict_import::ImportResult;

// 合成输入并编译，返回结果（不写输出文件）。
ImportResult compileSynthetic(const std::string& content,
                              const std::string& source = "") {
  abc_test::TempDir dir;
  const std::string in = dir.file("input.dict.yaml");
  abc_test::writeFileBytes(in, content);
  ImportOptions options;
  options.source = source;
  std::vector<unsigned char> index;
  return abc::dict_import::compileIndexToBuffer(in, options, &index);
}

ImportResult compileFixture(const std::string& fixture_name,
                            const std::string& source = "") {
  abc_test::TempDir dir;
  ImportOptions options;
  options.source = source;
  std::vector<unsigned char> index;
  return abc::dict_import::compileIndexToBuffer(
      abc_test::fixtureFile(fixture_name), options, &index);
}

}  // namespace

ABC_TEST(MinimalFixtureFullCounts) {
  const ImportResult r = compileFixture("minimal.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_TRUE(r.error.empty());
  const auto& c = r.report.counters;
  EXPECT_EQ(c.lines_total, 28u);
  EXPECT_EQ(c.blank_lines, 2u);
  EXPECT_EQ(c.comment_lines, 7u);
  EXPECT_EQ(c.header_keys_ignored, 4u);
  EXPECT_EQ(c.table_rows, 13u);
  EXPECT_EQ(c.valid_rows, 13u);
  EXPECT_EQ(c.skipped_rows, 0u);
  EXPECT_EQ(c.duplicates_removed, 5u);
  EXPECT_EQ(c.records, 8u);
  EXPECT_EQ(c.codes, 6u);
  EXPECT_FALSE(r.report.bom_stripped);
  // 忽略键按字节序去重排列。
  const std::vector<std::string> expected_keys = {"author", "name", "sort",
                                                  "version"};
  EXPECT_EQ(r.report.ignored_header_keys.size(), expected_keys.size());
  for (std::size_t i = 0; i < expected_keys.size() && i < 4; ++i) {
    EXPECT_EQ(r.report.ignored_header_keys[i], expected_keys[i]);
  }
  // 缺省 source = 输入文件基名。
  EXPECT_EQ(r.report.source, "minimal.dict.yaml");
}

ABC_TEST(ExplicitSourceLabel) {
  const ImportResult r = compileFixture("minimal.dict.yaml", "cn_dicts/base");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.source, "cn_dicts/base");
}

ABC_TEST(SourceLabelTooLongIsFatal) {
  std::string label(65, 'x');
  const ImportResult r = compileFixture("minimal.dict.yaml", label);
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "source label"));
}

ABC_TEST(SourceLabelControlIsFatal) {
  ImportResult r = compileFixture("minimal.dict.yaml", std::string("a\tb"));
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "source label"));
  r = compileFixture("minimal.dict.yaml", std::string("a\nb"));
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
}

ABC_TEST(PlainTableWithoutHeader) {
  const ImportResult r = compileFixture("no_header.txt");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.records, 2u);
  EXPECT_EQ(r.report.counters.codes, 2u);
  EXPECT_EQ(r.report.counters.comment_lines, 1u);
  EXPECT_EQ(r.report.counters.blank_lines, 1u);
  EXPECT_EQ(r.report.source, "no_header.txt");
}

ABC_TEST(BomStrippedAndCounted) {
  // BOM + CRLF 合成输入。
  const std::string content =
      std::string("\xEF\xBB\xBF", 3) + "\xE5\xAD\x97\tzi\r\n\xE8\xAF\x8D\tci\r\n";
  const ImportResult r = compileSynthetic(content);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_TRUE(r.report.bom_stripped);
  EXPECT_EQ(r.report.counters.records, 2u);
  EXPECT_EQ(r.report.counters.lines_total, 2u);
}

ABC_TEST(Utf16RejectedAsFatal) {
  // UTF-16LE BOM + "字\tzi\n" 的 UTF-16 编码字节。
  std::string content;
  content += '\xFF';
  content += '\xFE';
  const char16_t units[] = {0x5B57, 0x0009, 0x007A, 0x0069, 0x000A};
  for (const char16_t u : units) {
    content += static_cast<char>(u & 0xFF);
    content += static_cast<char>(u >> 8);
  }
  const ImportResult r = compileSynthetic(content);
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "UTF-16"));
}

ABC_TEST(InvalidUtf8TextIsSkippedWithLocator) {
  const ImportResult r =
      compileSynthetic("好\thao\nx\xFFy\tcode\n坏\thuai\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  EXPECT_EQ(r.report.counters.skipped_rows, 1u);
  EXPECT_EQ(r.report.counters.records, 2u);
  EXPECT_EQ(r.report.issues.size(), 1u);
  EXPECT_EQ(r.report.issues[0].line, 2u);
  EXPECT_EQ(r.report.issues[0].reason, "text is not valid UTF-8");
  EXPECT_TRUE(abc_test::contains(r.report.issues[0].excerpt, "\\xFF"));
}

ABC_TEST(ControlCharInTextIsSkipped) {
  const ImportResult r = compileSynthetic("a\x01b\tcode\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  EXPECT_EQ(r.report.issues[0].reason, "text contains a control character");
}

ABC_TEST(EmptyCodeAfterNormalizationIsSkipped) {
  const ImportResult r = compileSynthetic("字\t   \n");
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  EXPECT_EQ(r.report.issues[0].reason, "code is empty after normalization");
}

ABC_TEST(EmptyWeightFieldIsSkipped) {
  const ImportResult r = compileSynthetic("字\tzi\t\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  EXPECT_EQ(r.report.issues[0].reason, "empty weight field");
}

ABC_TEST(BadLinesFixtureExactDiagnostics) {
  const ImportResult r = compileFixture("bad_lines.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kSkippedLines);
  const auto& c = r.report.counters;
  EXPECT_EQ(c.table_rows, 13u);
  EXPECT_EQ(c.valid_rows, 4u);   // 行 5/15/16/17（去重前）；行 17 为合法重复行
  EXPECT_EQ(c.skipped_rows, 9u);
  EXPECT_EQ(c.duplicates_removed, 1u);
  EXPECT_EQ(c.records, 3u);
  EXPECT_EQ(c.codes, 3u);
  EXPECT_EQ(r.report.issues.size(), 9u);
  // 注：夹具不依赖行尾不可见空白；空权重/空编码场景由合成输入用例覆盖
  // （EmptyWeightFieldIsSkipped / EmptyCodeAfterNormalizationIsSkipped）。
  static const char* const reasons[9] = {
      "missing code column",                          // 行 6
      "too many fields (4th column not supported)",   // 行 7
      "invalid weight syntax",                        // 行 8
      "weight is not finite",                         // 行 9
      "missing code column",                          // 行 10
      "code contains an unsupported character",       // 行 11
      "code exceeds 128 bytes",                       // 行 12
      "text exceeds 512 bytes",                       // 行 13
      "empty text field",                             // 行 14
  };
  static const std::uint64_t lines[9] = {6, 7, 8, 9, 10, 11, 12, 13, 14};
  for (std::size_t i = 0; i < 9; ++i) {
    EXPECT_EQ(r.report.issues[i].line, lines[i]);
    EXPECT_EQ(r.report.issues[i].reason, reasons[i]);
  }
  // 超长行的摘录应截断并带省略号（原始行 > 48 字节）。
  EXPECT_TRUE(abc_test::contains(r.report.issues[7].excerpt, "..."));
}

ABC_TEST(MissingInputFileIsFatal) {
  abc_test::TempDir dir;
  ImportOptions options;
  std::vector<unsigned char> index;
  const ImportResult r = abc::dict_import::compileIndexToBuffer(
      dir.file("no_such_file.yaml"), options, &index);
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "cannot open input"));
}

ABC_TEST(ColumnsKeyIsFatal) {
  const ImportResult r = compileFixture("columns_key.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "'columns'"));
}

ABC_TEST(ImportTablesKeyIsFatal) {
  const ImportResult r = compileFixture("import_tables.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "'import_tables'"));
}

ABC_TEST(VocabularyKeyIsFatal) {
  const ImportResult r =
      compileSynthetic("---\nname: v\nvocabulary: essay\n...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "'vocabulary'"));
}

ABC_TEST(NestedYamlBlockIsFatal) {
  const ImportResult r =
      compileSynthetic("---\nname: n\nauthor:\n  - someone\n...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "nested YAML block"));
}

ABC_TEST(RepeatedDocumentStartIsFatal) {
  const ImportResult r = compileSynthetic("---\n---\n...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "repeated document start"));
}

ABC_TEST(MarkerInsideTableIsFatal) {
  ImportResult r = compileSynthetic("---\n...\n---\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "'---' inside the table"));
  r = compileSynthetic("---\n...\n...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "'...' inside the table"));
}

ABC_TEST(DocumentEndBeforeHeaderIsFatal) {
  const ImportResult r = compileSynthetic("...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "before any YAML header"));
}

ABC_TEST(MissingTableEndMarkerIsFatal) {
  const ImportResult r = compileFixture("no_table_end.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "missing the '...' table start"));
}

ABC_TEST(HeaderLineWithoutColonIsFatal) {
  const ImportResult r = compileSynthetic("---\nbroken\n...\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "expected a 'key: value' line"));
}

ABC_TEST(EmptyFixtureProducesEmptyIndex) {
  const ImportResult r = compileFixture("empty.dict.yaml");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.records, 0u);
  EXPECT_EQ(r.report.counters.codes, 0u);
  EXPECT_EQ(r.report.index_bytes, 48u);
}

ABC_TEST(WhitespaceOnlyLinesAreBlank) {
  const ImportResult r = compileSynthetic("# c\n   \n\t\n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.blank_lines, 2u);
  EXPECT_EQ(r.report.counters.comment_lines, 1u);
  EXPECT_EQ(r.report.counters.records, 1u);
}

ABC_TEST(MarkerTrailingWhitespaceTolerated) {
  const ImportResult r = compileSynthetic("---   \nname: m\n...   \n字\tzi\n");
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  EXPECT_EQ(r.report.counters.records, 1u);
}

ABC_TEST(OverwriteIsRefusedByFileApi) {
  abc_test::TempDir dir;
  const std::string in = dir.file("in.yaml");
  const std::string out = dir.file("out.ix");
  abc_test::writeFileBytes(in, "字\tzi\n");
  abc_test::writeFileBytes(out, "KEEP");
  const ImportResult r =
      abc::dict_import::compileIndexFile(in, out, ImportOptions{});
  EXPECT_TRUE(r.exit_code == ExitCode::kFatal);
  EXPECT_TRUE(abc_test::contains(r.error, "already exists"));
  EXPECT_EQ(abc_test::readFileBytes(out), "KEEP");  // 未被覆盖
  EXPECT_TRUE(r.report.output_path.empty());        // 未成功写出
}
