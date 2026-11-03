// rime_engine.cpp — librime 后端实现（B 方案）
//
// 状态机与智能ABC 语义：
//   kComposing  : 字母进 buffer，转给 Rime，但【不显示候选】
//   kSelecting  : 按过空格，显示候选（Rime 的候选 + 首音节单字）
//   空格        : 未放出 → 放出（消费）；已放出 → 上屏高亮项
//   回车        : 原样上屏 buffer
//   数字        : 仅在选择态有效，按索引上屏
#include "rime_engine.h"

#include <rime_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "abc/key.h"

namespace abc {
namespace {

// 合法拼音音节表（由 tools/gen_syllables.py 从 rime-ice 字库生成）。
#include "syllables.inc"

bool isSyllable(const std::string& s) {
  const std::size_t count = sizeof(kSyllables) / sizeof(kSyllables[0]);
  std::size_t lo = 0;
  std::size_t hi = count;
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    const int cmp = std::strcmp(kSyllables[mid], s.c_str());
    if (cmp < 0) {
      lo = mid + 1;
    } else if (cmp > 0) {
      hi = mid;
    } else {
      return true;
    }
  }
  return false;
}

Effect commitTextEffect(std::string text) {
  Effect e;
  e.kind = Effect::Kind::kCommitText;
  e.text = std::move(text);
  return e;
}

Effect clearCompositionEffect() {
  Effect e;
  e.kind = Effect::Kind::kClearComposition;
  return e;
}

// ABC_RIME_DEBUG=1 时把 rime 的内部状态打到 stderr，便于排查。
bool debugRime() {
  static const bool on = std::getenv("ABC_RIME_DEBUG") != nullptr;
  return on;
}

// ---- X11 keysym ----
constexpr int kSymSpace = 0x20;
constexpr int kSymReturn = 0xff0d;
constexpr int kSymBackSpace = 0xff08;
constexpr int kSymEscape = 0xff1b;
constexpr int kSymLeft = 0xff51;
constexpr int kSymUp = 0xff52;
constexpr int kSymRight = 0xff53;
constexpr int kSymDown = 0xff54;
constexpr int kSymPageUp = 0xff55;
constexpr int kSymPageDown = 0xff56;

// 首音节单字最多补这么多个
constexpr std::size_t kMaxSingleChars = 60;
constexpr std::size_t kMaxSyllableBytes = 6;

// 中文态标点映射（智能ABC 习惯：中文态出全角标点）。
// key = 半角字符，value = 中文态输出；英文模式原样上屏。
struct PunctPair {
  char half;
  const char* full;
};

constexpr PunctPair kPunctMap[] = {
    {',', "\xEF\xBC\x8C"},   // ，
    {'.', "\xE3\x80\x82"},   // 。
    {';', "\xEF\xBC\x9B"},   // ；
    {':', "\xEF\xBC\x9A"},   // ：
    {'?', "\xEF\xBC\x9F"},   // ？
    {'!', "\xEF\xBC\x81"},   // ！
    {'(', "\xEF\xBC\x88"},   // （
    {')', "\xEF\xBC\x89"},   // ）
    {'[', "\xE3\x80\x94"},   // 〔
    {']', "\xE3\x80\x95"},   // 〕
    {'{', "\xEF\xBD\x9B"},   // ｛
    {'}', "\xEF\xBD\x9D"},   // ｝
    {'<', "\xE3\x80\x8A"},   // 《
    {'>', "\xE3\x80\x8B"},   // 》
    {'~', "\xEF\xBD\x9E"},   // ～
    {'@', "\xC2\xB7"},       // ·（间隔号，智能ABC 习惯）
    {'^', "\xE2\x80\xA6\xE2\x80\xA6"},  // ……
    {'\\', "\xE3\x80\x81"},  // 、
    {'|', "\xEF\xBD\x9C"},   // ｜
    {'_', "\xE2\x80\x94\xE2\x80\x94"},  // ——
};

// 智能ABC 的特殊标点：双引号交替出弯引号（" → “ ” “ …）。
// 方向由会话成员 quoteOpen_ 承载，见 RimeEngineSession。

const char* zhPunct(char32_t unicode) {
  if (unicode > 0x7F) return nullptr;
  for (const PunctPair& p : kPunctMap) {
    if (static_cast<char32_t>(static_cast<unsigned char>(p.half)) == unicode) {
      return p.full;
    }
  }
  return nullptr;
}

RimeApi* api() { return rime_get_api(); }

// ---------------- 全局初始化 ----------------

std::mutex g_initMutex;
bool g_initDone = false;
bool g_ready = false;
RimeOptions g_options;

void fillTraits(RimeTraits* traits, const RimeOptions& opts) {
  std::memset(traits, 0, sizeof(*traits));
  traits->data_size = sizeof(RimeTraits);
  traits->shared_data_dir = opts.sharedDataDir.c_str();
  traits->user_data_dir = opts.userDataDir.c_str();
  traits->log_dir = opts.logDir.c_str();
  traits->distribution_name = "Rime";
  traits->distribution_code_name = "abc-rime";
  traits->distribution_version = "0.1";
  traits->app_name = "rime.abc";
  traits->min_log_level = 2;  // warning 以上
}

// ---------------- 单字查询（用独立会话当音节 oracle） ----------------

bool isSingleChar(const char* text) {
  if (text == nullptr || text[0] == '\0') return false;
  int chars = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if ((static_cast<unsigned char>(*p) & 0xC0u) != 0x80u) ++chars;
  }
  return chars == 1;
}

class SingleCharIndex {
 public:
  void setSession(RimeSessionId sid) { sid_ = sid; }

  // 退出前必须显式销毁会话：librime 的 Service 析构会对残留会话崩溃。
  void reset() {
    if (sid_ != 0 && api() != nullptr) api()->destroy_session(sid_);
    sid_ = 0;
    cache_.clear();
  }

  // 该串作为音节时的单字候选（懒查询 + 缓存）。
  const std::vector<std::string>& chars(const std::string& syllable) {
    auto it = cache_.find(syllable);
    if (it != cache_.end()) return it->second;

    std::vector<std::string> out;
    if (sid_ != 0 && api() != nullptr) {
      api()->clear_composition(sid_);
      bool typed = true;
      for (const char c : syllable) {
        if (!api()->process_key(sid_, static_cast<int>(static_cast<unsigned char>(c)), 0)) {
          typed = false;
          break;
        }
      }
      // 逐页收集单字：rime 的候选里词远多于字，要翻得够深才够用。
      constexpr int kMaxPages = 12;
      for (int page = 0; typed && page < kMaxPages; ++page) {
        RIME_STRUCT(RimeContext, ctx);
        if (!api()->get_context(sid_, &ctx)) break;
        for (int i = 0; i < ctx.menu.num_candidates; ++i) {
          const char* text = ctx.menu.candidates[i].text;
          if (isSingleChar(text)) {
            out.emplace_back(text);
            if (out.size() >= kMaxSingleChars) break;
          }
        }
        const bool last = ctx.menu.is_last_page != 0;
        api()->free_context(&ctx);
        if (last || out.size() >= kMaxSingleChars) break;
        api()->process_key(sid_, kSymPageDown, 0);
      }
      api()->clear_composition(sid_);
      if (debugRime()) {
        std::fprintf(stderr, "[chars] '%s' -> %zu\n", syllable.c_str(),
                     out.size());
      }
    }
    return cache_.emplace(syllable, std::move(out)).first->second;
  }

 private:
  RimeSessionId sid_ = 0;
  std::map<std::string, std::vector<std::string>> cache_;
};

// 把 code 切成音节数最少的方案，输出完整切分；并列取更长首音节。
// 例：zhineng -> ["zhi","neng"]，xian -> ["xian"]，切不开返回 false。
bool segmentAll(const std::string& code, SingleCharIndex* index,
                std::vector<std::string>* out) {
  (void)index;
  const std::size_t n = code.size();
  if (n < 2) return false;
  const std::size_t kInf = static_cast<std::size_t>(-1);
  std::vector<std::size_t> best(n + 1, kInf);
  std::vector<std::size_t> pick(n + 1, 0);
  best[n] = 0;
  for (std::size_t i = n; i-- > 0;) {
    const std::size_t maxLen = std::min(kMaxSyllableBytes, n - i);
    for (std::size_t len = 1; len <= maxLen; ++len) {
      if (best[i + len] == kInf) continue;
      if (!isSyllable(code.substr(i, len))) continue;
      const std::size_t cand = best[i + len] + 1;
      if (cand < best[i] || (cand == best[i] && len > pick[i])) {
        best[i] = cand;
        pick[i] = len;
      }
    }
  }
  if (best[0] == kInf || pick[0] == 0) return false;
  out->clear();
  for (std::size_t i = 0; i < n;) {
    out->push_back(code.substr(i, pick[i]));
    i += pick[i];
  }
  return true;
}

// 首音节；切不开返回 false。当前未使用（保留给后续按音节分组）。
[[maybe_unused]] bool firstSyllable(const std::string& code,
                                    SingleCharIndex* index, std::string* out) {
  std::vector<std::string> syllables;
  if (!segmentAll(code, index, &syllables) || syllables.empty()) return false;
  *out = syllables.front();
  return true;
}

// ---------------- 会话 ----------------

class RimeEngineSession final : public EngineSession {
 public:
  RimeEngineSession(const EngineConfig& config, RimeSessionId sid,
                    SingleCharIndex* index)
      : config_(config), sid_(sid), index_(index) {
    mode_ = config_.initialMode;
    refresh();
  }

  ~RimeEngineSession() override {
    if (sid_ != 0 && api() != nullptr) api()->destroy_session(sid_);
  }

  EngineResult handle(const KeyCommand& command) override {
    std::vector<Effect> effects;
    bool consumed = false;
    dispatch(command, effects, consumed);
    refresh();
    EngineResult result = last_;
    result.consumed = consumed;
    result.effects = std::move(effects);
    return result;
  }

  EngineResult reset() override {
    if (sid_ != 0 && api() != nullptr) api()->clear_composition(sid_);
    clearState();
    refresh();
    return last_;
  }

  EngineResult snapshot() const override { return last_; }

  const EngineConfig& config() const override { return config_; }

 private:
  void dispatch(const KeyCommand& cmd, std::vector<Effect>& effects,
                bool& consumed) {
    switch (cmd.code) {
      case KeyCode::kCharacter:
        if (cmd.unicode >= U'a' && cmd.unicode <= U'z') {
          onLetter(static_cast<char>(cmd.unicode), effects, consumed);
        } else if (cmd.unicode >= U'A' && cmd.unicode <= U'Z') {
          onLetter(static_cast<char>(cmd.unicode - U'A' + U'a'), effects, consumed);
        } else {
          nonHandledKey(effects, consumed);
        }
        break;
      case KeyCode::kPunctuation:
        onPunctuation(cmd.unicode, effects, consumed);
        break;
      case KeyCode::kBackspace:
        onBackspace(effects, consumed);
        break;
      case KeyCode::kSpace:
        onSpace(effects, consumed);
        break;
      case KeyCode::kEnter:
        onEnter(effects, consumed);
        break;
      case KeyCode::kEscape:
        if (!buffer_.empty()) effects.push_back(clearCompositionEffect());
        clearRime();
        clearState();
        consumed = true;
        break;
      case KeyCode::kDigit:
        onDigit(cmd.digit, effects, consumed);
        break;
      case KeyCode::kLeft:
      case KeyCode::kRight:
        onMoveHighlight(cmd.code == KeyCode::kLeft ? -1 : 1, consumed);
        break;
      case KeyCode::kUp:
      case KeyCode::kPageUp:
        onPage(-1, consumed);
        break;
      case KeyCode::kDown:
      case KeyCode::kPageDown:
        onPage(1, consumed);
        break;
      case KeyCode::kModeSwitch:
        if (!buffer_.empty()) {
          if (config_.modeSwitchCompositionPolicy ==
              ModeSwitchCompositionPolicy::kCommitRaw) {
            // 分阶段选字中缓冲是完整拼音，直接上屏会重复输出已选的字。
            effects.push_back(commitTextEffect(
                stage_ > 0 ? picked_ + remainingCode() : buffer_));
          } else {
            effects.push_back(clearCompositionEffect());
          }
        }
        clearRime();
        clearState();
        mode_ = Mode::kEnglish;
        consumed = true;
        break;
      case KeyCode::kUnknown:
        nonHandledKey(effects, consumed);
        break;
    }
  }

  void onLetter(char ch, std::vector<Effect>& effects, bool& consumed) {
    if (buffer_.size() >= config_.maxBufferChars) return;  // 溢出：不消费
    if (stage_ > 0) {
      // 分阶段选字中来了新字母：视作新输入，已选的字先上屏。
      effects.push_back(commitTextEffect(picked_));
      clearRime();
      clearState();
      // 落入常规组合流程。
    } else if (revealed_ && !displayed_.empty()) {
      // 智能ABC 原味：候选已放出后继续输入字母 = 当前高亮项直接上屏，
      // 新字母作为新一轮组合的开头（不必再按第二次空格）。
      // 例：nihao 空格（出候选）→ 输入 w → 「你好」上屏，预编辑变成 w。
      std::size_t idx = 0;
      if (highlight_ >= 0 &&
          static_cast<std::size_t>(highlight_) < displayed_.size()) {
        idx = static_cast<std::size_t>(highlight_);
      }
      std::string chosen = displayed_[idx].text;
      // rime 候选优先用选词 API，能正确记录词频；拿不到就退回候选原文。
      if (idx < rimeCount_ && api() != nullptr && sid_ != 0 &&
          api()->select_candidate_on_current_page(sid_, idx)) {
        const std::string committed = takeCommit();
        if (!committed.empty()) chosen = committed;
      }
      effects.push_back(commitTextEffect(std::move(chosen)));
      clearRime();
      clearState();
      // 落入常规组合流程：新字母成为新的缓冲首字符。
    }
    buffer_.push_back(ch);
    revealed_ = false;
    highlight_ = 0;
    charPage_ = 0;
    charStart_ = 0;
    processKey(static_cast<int>(static_cast<unsigned char>(ch)));
    mode_ = Mode::kComposing;
    consumed = true;
    (void)effects;
  }

  void onBackspace(std::vector<Effect>& effects, bool& consumed) {
    if (buffer_.empty()) return;
    if (stage_ > 0) {
      // 分阶段选字中：先退已选的字，退完再退字母。
      undoStage();
      consumed = true;
      (void)effects;
      return;
    }
    buffer_.pop_back();
    revealed_ = false;
    highlight_ = 0;
    charPage_ = 0;
    charStart_ = 0;
    processKey(kSymBackSpace);
    if (buffer_.empty()) {
      clearRime();
      clearState();
    } else {
      mode_ = Mode::kComposing;
    }
    consumed = true;
    (void)effects;
  }

  // 中文态标点：先提交手头组合（若有），再出全角标点。
  // 英文模式原样转发。双引号按智能ABC 习惯交替出 “ ”。
  void onPunctuation(char32_t unicode, std::vector<Effect>& effects,
                     bool& consumed) {
    if (mode_ == Mode::kEnglish) {
      consumed = false;
      return;
    }
    const char* full = nullptr;
    if (unicode == U'"') {
      full = quoteOpen_ ? "\xE2\x80\x9C" : "\xE2\x80\x9D";
      quoteOpen_ = !quoteOpen_;
    } else {
      full = zhPunct(unicode);
    }
    if (full == nullptr) {
      // 没有映射的符号（如 # $ %）：仍先提交组合，符号原样上屏。
      if (!buffer_.empty()) {
        effects.push_back(commitTextEffect(
            stage_ > 0 ? picked_ + remainingCode() : buffer_));
        clearRime();
        clearState();
      }
      std::string raw;
      if (unicode <= 0x7F) {
        raw.assign(1, static_cast<char>(unicode));
        effects.push_back(commitTextEffect(std::move(raw)));
        consumed = true;
      } else {
        consumed = false;
      }
      return;
    }
    if (!buffer_.empty()) {
      effects.push_back(commitTextEffect(
          stage_ > 0 ? picked_ + remainingCode() : buffer_));
      clearRime();
      clearState();
    }
    effects.push_back(commitTextEffect(full));
    consumed = true;
  }

  void onSpace(std::vector<Effect>& effects, bool& consumed) {
    if (buffer_.empty()) return;  // 空缓冲：空格交给应用
    if (!revealed_) {
      // 第一次空格：只把候选放出来，不上屏
      revealed_ = true;
      highlight_ = 0;
      mode_ = Mode::kSelecting;
      consumed = true;
      return;
    }
    commitIndex(highlight_, effects);
    consumed = true;
  }

  void onEnter(std::vector<Effect>& effects, bool& consumed) {
    if (buffer_.empty()) return;
    // 分阶段选字中：上屏「已选字 + 剩余编码字母」，与智能ABC 的回车一致。
    // 其余情况原样上屏字母缓冲。
    std::string text = (stage_ > 0) ? picked_ + remainingCode() : buffer_;
    effects.push_back(commitTextEffect(std::move(text)));
    clearRime();
    clearState();
    consumed = true;
  }

  void onDigit(int digit, std::vector<Effect>& effects, bool& consumed) {
    if (!revealed_ || digit < 1) return;
    const std::size_t idx = static_cast<std::size_t>(digit - 1);
    if (idx >= displayed_.size()) return;
    commitIndex(idx, effects);
    consumed = true;
  }

  void onMoveHighlight(int delta, bool& consumed) {
    if (!revealed_ || displayed_.empty()) return;
    const int count = static_cast<int>(displayed_.size());
    int next = highlight_ + delta;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    highlight_ = next;
    consumed = true;
  }

  void onPage(int delta, bool& consumed) {
    if (!revealed_) return;
    if (stage_ > 0) {
      // 分阶段选字：翻当前音节的单字页
      if (syllables_.empty() || stage_ >= syllables_.size()) {
        consumed = true;
        return;
      }
      const std::size_t total = index_->chars(syllables_[stage_]).size();
      if (delta > 0) {
        const std::size_t next =
            static_cast<std::size_t>(stagePage_ + 1) * config_.pageSize;
        if (next < total) ++stagePage_;
      } else if (stagePage_ > 0) {
        --stagePage_;
      }
      highlight_ = 0;
      consumed = true;
      return;
    }
    if (charPage_ > 0) {
      if (syllables_.empty()) {
        consumed = true;
        return;
      }
      if (delta < 0) {
        --charPage_;
      } else {
        const std::size_t total = index_->chars(syllables_.front()).size();
        const std::size_t nextStart =
            charStart_ + static_cast<std::size_t>(charPage_) * config_.pageSize;
        if (nextStart < total) ++charPage_;
      }
      highlight_ = 0;
      consumed = true;
      return;
    }

    const uint32_t before = currentRimePage();
    processKey(delta < 0 ? kSymPageUp : kSymPageDown);
    const uint32_t after = currentRimePage();
    // 已经停在 rime 最后一页还按「下一页」→ 进入单字页（智能ABC 的词尽出字）
    if (delta > 0 && after == before && rimeLastPage_ && !syllables_.empty() &&
        !index_->chars(syllables_.front()).empty()) {
      charPage_ = 1;
    }
    highlight_ = 0;
    consumed = true;
  }

  uint32_t currentRimePage() const {
    if (sid_ == 0 || api() == nullptr) return 0;
    RIME_STRUCT(RimeContext, ctx);
    if (!api()->get_context(sid_, &ctx)) return 0;
    const uint32_t page = static_cast<uint32_t>(ctx.menu.page_no);
    api()->free_context(&ctx);
    return page;
  }

  // 上屏第 idx 个候选。
  //   分阶段选字中：取该字，进入下一音节；全部选完才真正上屏。
  //   选中「单字」且输入可切成多音节：进入分阶段选字（智能ABC 原味）。
  //     注意 Rime 自己也会给出首音节单字，所以必须按内容判断，不能按索引。
  //   其余落在 Rime 候选范围内：转发数字键，让 Rime 记用户词频。
  void commitIndex(std::size_t idx, std::vector<Effect>& effects) {
    if (idx >= displayed_.size()) return;
    const std::string chosen = displayed_[idx].text;
    if (debugRime()) {
      std::fprintf(stderr,
                   "[pick] idx=%zu rimeCount=%zu disp=%zu stage=%zu sylls=%zu "
                   "single=%d chosen='%s'\n",
                   idx, rimeCount_, displayed_.size(), stage_, syllables_.size(),
                   static_cast<int>(isSingleChar(chosen.c_str())),
                   chosen.c_str());
    }

    if (stage_ > 0) {
      pickStagedChar(chosen, effects);
      return;
    }

    if (isSingleChar(chosen.c_str()) && syllables_.size() >= 2) {
      picked_.clear();
      pickStagedChar(chosen, effects);
      return;
    }

    if (idx < rimeCount_) {
      // 用 Rime 的选词 API（不是转发数字键）：会正确提交并记用户词频。
      if (api() != nullptr && sid_ != 0 &&
          api()->select_candidate_on_current_page(sid_, idx)) {
        std::string text = takeCommit();
        if (text.empty()) text = chosen;  // 兜底：拿不到 commit 就用候选原文
        effects.push_back(commitTextEffect(std::move(text)));
        clearState();
        return;
      }
      effects.push_back(commitTextEffect(chosen));
      clearRime();
      clearState();
      return;
    }

    effects.push_back(commitTextEffect(chosen));
    clearRime();
    clearState();
  }

  // 记录一个字，推进到下一音节；全部音节选完则上屏。
  void pickStagedChar(const std::string& ch, std::vector<Effect>& effects) {
    picked_ += ch;
    ++stage_;
    stagePage_ = 0;
    // 进入分阶段选字后不再需要 Rime 的组合，先清掉，避免重复提交。
    if (stage_ == 1) clearRime();
    if (stage_ >= syllables_.size()) {
      effects.push_back(commitTextEffect(picked_));
      clearState();
      return;
    }
    revealed_ = true;
    highlight_ = 0;
    mode_ = Mode::kSelecting;
  }

  // 分阶段选字回退一个音节。
  void undoStage() {
    if (stage_ == 0) return;
    --stage_;
    stagePage_ = 0;
    if (!picked_.empty()) {
      // 弹出最后一个 UTF-8 码位
      std::size_t pos = picked_.size();
      while (pos > 0 && (static_cast<unsigned char>(picked_[pos - 1]) & 0xC0u) == 0x80u) {
        --pos;
      }
      if (pos > 0) --pos;
      picked_.erase(pos);
    }
    highlight_ = 0;
    if (stage_ == 0) rebuildRime();
  }

  // 把 buffer_ 重新喂给 Rime（退出分阶段选字时恢复组合）。
  void rebuildRime() {
    clearRime();
    for (const char c : buffer_) {
      processKey(static_cast<int>(static_cast<unsigned char>(c)));
    }
  }

  void nonHandledKey(std::vector<Effect>& effects, bool& consumed) {
    if (!buffer_.empty()) {
      // 分阶段选字中缓冲仍是完整拼音，直接上屏会重复输出已选的字，
      // 用「已选字 + 剩余编码」作为未处理键的提交内容。
      effects.push_back(commitTextEffect(
          stage_ > 0 ? picked_ + remainingCode() : buffer_));
      clearRime();
      clearState();
    }
    consumed = false;
  }

  bool processKey(int keysym) {
    if (sid_ == 0 || api() == nullptr) return false;
    return api()->process_key(sid_, keysym, 0) != 0;
  }

  std::string takeCommit() {
    std::string text;
    if (sid_ == 0 || api() == nullptr) return text;
    RIME_STRUCT(RimeCommit, commit);
    if (api()->get_commit(sid_, &commit)) {
      if (commit.text != nullptr) text = commit.text;
      api()->free_commit(&commit);
    }
    return text;
  }

  void clearRime() {
    if (sid_ != 0 && api() != nullptr) api()->clear_composition(sid_);
  }

  void clearState() {
    buffer_.clear();
    revealed_ = false;
    highlight_ = 0;
    mode_ = Mode::kChineseEmpty;
    displayed_.clear();
    rimeCount_ = 0;
    syllables_.clear();
    stage_ = 0;
    stagePage_ = 0;
    picked_.clear();
    charPage_ = 0;
    charStart_ = 0;
    rimeLastPage_ = false;
    rimeLastPageNo_ = 0;
    quoteOpen_ = true;
    last_ = EngineResult{};
    last_.mode = mode_;
  }

  // 当前阶段尚未选择的编码（如已选 zhi，剩 "neng"）。
  std::string remainingCode() const {
    std::string out;
    for (std::size_t i = stage_; i < syllables_.size(); ++i) out += syllables_[i];
    return out;
  }

  // 重建对外快照：preedit = 字母缓冲；候选仅在「已放出」时给出。
  void refresh() {
    EngineResult result;
    result.mode = mode_;
    result.preedit = buffer_;
    result.highlight = -1;
    displayed_.clear();
    rimeCount_ = 0;
    // 防御：没有音节切分就没有单字页
    if (charPage_ > 0 && syllables_.empty()) charPage_ = 0;

    if (!buffer_.empty() && revealed_ && sid_ != 0 && api() != nullptr) {
      if (stage_ > 0) {
        // 分阶段选字：只列当前音节的单字，组字框显示「已选字 + 剩余编码」。
        result.preedit = picked_ + remainingCode();
        result.keepPreedit = true;
        const std::vector<std::string>& chars =
            index_->chars(syllables_[stage_]);
        const std::size_t start =
            static_cast<std::size_t>(stagePage_) * config_.pageSize;
        for (std::size_t i = start;
             i < chars.size() && displayed_.size() < config_.pageSize; ++i) {
          Candidate c;
          c.text = chars[i];
          c.code = syllables_[stage_];
          c.source = "abc-single";
          displayed_.push_back(std::move(c));
        }
        result.page = static_cast<uint32_t>(stagePage_);
        const std::size_t pages =
            (chars.size() + config_.pageSize - 1) / config_.pageSize;
        result.pageCount = static_cast<uint32_t>(pages == 0 ? 1 : pages);
      } else if (charPage_ > 0) {
        // 单字页：rime 的词已经翻完，继续给首音节单字。
        const std::vector<std::string>& chars =
            index_->chars(syllables_.front());
        const std::size_t start = charStart_ +
                                  static_cast<std::size_t>(charPage_ - 1) *
                                      config_.pageSize;
        for (std::size_t i = start;
             i < chars.size() && displayed_.size() < config_.pageSize; ++i) {
          Candidate c;
          c.text = chars[i];
          c.code = syllables_.front();
          c.source = "abc-single";
          displayed_.push_back(std::move(c));
        }
        result.page = rimeLastPageNo_ + static_cast<uint32_t>(charPage_);
        result.pageCount = result.page + 1;
      } else {
        RIME_STRUCT(RimeContext, ctx);
        if (api()->get_context(sid_, &ctx)) {
          if (debugRime()) {
            std::fprintf(stderr,
                         "[rime] comp_len=%d preedit='%s' cands=%d page=%d last=%d\n",
                         ctx.composition.length,
                         ctx.composition.preedit ? ctx.composition.preedit : "",
                         ctx.menu.num_candidates, ctx.menu.page_no,
                         static_cast<int>(ctx.menu.is_last_page));
          }
          for (int i = 0; i < ctx.menu.num_candidates; ++i) {
            Candidate c;
            c.text = ctx.menu.candidates[i].text != nullptr
                         ? ctx.menu.candidates[i].text
                         : "";
            c.code = buffer_;
            c.source = "rime";
            if (!c.text.empty()) displayed_.push_back(std::move(c));
          }
          rimeCount_ = displayed_.size();
          result.page = static_cast<uint32_t>(ctx.menu.page_no);
          rimeLastPage_ = ctx.menu.is_last_page != 0;
          rimeLastPageNo_ = result.page;
          result.pageCount =
              ctx.menu.is_last_page ? result.page + 1 : result.page + 2;
          api()->free_context(&ctx);
        }

        // 音节切分：分阶段选字与「词尽出字」都要用，这里保证每次都算好。
        if (syllables_.empty()) segmentAll(buffer_, index_, &syllables_);

        // 智能ABC 语义：**所有词翻完**（rime 最后一页）之后才出首音节单字，
        // 并填满本页剩余位置。Rime 自己也可能给几个字，先按内容去重。
        charStart_ = 0;
        if (rimeLastPage_ && !displayed_.empty()) {
          if (syllables_.size() >= 2) {
            for (const std::string& ch : index_->chars(syllables_.front())) {
              if (displayed_.size() >= config_.pageSize) break;
              bool dup = false;
              for (const Candidate& existing : displayed_) {
                if (existing.text == ch) {
                  dup = true;
                  break;
                }
              }
              if (dup) continue;
              Candidate c;
              c.text = ch;
              c.code = syllables_.front();
              c.source = "abc-single";
              displayed_.push_back(std::move(c));
              ++charStart_;
            }
          }
        }
      }
    }

    if (revealed_ && !displayed_.empty()) {
      if (highlight_ < 0) highlight_ = 0;
      if (highlight_ >= static_cast<int>(displayed_.size())) {
        highlight_ = static_cast<int>(displayed_.size()) - 1;
      }
      result.highlight = highlight_;
    } else {
      highlight_ = 0;
    }

    result.candidates = displayed_;
    last_ = std::move(result);
  }

  EngineConfig config_;
  RimeSessionId sid_ = 0;
  SingleCharIndex* index_ = nullptr;

  Mode mode_ = Mode::kChineseEmpty;
  std::string buffer_;
  bool revealed_ = false;
  int highlight_ = 0;
  std::vector<Candidate> displayed_;
  std::size_t rimeCount_ = 0;
  // 分阶段选字（智能ABC 原味）：syllables_ 是完整切分，stage_ 是已选音节数，
  // picked_ 是已选的字。stage_ == 0 表示还在「词语 + 首音节单字」阶段。
  std::vector<std::string> syllables_;
  std::size_t stage_ = 0;
  int stagePage_ = 0;  // 分阶段选字时当前音节的单字页
  std::string picked_;
  // 词尽之后的「单字页」：charPage_ 0 = rime 的页，n>0 = 第 n 个单字页；
  // charStart_ = 已在 rime 最后一页用掉的单字个数。
  int charPage_ = 0;
  std::size_t charStart_ = 0;
  bool rimeLastPage_ = false;
  uint32_t rimeLastPageNo_ = 0;
  // 中文双引号方向：true = 下一对出 “（开引号）。
  bool quoteOpen_ = true;
  EngineResult last_;
};

// 进程级单例：会话共享的音节索引。
SingleCharIndex g_singleIndex;

}  // namespace

bool initializeRime(const RimeOptions& options) {
  std::lock_guard<std::mutex> lock(g_initMutex);
  if (g_initDone) return g_ready;
  g_initDone = true;

  if (api() == nullptr) return g_ready = false;

  g_options = options;
  RimeTraits traits;
  fillTraits(&traits, g_options);
  api()->setup(&traits);
  api()->initialize(&traits);
  // 不强制 full check：build/ 已就绪时几乎瞬时完成。
  api()->start_maintenance(0);
  api()->join_maintenance_thread();

  RimeSessionId probe = api()->create_session();
  if (probe == 0) return g_ready = false;
  api()->select_schema(probe, g_options.schemaId.c_str());
  g_singleIndex.setSession(probe);

  // librime 的 Service 静态析构遇到残留会话会段错误。
  // atexit 处理器比库的静态析构更早运行，在这里收尾最安全。
  std::atexit(&finalizeRime);

  g_ready = true;
  return true;
}

void finalizeRime() {
  std::lock_guard<std::mutex> lock(g_initMutex);
  if (!g_ready || api() == nullptr) return;
  // 先销毁我们持有的会话，再 finalize；否则 librime 退出清理会段错误。
  g_singleIndex.reset();
  api()->finalize();
  g_ready = false;
}

bool rimeReady() { return g_ready; }

std::unique_ptr<EngineSession> createRimeEngineSession(
    const EngineConfig& config) {
  if (!g_ready || api() == nullptr) return nullptr;
  RimeSessionId sid = api()->create_session();
  if (sid == 0) return nullptr;
  if (!api()->select_schema(sid, g_options.schemaId.c_str())) {
    api()->destroy_session(sid);
    return nullptr;
  }
  return std::unique_ptr<EngineSession>(
      new RimeEngineSession(config, sid, &g_singleIndex));
}

}  // namespace abc
