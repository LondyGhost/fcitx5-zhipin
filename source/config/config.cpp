// config.cpp — 用户配置解析实现（A5）。
// 键表、默认值、范围与错误规则全部来自 docs/interfaces/config.md（v1.0.0-rc1）。
// 警告信息只含键名与原因，绝不含值内容（路径隐私，契约第 6 节）。
#include "abc_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace abc {
namespace config {
namespace {

std::string trim(const std::string& s) {
  const auto begin = s.find_first_not_of(" \t\v\f");
  if (begin == std::string::npos) return "";
  const auto end = s.find_last_not_of(" \t\v\f");
  return s.substr(begin, end - begin + 1);
}

std::string asciiLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

// 严格十进制无符号整数解析（拒绝 +、-、十六进制、溢出）。
bool parseUint(const std::string& s, uint32_t* out) {
  if (s.empty() || s.size() > 10) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  if (v > 0xFFFFFFFFULL) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

// 展开前导 "~" 或 "~/..."（契约示例允许 ~ 路径）。HOME 缺失时失败。
bool expandHome(const std::string& value, std::string* out) {
  if (value != "~" && value.rfind("~/", 0) != 0) {
    *out = value;
    return true;
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') return false;
  if (value == "~") {
    *out = home;
  } else {
    *out = std::string(home) + value.substr(1);
  }
  return true;
}

std::string warningLine(std::size_t lineNo, const std::string& key,
                        const std::string& reason) {
  std::ostringstream w;
  w << "warning: line " << lineNo << ": ";
  if (!key.empty()) w << "key '" << key << "': ";
  w << reason;
  return w.str();
}

struct Parser {
  ParseResult result;
  std::unordered_set<std::string> seen;

  void warn(std::size_t lineNo, const std::string& key,
            const std::string& reason) {
    result.warnings.push_back(warningLine(lineNo, key, reason));
  }

  void applyEnumPair(std::size_t lineNo, const std::string& key,
                     const std::string& value, bool* target,
                     const char* defaultText) {
    const std::string v = asciiLower(value);
    if (v == "true" || v == "false") {
      *target = (v == "true");
    } else {
      warn(lineNo, key, std::string("expected true/false, using default ") +
                            defaultText);
    }
  }
};

void applyKeyValue(Parser* p, std::size_t lineNo, const std::string& key,
                   const std::string& value) {
  EngineConfig& cfg = p->result.engine;
  AddonSettings& addon = p->result.addon;

  if (key == "initialMode") {
    const std::string v = asciiLower(value);
    if (v == "chinese") cfg.initialMode = Mode::kChineseEmpty;
    else if (v == "english") cfg.initialMode = Mode::kEnglish;
    else p->warn(lineNo, key, "expected 'chinese' or 'english', using default");
    return;
  }
  if (key == "pageSize") {
    uint32_t v = 0;
    if (parseUint(value, &v) && v >= 1 && v <= kMaxPageSize) {
      cfg.pageSize = v;
    } else {
      p->warn(lineNo, key, "expected integer in 1..9, using default");
    }
    return;
  }
  if (key == "maxBufferChars") {
    uint32_t v = 0;
    if (parseUint(value, &v) && v >= 1 && v <= kAbsoluteMaxBufferChars) {
      cfg.maxBufferChars = v;
    } else {
      p->warn(lineNo, key, "expected integer in 1..256, using default");
    }
    return;
  }
  if (key == "spaceNoCandidatePolicy") {
    const std::string v = asciiLower(value);
    if (v == "commit_raw") {
      cfg.spaceNoCandidatePolicy = SpaceNoCandidatePolicy::kCommitRawAndForward;
    } else if (v == "keep_buffer") {
      cfg.spaceNoCandidatePolicy = SpaceNoCandidatePolicy::kKeepBufferAndForward;
    } else {
      p->warn(lineNo, key,
              "expected 'commit_raw' or 'keep_buffer', using default");
    }
    return;
  }
  if (key == "modeSwitchCompositionPolicy") {
    const std::string v = asciiLower(value);
    if (v == "commit_raw") {
      cfg.modeSwitchCompositionPolicy =
          ModeSwitchCompositionPolicy::kCommitRaw;
    } else if (v == "clear") {
      cfg.modeSwitchCompositionPolicy =
          ModeSwitchCompositionPolicy::kClearComposition;
    } else {
      p->warn(lineNo, key, "expected 'commit_raw' or 'clear', using default");
    }
    return;
  }
  if (key == "indexPath") {
    std::string expanded;
    if (value.empty()) {
      p->warn(lineNo, key, "empty value, using default");
      addon.indexPath = defaultIndexPath(&p->result.warnings);
    } else if (expandHome(value, &expanded)) {
      addon.indexPath = expanded;
    } else {
      p->warn(lineNo, key, "cannot expand '~' (HOME unset), using default");
      addon.indexPath = defaultIndexPath(&p->result.warnings);
    }
    return;
  }
  if (key == "modeSwitchKey") {
    if (value.empty()) {
      p->warn(lineNo, key, "empty value, using default");
    } else {
      addon.modeSwitchKey = value;
    }
    return;
  }
  if (key == "preeditInClient") {
    p->applyEnumPair(lineNo, key, value, &addon.preeditInClient, "true");
    return;
  }
  p->warn(lineNo, key, "unknown key, ignored");
}

void parseLines(Parser* p, const std::string& content) {
  std::istringstream in(content);
  std::string line;
  std::size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    if (lineNo == 1 && line.size() >= 3 &&  // UTF-8 BOM（契约第 3 节允许）
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
      line.erase(0, 3);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF 容错

    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;  // 空行/整行注释

    const auto eq = trimmed.find('=');
    if (eq == std::string::npos) {
      p->warn(lineNo, "", "malformed line (missing '='), ignored");
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq));
    const std::string value = trim(trimmed.substr(eq + 1));
    if (key.empty()) {
      p->warn(lineNo, "", "empty key, ignored");
      continue;
    }
    if (!p->seen.insert(key).second) {
      p->warn(lineNo, key, "duplicate key, last value wins");
    }
    applyKeyValue(p, lineNo, key, value);
  }
}

}  // namespace

std::string defaultIndexPath(std::vector<std::string>* warnings) {
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/abc/index.abcdix";
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && *home != '\0') {
    return std::string(home) + "/.local/share/abc/index.abcdix";
  }
  if (warnings != nullptr) {
    warnings->push_back(
        "warning: XDG_DATA_HOME and HOME are unset; "
        "indexPath defaulted to relative 'abc/index.abcdix'");
  }
  return "abc/index.abcdix";
}

ParseResult parseString(const std::string& content) {
  Parser p;
  p.result.addon.indexPath = defaultIndexPath(&p.result.warnings);
  parseLines(&p, content);
  return std::move(p.result);
}

ParseResult parseFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ParseResult p;
    p.addon.indexPath = defaultIndexPath(&p.warnings);
    p.warnings.push_back("warning: config file not readable, using defaults");
    return p;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return parseString(buffer.str());
}

}  // namespace config
}  // namespace abc
