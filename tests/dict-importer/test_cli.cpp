// A2 测试：CLI 端到端（--help/--version/退出码/报告文件/不覆盖既有输出）。
// 需要 abc_dict_import 可执行路径（argv[2] 或 CTest 注入）；缺省时跳过本文件
// 用例（库级行为已由 test_parse/test_compile/test_cross 覆盖）。

#include <string>

#include "dict_importer.hpp"
#include "test_util.hpp"

namespace {

using abc::dict_import::ExitCode;

std::string importerExe() { return abc_test::runConfig().importer_exe; }

// 运行 CLI 并返回退出码；stdout/stderr 合并写入 log 文件。
int runCli(const std::string& args, const std::string& log) {
  return abc_test::runCommand(
      abc_test::redirected(importerExe(), args, log));
}

}  // namespace

ABC_TEST(CliHelpExitZero) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string log = dir.file("help.log");
  EXPECT_EQ(runCli("--help", log), 0);
  const std::string out = abc_test::readFileBytes(log);
  EXPECT_TRUE(abc_test::contains(out, "--input"));
  EXPECT_TRUE(abc_test::contains(out, "--output"));
  EXPECT_TRUE(abc_test::contains(out, "--source"));
  EXPECT_TRUE(abc_test::contains(out, "\xE4\xB8\x8D\xE8\xA6\x86\xE7\x9B\x96\xE6\x97\xA2\xE6\x9C\x89\xE6\x96\x87\xE4\xBB\xB6"));  // “不覆盖既有文件”
}

ABC_TEST(CliVersionExitZero) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string log = dir.file("version.log");
  EXPECT_EQ(runCli("--version", log), 0);
  const std::string out = abc_test::readFileBytes(log);
  EXPECT_TRUE(abc_test::contains(out, "index format: 1.0"));
  EXPECT_TRUE(abc_test::contains(out, "interface: 1.0.0"));
}

ABC_TEST(CliImportMinimalExitZero) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string out = dir.file("out.ix");
  const std::string report = dir.file("report.txt");
  const std::string log = dir.file("run.log");
  const std::string args = "--input " +
                           abc_test::quoted(abc_test::fixtureFile("minimal.dict.yaml")) +
                           " --output " + abc_test::quoted(out) +
                           " --source cn_dicts/base --report " +
                           abc_test::quoted(report);
  EXPECT_EQ(runCli(args, log), 0);
  EXPECT_TRUE(abc_test::fileExists(out));
  const std::string rep = abc_test::readFileBytes(report);
  EXPECT_TRUE(abc_test::contains(rep, "records: written=8 codes=6"));
  EXPECT_TRUE(abc_test::contains(rep, "skipped-lines: 0"));
  EXPECT_TRUE(abc_test::contains(rep, "source: cn_dicts/base"));
  EXPECT_TRUE(abc_test::contains(rep, "exit: ok"));
}

ABC_TEST(CliDeterministicOutputBytes) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string in =
      abc_test::quoted(abc_test::fixtureFile("minimal.dict.yaml"));
  const std::string a = dir.file("a.ix");
  const std::string b = dir.file("b.ix");
  const std::string log = dir.file("run.log");
  EXPECT_EQ(runCli("--input " + in + " --output " + abc_test::quoted(a) +
                       " --source same",
                   log),
            0);
  EXPECT_EQ(runCli("--input " + in + " --output " + abc_test::quoted(b) +
                       " --source same",
                   log),
            0);
  EXPECT_EQ(abc_test::readFileBytes(a), abc_test::readFileBytes(b));
}

ABC_TEST(CliNeverOverwritesExistingOutput) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string out = dir.file("existing.ix");
  abc_test::writeFileBytes(out, "KEEP");
  const std::string log = dir.file("run.log");
  const std::string args = "--input " +
                           abc_test::quoted(abc_test::fixtureFile("minimal.dict.yaml")) +
                           " --output " + abc_test::quoted(out);
  EXPECT_EQ(runCli(args, log), 1);
  EXPECT_EQ(abc_test::readFileBytes(out), "KEEP");  // 原样保留
  const std::string err = abc_test::readFileBytes(log);
  EXPECT_TRUE(abc_test::contains(err, "already exists"));
}

ABC_TEST(CliBadLinesExitTwoWithLocator) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string out = dir.file("bad.ix");
  const std::string report = dir.file("report.txt");
  const std::string log = dir.file("run.log");
  const std::string args = "--input " +
                           abc_test::quoted(abc_test::fixtureFile("bad_lines.dict.yaml")) +
                           " --output " + abc_test::quoted(out) +
                           " --report " + abc_test::quoted(report);
  EXPECT_EQ(runCli(args, log), 2);
  EXPECT_TRUE(abc_test::fileExists(out));  // 索引仍应写出
  const std::string rep = abc_test::readFileBytes(report);
  EXPECT_TRUE(abc_test::contains(rep, "skipped-lines: 9"));
  EXPECT_TRUE(abc_test::contains(rep, "line 13: text exceeds 512 bytes"));
  EXPECT_TRUE(abc_test::contains(rep, "exit: ok-with-skipped-lines"));
}

ABC_TEST(CliFatalWritesNoIndex) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string out = dir.file("never.ix");
  const std::string log = dir.file("run.log");
  const std::string args = "--input " +
                           abc_test::quoted(abc_test::fixtureFile("import_tables.dict.yaml")) +
                           " --output " + abc_test::quoted(out);
  EXPECT_EQ(runCli(args, log), 1);
  EXPECT_FALSE(abc_test::fileExists(out));  // 致命错误不写出任何文件
  const std::string err = abc_test::readFileBytes(log);
  EXPECT_TRUE(abc_test::contains(err, "'import_tables'"));
}

ABC_TEST(CliUtf16FatalExitOne) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  std::string content;
  content += '\xFF';
  content += '\xFE';
  const char16_t units[] = {0x5B57, 0x0009, 0x007A, 0x0069, 0x000A};
  for (const char16_t u : units) {
    content += static_cast<char>(u & 0xFF);
    content += static_cast<char>(u >> 8);
  }
  const std::string in = dir.file("utf16.dict.yaml");
  abc_test::writeFileBytes(in, content);
  const std::string out = dir.file("out.ix");
  const std::string log = dir.file("run.log");
  const std::string args =
      "--input " + abc_test::quoted(in) + " --output " + abc_test::quoted(out);
  EXPECT_EQ(runCli(args, log), 1);
  EXPECT_FALSE(abc_test::fileExists(out));
}

ABC_TEST(CliUsageErrorsExitOne) {
  if (importerExe().empty()) ABC_SKIP("no abc_dict_import path provided");
  abc_test::TempDir dir;
  const std::string log = dir.file("usage.log");
  EXPECT_EQ(runCli("", log), 1);
  EXPECT_TRUE(abc_test::contains(abc_test::readFileBytes(log), "--input"));
  const std::string log2 = dir.file("usage2.log");
  EXPECT_EQ(runCli("--bogus", log2), 1);
  EXPECT_TRUE(abc_test::contains(abc_test::readFileBytes(log2), "unknown option"));
}
