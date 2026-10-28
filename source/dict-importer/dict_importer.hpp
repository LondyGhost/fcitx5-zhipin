// A2 — Rime 词库导入器（离线索引编译器）核心 API。
//
// 契约依据：
//   - docs/interfaces/index-format.md  索引布局 v1.0、确定性、生产者义务（§6）
//   - docs/interfaces/dictionary.md    字段约束（2.1）、去重（2.3）、stableId（2.2）
//   - tasks/A2-Rime词库导入器.md       支持格式子集、坏行报告、不执行词库内容
//
// 支持的输入格式（v1 冻结子集，详见 docs/modules/rime-import.md）：
//   - UTF-8（含/不含 BOM）的 Rime 词库文本；\n 换行，行尾 \r 被剥离；
//   - 可选 YAML 头：`---` 与 `...` 之间的平铺 `key: value` 行；
//     `columns` / `import_tables` / `vocabulary` 一律致命错误（不支持且不得静默），
//     其余键忽略并计入报告；
//   - 无 YAML 头的纯文本码表（custom_phrase.txt 风格）；
//   - 表体行：`text \t code [\t weight]`；`#` 开头为注释；空行忽略；
//   - code 列做归一化：去除空格分隔符并折叠 A-Z → a-z，归一化后必须为
//     1..kIndexMaxCodeBytes 字节的纯小写字母（引擎缓冲只容纳 a-z，契约 2.1
//     授权 A2 定义编码字符集）；
//   - weight：可选；语法 ^[+-]?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?$，
//     经 strtod（C 小数点；调用方不得改 setlocale）解析并要求整字段消费、
//     结果有限；缺省为 0.0（-0.0 归一化为 +0.0）；
//   - 不支持转义序列：反斜杠是字面字符（与 Rime 实际行为一致）。
//
// 坏行/不支持语法：逐行跳过并在报告登记行号、原因与转义摘录，绝不静默吞行；
// 结构性错误（无法确定词条含义，如 import_tables 引用外部表）为致命错误，
// 不写出任何索引。
//
// 确定性：同一输入文件 + 同一 options 重复编译，索引逐字节一致（契约 3.2），
// 排序用字节序比较、权重用 IEEE-754 严格解析，报告不含时间戳/主机信息。
//
// 资源上界（有界解析）：输入 ≤ 512 MiB；有效词条 ≤ kIndexMaxRecords；
// 解析与去重需将词条常驻内存（离线工具，上界即上述两个常数）。

#ifndef ABC_DICT_IMPORTER_HPP_
#define ABC_DICT_IMPORTER_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "abc/index_format.h"  // kIndexMax*、kIndexFormatVersion（A0 常量）

namespace abc {
namespace dict_import {

// 导入结果退出码（CLI 退出码与本枚举一致）。
enum class ExitCode : int {
  kOk = 0,               // 成功且无跳过行
  kFatal = 1,            // 致命错误：输入不可读/结构不支持/输出不可写；未写出索引
  kSkippedLines = 2,     // 索引已写出，但存在被跳过的坏行（报告含定位）
  kSelfCheckFailed = 3,  // 输出自检失败（导入器缺陷；不写出索引）
};

// 一条被跳过行的可定位诊断。摘录转义：0x20..0x7E 之外的字节 → \xNN，
// 反斜杠 → \\；最多 48 字节，截断时追加 "..."。
struct LineIssue {
  std::uint64_t line = 0;  // 1 基行号
  std::string reason;      // 稳定的英文诊断文本（便于 golden 测试断言）
  std::string excerpt;     // 该行原始字节的前缀（转义后）
};

// 导入统计。只进入报告，绝不写入索引（契约 3.2）。
struct ImportCounters {
  std::uint64_t lines_total = 0;        // 物理行数（BOM 后）
  std::uint64_t blank_lines = 0;
  std::uint64_t comment_lines = 0;
  std::uint64_t header_keys_ignored = 0;  // YAML 头被忽略键的出现次数
  std::uint64_t table_rows = 0;           // 进入字段解析的数据行
  std::uint64_t valid_rows = 0;           // 解析为词条的行（去重前）
  std::uint64_t skipped_rows = 0;         // 坏行/不支持行（issues 中有定位）
  std::uint64_t duplicates_removed = 0;   // 去重删除条数（契约 2.3）
  std::uint64_t records = 0;              // 写出记录数
  std::uint64_t codes = 0;                // 写出不同编码数（= 头部 codeCount）
};

struct ImportOptions {
  std::string source;  // 来源标识；空 = 输入文件基名（不含目录）。
                       // 非空时必须 ≤ kIndexMaxSourceBytes 且不含控制字符。
};

struct ImportReport {
  std::string input_path;   // 显式参数回显（不展开符号链接/相对路径）
  std::string output_path;  // 未写出（致命）时为空
  std::string source;       // 实际使用的来源标识
  bool bom_stripped = false;
  std::vector<std::string> ignored_header_keys;  // 忽略的键名（按字节序去重）
  ImportCounters counters;
  std::vector<LineIssue> issues;  // 按行号升序（解析顺序）
  std::uint32_t crc32 = 0;        // 写出索引的 CRC-32（对 [0x20, EOF)）
  std::uint64_t index_bytes = 0;  // 索引字节数（48 头部 + 记录区）
};

struct ImportResult {
  ExitCode exit_code = ExitCode::kOk;
  std::string error;  // 致命错误诊断（含路径与行号/阶段定位）；非致命时为空
  ImportReport report;
};

// 解析 input_path 并编译为 index-format.md v1.0 索引字节流（不写文件）。
// out_index 仅在 exit_code == kOk / kSkippedLines 时被赋值。
// 同一输入重复调用结果逐字节一致；自检失败返回 kSelfCheckFailed。
ImportResult compileIndexToBuffer(const std::string& input_path,
                                  const ImportOptions& options,
                                  std::vector<unsigned char>* out_index);

// compileIndexToBuffer + 写出 output_path：
//   - output_path 已存在 → kFatal（不覆盖既有文件，含同路径输入的情况）；
//   - 写出后回读校验长度一致，不一致按 kFatal 报告（可能留下残缺文件，
//     诊断中说明）。
ImportResult compileIndexFile(const std::string& input_path,
                              const std::string& output_path,
                              const ImportOptions& options);

}  // namespace dict_import
}  // namespace abc

#endif  // ABC_DICT_IMPORTER_HPP_
