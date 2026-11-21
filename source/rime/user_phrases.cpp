// user_phrases.cpp — 用户词组记忆的实现。
#include "user_phrases.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace abc {
namespace {

// 每个编码最多留几个词组；总量上限防止文件无限膨胀。
constexpr std::size_t kMaxPerCode = 8;
constexpr std::size_t kMaxTotal = 5000;

// 去掉音节分隔符，让 shan'an 与 shanan 归一到同一个键。
std::string normalizeCode(const std::string& code) {
  std::string out;
  out.reserve(code.size());
  for (const char c : code) {
    if (c != '\'') out.push_back(c);
  }
  return out;
}

// UTF-8 码位计数：用于「至少两个字符才算词组」。
std::size_t utf8Length(const std::string& s) {
  std::size_t n = 0;
  for (const char c : s) {
    if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ++n;
  }
  return n;
}

void ensureParentDir(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) return;
  const std::string dir = path.substr(0, slash);
  // 逐级创建（只处理常见的 $HOME/.local/share/abc 这种深度）。
  std::string cur;
  std::size_t i = 0;
  if (!dir.empty() && dir[0] == '/') {
    cur = "/";
    i = 1;
  }
  while (i <= dir.size()) {
    const std::size_t next = dir.find('/', i);
    const std::string part = dir.substr(i, next == std::string::npos
                                              ? std::string::npos
                                              : next - i);
    if (!part.empty()) {
      if (cur.size() > 1 || cur.empty()) cur += "/";
      cur += part;
      ::mkdir(cur.c_str(), 0755);
    }
    if (next == std::string::npos) break;
    i = next + 1;
  }
}

}  // namespace

UserPhraseStore& UserPhraseStore::instance() {
  static UserPhraseStore store;
  return store;
}

void UserPhraseStore::configure(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (loaded_) return;
  loaded_ = true;
  path_ = path;
  if (!path_.empty()) loadLocked();
}

void UserPhraseStore::loadLocked() {
  std::ifstream in(path_);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string code;
    std::string text;
    std::string countText;
    if (!std::getline(iss, code, '\t')) continue;
    if (!std::getline(iss, text, '\t')) continue;
    std::getline(iss, countText, '\t');
    if (code.empty() || text.empty()) continue;
    Entry e;
    e.text = text;
    e.count = countText.empty() ? 1 : std::atoi(countText.c_str());
    if (e.count < 1) e.count = 1;
    e.seq = ++seq_;
    table_[code].push_back(std::move(e));
  }
}

void UserPhraseStore::saveLocked() {
  if (path_.empty()) return;
  ensureParentDir(path_);
  const std::string tmp = path_ + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    for (const auto& [code, entries] : table_) {
      for (const Entry& e : entries) {
        out << code << '\t' << e.text << '\t' << e.count << '\n';
      }
    }
  }
  std::rename(tmp.c_str(), path_.c_str());
}

void UserPhraseStore::add(const std::string& code, const std::string& phrase) {
  if (phrase.empty() || utf8Length(phrase) < 2) return;
  const std::string key = normalizeCode(code);
  if (key.empty()) return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!loaded_) {
    loaded_ = true;
    if (!path_.empty()) loadLocked();
  }
  auto& entries = table_[key];
  for (Entry& e : entries) {
    if (e.text == phrase) {
      ++e.count;
      e.seq = ++seq_;
      saveLocked();
      return;
    }
  }
  // 总量上限：简单粗暴地砍掉最久未用的编码组。
  if (table_.size() >= kMaxTotal) {
    table_.erase(table_.begin());
  }
  Entry e;
  e.text = phrase;
  e.count = 1;
  e.seq = ++seq_;
  entries.push_back(std::move(e));
  if (entries.size() > kMaxPerCode) {
    // 留频次最高的 kMaxPerCode 个
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) {
                       return a.count > b.count;
                     });
    entries.resize(kMaxPerCode);
  }
  saveLocked();
}

std::vector<std::string> UserPhraseStore::lookup(const std::string& code) {
  const std::string key = normalizeCode(code);
  std::vector<std::string> out;
  if (key.empty()) return out;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!loaded_) {
    loaded_ = true;
    if (!path_.empty()) loadLocked();
  }
  const auto it = table_.find(key);
  if (it == table_.end()) return out;

  std::vector<Entry> sorted = it->second;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const Entry& a, const Entry& b) {
                     if (a.count != b.count) return a.count > b.count;
                     return a.seq > b.seq;
                   });
  out.reserve(sorted.size());
  for (const Entry& e : sorted) out.push_back(e.text);
  return out;
}

std::string UserPhraseStore::compose(const std::string& code) {
  const std::string key = normalizeCode(code);
  if (key.empty()) return std::string();

  std::lock_guard<std::mutex> lock(mutex_);
  if (!loaded_) {
    loaded_ = true;
    if (!path_.empty()) loadLocked();
  }

  std::string out;
  std::size_t pos = 0;
  int chunks = 0;
  while (pos < key.size()) {
    // 从长到短找已学编码前缀：优先长片段，避免 "jin" 抢走 "jinhai"。
    bool matched = false;
    for (std::size_t len = key.size() - pos; len >= 1; --len) {
      const auto it = table_.find(key.substr(pos, len));
      if (it == table_.end() || it->second.empty()) continue;
      const Entry* best = &it->second.front();
      for (const Entry& e : it->second) {
        if (e.count > best->count) best = &e;
      }
      out += best->text;
      pos += len;
      ++chunks;
      matched = true;
      break;
    }
    if (!matched) return std::string();  // 有切不开的部分，整串拼不出来
  }
  // 只有一段说明这就是精确命中的情况，交给 lookup 处理。
  return chunks >= 2 ? out : std::string();
}

void UserPhraseStore::clearForTest() {
  std::lock_guard<std::mutex> lock(mutex_);
  table_.clear();
  seq_ = 0;
}

}  // namespace abc
