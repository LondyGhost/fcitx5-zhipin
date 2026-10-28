// A5 — 配置解析单元测试（tests/deploy/test_config_parser.cpp）。
// 覆盖 docs/interfaces/config.md 全部规则：键表、默认值、范围回退、
// 重复键、未知键、BOM/CRLF、注释、~ 展开、默认索引路径、警告脱敏。
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "abc_config.h"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cerr << "[FAIL] " << what << "\n";
    ++g_failures;
  }
}

bool hasWarning(const abc::config::ParseResult& r, const std::string& needle) {
  for (const std::string& w : r.warnings) {
    if (w.find(needle) != std::string::npos) return true;
  }
  return false;
}

bool noWarningContains(const abc::config::ParseResult& r,
                       const std::string& needle) {
  for (const std::string& w : r.warnings) {
    if (w.find(needle) != std::string::npos) return false;
  }
  return true;
}

// 结果永远可直接交给 createEngineSession（契约 config.md 第 6 节）。
bool alwaysValid(const abc::config::ParseResult& r) {
  const abc::EngineConfig& c = r.engine;
  return c.pageSize >= 1 && c.pageSize <= abc::kMaxPageSize &&
         c.maxBufferChars >= 1 && c.maxBufferChars <= 256 &&
         (c.initialMode == abc::Mode::kEnglish ||
          c.initialMode == abc::Mode::kChineseEmpty) &&
         !r.addon.modeSwitchKey.empty() && !r.addon.indexPath.empty();
}

// 环境变量读写：POSIX 用 setenv/unsetenv；Windows 用 _putenv_s。
// 目标平台是 Linux，这里只为让测试在开发机（MSVC/MinGW）也能直接编译。
int envSet(const char* name, const char* value) {
#if defined(_WIN32)
  return _putenv_s(name, value);
#else
  return ::setenv(name, value, 1);
#endif
}

int envUnset(const char* name) {
#if defined(_WIN32)
  return _putenv_s(name, "");
#else
  return ::unsetenv(name);
#endif
}

struct EnvGuard {  // 保存/恢复环境变量，测试互不污染
  std::string name;
  bool had = false;
  std::string old;
  explicit EnvGuard(const char* n) : name(n) {
    const char* v = std::getenv(n);
    had = v != nullptr;
    if (had) old = v;
  }
  void set(const std::string& v) { envSet(name.c_str(), v.c_str()); }
  void unset() { envUnset(name.c_str()); }
  ~EnvGuard() {
    if (had) envSet(name.c_str(), old.c_str());
    else envUnset(name.c_str());
  }
};

void runAll() {
  // 1. 完整合法配置
  {
    const abc::config::ParseResult r = abc::config::parseString(
        "initialMode = english\n"
        "pageSize = 5\n"
        "maxBufferChars = 100\n"
        "spaceNoCandidatePolicy = keep_buffer\n"
        "modeSwitchCompositionPolicy = clear\n"
        "indexPath = /data/my index/abc.abcdix\n"
        "modeSwitchKey = Control+space\n"
        "preeditInClient = false\n");
    check(r.warnings.empty(), "valid config produces no warnings");
    check(r.engine.initialMode == abc::Mode::kEnglish, "initialMode=english");
    check(r.engine.pageSize == 5, "pageSize=5");
    check(r.engine.maxBufferChars == 100, "maxBufferChars=100");
    check(r.engine.spaceNoCandidatePolicy ==
              abc::SpaceNoCandidatePolicy::kKeepBufferAndForward,
          "spaceNoCandidatePolicy=keep_buffer");
    check(r.engine.modeSwitchCompositionPolicy ==
              abc::ModeSwitchCompositionPolicy::kClearComposition,
          "modeSwitchCompositionPolicy=clear");
    check(r.addon.indexPath == "/data/my index/abc.abcdix",
          "indexPath preserves spaces");
    check(r.addon.modeSwitchKey == "Control+space", "modeSwitchKey applied");
    check(!r.addon.preeditInClient, "preeditInClient=false");
    check(alwaysValid(r), "valid config result is session-ready");
  }

  // 2. 未知键、重复键（最后生效）、注释/空行/缩进
  {
    const abc::config::ParseResult r = abc::config::parseString(
        "# 注释行\n"
        "\n"
        "   pageSize = 3   \n"
        "pageSize = 7\n"
        "unknownKey = whatever\n"
        "  # 缩进注释\n");
    check(r.engine.pageSize == 7, "duplicate key: last wins");
    check(hasWarning(r, "unknownKey"), "unknown key warned");
    check(hasWarning(r, "duplicate"), "duplicate key warned");
    check(noWarningContains(r, "whatever"),
          "unknown key warning does not leak value");
    check(alwaysValid(r), "unknown/dup config result is session-ready");
  }

  // 3. 非法值回退默认 + 警告不含值内容
  {
    const abc::config::ParseResult r = abc::config::parseString(
        "pageSize = 0\n"
        "pageSize = 10\n"
        "maxBufferChars = 257\n"
        "initialMode = french\n"
        "spaceNoCandidatePolicy = explode\n"
        "modeSwitchCompositionPolicy = zap\n"
        "preeditInClient = maybe\n");
    check(r.engine.pageSize == 9, "bad pageSize falls back to default 9");
    check(r.engine.maxBufferChars == 64, "bad maxBufferChars falls back 64");
    check(r.engine.initialMode == abc::Mode::kChineseEmpty,
          "bad initialMode falls back chinese");
    check(r.engine.spaceNoCandidatePolicy ==
              abc::SpaceNoCandidatePolicy::kCommitRawAndForward,
          "bad space policy falls back");
    check(r.engine.modeSwitchCompositionPolicy ==
              abc::ModeSwitchCompositionPolicy::kCommitRaw,
          "bad switch policy falls back");
    check(r.addon.preeditInClient, "bad bool falls back true");
    check(hasWarning(r, "pageSize"), "bad pageSize warned");
    check(noWarningContains(r, "257"), "warnings never contain values");
    check(alwaysValid(r), "garbage config result is session-ready");
  }

  // 4. 数字格式严格性
  {
    check(abc::config::parseString("pageSize = -1").engine.pageSize == 9,
          "negative pageSize rejected");
    check(abc::config::parseString("pageSize = 1.5").engine.pageSize == 9,
          "float pageSize rejected");
    check(abc::config::parseString("pageSize = +9").engine.pageSize == 9,
          "plus-signed pageSize rejected");
    check(abc::config::parseString("pageSize = 0000009").engine.pageSize == 9,
          "zero-padded 9 accepted as 9");
    check(abc::config::parseString("pageSize = 99999999999").engine.pageSize == 9,
          "overflowing pageSize rejected");
    check(abc::config::parseString("maxBufferChars = 256").engine.maxBufferChars == 256,
          "maxBufferChars 256 accepted");
  }

  // 5. BOM + CRLF + 无换行结尾
  {
    std::string content = "\xEF\xBB\xBFinitialMode = english\r\npageSize = 4";
    const abc::config::ParseResult r = abc::config::parseString(content);
    check(r.engine.initialMode == abc::Mode::kEnglish, "BOM+CRLF: initialMode");
    check(r.engine.pageSize == 4, "BOM+CRLF: last line without newline");
    check(r.warnings.empty(), "BOM+CRLF produces no warnings");
  }

  // 6. 畸形行（无 =）
  {
    const abc::config::ParseResult r = abc::config::parseString("garbage line\n");
    check(hasWarning(r, "malformed"), "malformed line warned");
    check(alwaysValid(r), "malformed line keeps defaults valid");
  }

  // 7. 空值
  {
    const abc::config::ParseResult r = abc::config::parseString("indexPath =\n");
    check(hasWarning(r, "indexPath"), "empty indexPath warned");
    check(noWarningContains(r, "indexPath ="), "empty value not echoed");
    check(alwaysValid(r), "empty value keeps result valid");
  }

  // 8. 默认 indexPath 与 HOME 展开
  {
    EnvGuard home("HOME"), xdg("XDG_DATA_HOME");
    xdg.unset();
    home.set("/tmp/abc-home");
    check(abc::config::defaultIndexPath(nullptr) ==
              "/tmp/abc-home/.local/share/abc/index.abcdix",
          "default indexPath falls back to HOME/.local/share");
    xdg.set("/xdg/data");
    check(abc::config::defaultIndexPath(nullptr) == "/xdg/data/abc/index.abcdix",
          "XDG_DATA_HOME wins for default indexPath");

    EnvGuard home2("HOME");
    home2.set("/users/some one");
    const abc::config::ParseResult r =
        abc::config::parseString("indexPath = ~/im/index.abcdix\n");
    check(r.addon.indexPath == "/users/some one/im/index.abcdix",
          "leading ~/ expanded with HOME");
    const abc::config::ParseResult r2 =
        abc::config::parseString("indexPath = ~\n");
    check(r2.addon.indexPath == "/users/some one", "bare ~ expanded to HOME");
  }

  // 9. HOME/XDG 都缺失时的默认路径与警告
  {
    EnvGuard home("HOME"), xdg("XDG_DATA_HOME");
    home.unset();
    xdg.unset();
    const abc::config::ParseResult r = abc::config::parseString("");
    check(r.addon.indexPath == "abc/index.abcdix",
          "no env: relative default indexPath");
    check(hasWarning(r, "XDG_DATA_HOME"), "no env warned");
    check(alwaysValid(r), "no-env result is session-ready");
  }

  // 10. 文件缺失 → 全默认 + 警告
  {
    const abc::config::ParseResult r =
        abc::config::parseFile("/nonexistent/abc/config.ini");
    check(r.engine.pageSize == 9 && r.addon.modeSwitchKey == "Shift" &&
              r.addon.preeditInClient,
          "missing file yields defaults");
    check(hasWarning(r, "not readable"), "missing file warned");
    check(alwaysValid(r), "missing-file result is session-ready");
  }

  // 11. 大小写不敏感的枚举/布尔词
  {
    const abc::config::ParseResult r = abc::config::parseString(
        "initialMode = ENGLISH\npreeditInClient = FALSE\n"
        "spaceNoCandidatePolicy = KEEP_BUFFER\n");
    check(r.engine.initialMode == abc::Mode::kEnglish, "enum case-insensitive");
    check(!r.addon.preeditInClient, "bool case-insensitive");
    check(r.warnings.empty(), "case-insensitive values produce no warnings");
  }
}

}  // namespace

int main() {
  runAll();
  if (g_failures == 0) {
    std::cout << "abc_config_tests: ALL PASS\n";
    return 0;
  }
  std::cout << "abc_config_tests: " << g_failures << " failure(s)\n";
  return 1;
}
