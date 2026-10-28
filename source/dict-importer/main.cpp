// A2 — abc_dict_import 命令行入口。
//
// 用法：
//   abc_dict_import --input <词库文件> --output <索引文件> [--source <标签>]
//                   [--report <报告文件>] [--help] [--version]
//
// 退出码（docs/modules/rime-import.md §错误处理）：
//   0 成功且无跳过行；1 致命错误（未写出索引）；2 索引已写出但有跳过行；
//   3 输出自检失败（导入器缺陷）。
//
// 报告只写 stdout / 显式 --report 文件；不含时间戳、主机信息或凭据。
// 进程不得调用 setlocale：权重解析按 C 小数点并在库内整字段校验。

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "abc/index_format.h"
#include "abc/version.h"
#include "dict_importer.hpp"

namespace {

using abc::dict_import::ExitCode;
using abc::dict_import::ImportOptions;
using abc::dict_import::ImportResult;

const char kUsage[] =
    "abc_dict_import — Rime 词库 → 引擎索引 编译器（离线工具）\n"
    "\n"
    "用法:\n"
    "  abc_dict_import --input <词库文件> --output <索引文件>\n"
    "                  [--source <来源标签>] [--report <报告文件>]\n"
    "  abc_dict_import --help | --version\n"
    "\n"
    "参数:\n"
    "  --input PATH    输入的 Rime 词库文本（UTF-8，可含 BOM；显式路径）\n"
    "  --output PATH   输出的引擎索引（index-format.md v1.0；不覆盖既有文件）\n"
    "  --source LABEL  索引内 source 字段标签（缺省 = 输入文件基名；<= 64 字节）\n"
    "  --report PATH   将导入报告同时写入该文件（不覆盖既有文件）\n"
    "\n"
    "支持的输入（v1 冻结子集）:\n"
    "  - UTF-8 词库文本；可选 YAML 头（--- 与 ... 之间平铺 key: value）；\n"
    "  - 表体行: text \\t code [\\t weight]；'#' 注释；空行忽略；\n"
    "  - code 归一化: 去除空格分隔符并折叠大写（归一化后须为 a-z）；\n"
    "  - 权重: 十进制（可选符号/小数/指数），缺省 0.0；\n"
    "  - 不支持: 转义序列、columns/import_tables/vocabulary、Lua/Processor\n"
    "    （词库内容永远视为数据，绝不执行）。\n"
    "\n"
    "退出码: 0 成功（无跳过行）；1 致命错误（未写出索引）；\n"
    "        2 成功但有跳过行（报告含行号与原因）；3 输出自检失败。\n";

std::string exitCodeName(ExitCode code) {
  switch (code) {
    case ExitCode::kOk: return "ok";
    case ExitCode::kFatal: return "fatal";
    case ExitCode::kSkippedLines: return "ok-with-skipped-lines";
    case ExitCode::kSelfCheckFailed: return "self-check-failed";
  }
  return "unknown";
}

std::string formatReport(const ImportResult& r) {
  std::string out;
  out += "input: " + r.report.input_path + "\n";
  out += "output: " +
         (r.report.output_path.empty() ? std::string("(not written)")
                                       : r.report.output_path) +
         "\n";
  out += "source: " + r.report.source + "\n";
  out += "bom: ";
  out += r.report.bom_stripped ? "stripped" : "none";
  out += "\n";
  out += "header-keys-ignored:";
  if (r.report.ignored_header_keys.empty()) {
    out += " none";
  } else {
    for (const std::string& k : r.report.ignored_header_keys) {
      out += " " + k;
    }
  }
  out += "\n";
  char buf[512];
  const auto& c = r.report.counters;
  std::snprintf(buf, sizeof(buf),
                "lines: total=%llu blank=%llu comment=%llu "
                "header-keys-ignored=%llu\n",
                static_cast<unsigned long long>(c.lines_total),
                static_cast<unsigned long long>(c.blank_lines),
                static_cast<unsigned long long>(c.comment_lines),
                static_cast<unsigned long long>(c.header_keys_ignored));
  out += buf;
  std::snprintf(buf, sizeof(buf),
                "rows: table=%llu valid=%llu skipped=%llu duplicates=%llu\n",
                static_cast<unsigned long long>(c.table_rows),
                static_cast<unsigned long long>(c.valid_rows),
                static_cast<unsigned long long>(c.skipped_rows),
                static_cast<unsigned long long>(c.duplicates_removed));
  out += buf;
  std::snprintf(buf, sizeof(buf), "records: written=%llu codes=%llu\n",
                static_cast<unsigned long long>(c.records),
                static_cast<unsigned long long>(c.codes));
  out += buf;
  std::snprintf(buf, sizeof(buf), "index: bytes=%llu crc32=0x%08X\n",
                static_cast<unsigned long long>(r.report.index_bytes),
                r.report.crc32);
  out += buf;
  std::snprintf(buf, sizeof(buf), "skipped-lines: %llu\n",
                static_cast<unsigned long long>(r.report.issues.size()));
  out += buf;
  for (const auto& issue : r.report.issues) {
    std::snprintf(buf, sizeof(buf), "  line %llu: %s: %s\n",
                  static_cast<unsigned long long>(issue.line),
                  issue.reason.c_str(), issue.excerpt.c_str());
    out += buf;
  }
  out += std::string("exit: ") + exitCodeName(r.exit_code) + "\n";
  return out;
}

[[noreturn]] void usageError(const std::string& msg) {
  std::fprintf(stderr, "abc_dict_import: %s\n\n%s", msg.c_str(), kUsage);
  std::exit(1);
}

bool writeTextFile(const std::string& path, const std::string& text,
                   std::string* err) {
  if (std::FILE* probe = std::fopen(path.c_str(), "rb")) {
    std::fclose(probe);
    *err = "report file already exists (the importer never overwrites)";
    return false;
  }
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    *err = std::string("cannot open report file: ") + std::strerror(errno);
    return false;
  }
  const std::size_t written = std::fwrite(text.data(), 1, text.size(), f);
  const bool bad = std::ferror(f) != 0 || written != text.size();
  std::fclose(f);
  if (bad) {
    std::remove(path.c_str());
    *err = "short write on report file";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input;
  std::string output;
  std::string source;
  std::string report_path;
  bool have_input = false;
  bool have_output = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    }
    if (arg == "--version" || arg == "-V") {
      std::printf("abc_dict_import (linux-abc engine)\n"
                  "  index format: %u.%u (abc/index_format.h)\n"
                  "  interface: %s\n",
                  (abc::kIndexFormatVersion >> 16) & 0xFFFFu,
                  abc::kIndexFormatVersion & 0xFFFFu,
                  ABC_INTERFACE_VERSION_STRING);
      return 0;
    }
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        usageError("option " + arg + " requires a value");
      }
      return std::string(argv[++i]);
    };
    if (arg == "--input") {
      input = value();
      have_input = true;
    } else if (arg == "--output") {
      output = value();
      have_output = true;
    } else if (arg == "--source") {
      source = value();
    } else if (arg == "--report") {
      report_path = value();
    } else {
      usageError("unknown option: " + arg);
    }
  }

  if (!have_input) usageError("missing required option --input");
  if (!have_output) usageError("missing required option --output");

  ImportOptions options;
  options.source = source;
  const ImportResult result =
      abc::dict_import::compileIndexFile(input, output, options);

  if (result.exit_code == ExitCode::kFatal ||
      result.exit_code == ExitCode::kSelfCheckFailed) {
    std::fprintf(stderr, "abc_dict_import: fatal: %s\n",
                 result.error.c_str());
    return static_cast<int>(result.exit_code);
  }

  const std::string report_text = formatReport(result);
  std::fputs(report_text.c_str(), stdout);
  if (!report_path.empty()) {
    std::string err;
    if (!writeTextFile(report_path, report_text, &err)) {
      std::fprintf(stderr, "abc_dict_import: fatal: %s\n", err.c_str());
      return static_cast<int>(ExitCode::kFatal);
    }
  }
  return static_cast<int>(result.exit_code);
}
