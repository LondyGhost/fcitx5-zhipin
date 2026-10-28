// A1 输入状态机核心 — EngineSession 实现。
//
// 契约：docs/interfaces/engine.md v1.0.0（rc1）、docs/interfaces/dictionary.md（Dictionary 抽象）。
// 本文件是 source/core 的全部实现：纯逻辑，仅依赖 abc 公共头与标准库，
// 不含 fcitx5/文件系统/日志/静态可变状态。缓冲与组合状态仅由会话对象持有。
//
// 转移表实现对应 engine.md 第 5 节；表内未覆盖的细节按 4.3（未处理键策略）、
// 5.5（lookup 时机与错误）执行。实现对 A0 的两点澄清请求见 docs/modules/core.md。

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "abc/engine.h"

namespace abc {
namespace {

inline bool isAsciiLetter(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

inline char32_t toLowerAscii(char32_t c) {
  return (c >= U'A' && c <= U'Z') ? c + (U'a' - U'A') : c;
}

// engine.md 2.2 命令字段合法性。
bool commandFieldsValid(const KeyCommand& cmd) {
  switch (cmd.code) {
    case KeyCode::kCharacter:
    case KeyCode::kPunctuation:
      return cmd.unicode != 0;
    case KeyCode::kDigit:
      return cmd.digit >= 1 && cmd.digit <= 9;
    default:
      return true;  // 其余键码忽略附加字段
  }
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

class EngineSessionImpl final : public EngineSession {
 public:
  EngineSessionImpl(const EngineConfig& config, Dictionary* dictionary)
      : config_(config), dictionary_(dictionary) {
    // 工厂已校验 initialMode ∈ {kEnglish, kChineseEmpty}。
    mode_ = config_.initialMode;
  }

  EngineResult handle(const KeyCommand& command) override {
    if (!commandFieldsValid(command)) {
      // engine.md 8.2：字段非法 → 不消费、状态不变、键交还 fcitx5。
      return makeResult({}, false, EngineError::kInvalidCommand);
    }
    std::vector<Effect> effects;
    bool consumed = false;
    dispatch(command, effects, consumed);
    return makeResult(std::move(effects), consumed, lastError_);
  }

  EngineResult reset() override {
    // engine.md 6：清空组合不提交，mode = config.initialMode；
    // 快照本身即清空语义（preedit 为空），effects 为空。
    clearComposition();
    mode_ = config_.initialMode;
    return makeResult({}, false, EngineError::kNone);
  }

  EngineResult snapshot() const override {
    return makeResult({}, false, EngineError::kNone);
  }

  const EngineConfig& config() const override { return config_; }

 private:
  // ---- 状态转移（engine.md 第 5 节）----

  void dispatch(const KeyCommand& cmd, std::vector<Effect>& effects,
                bool& consumed) {
    switch (mode_) {
      case Mode::kEnglish:
        dispatchEnglish(cmd, effects, consumed);
        break;
      case Mode::kChineseEmpty:
        dispatchChineseEmpty(cmd, effects, consumed);
        break;
      case Mode::kComposing:
        dispatchComposing(cmd, effects, consumed);
        break;
      case Mode::kSelecting:
        dispatchSelecting(cmd, effects, consumed);
        break;
    }
  }

  // 5.1 English：除 kModeSwitch 外一律不消费。
  void dispatchEnglish(const KeyCommand& cmd, std::vector<Effect>& effects,
                       bool& consumed) {
    (void)effects;
    if (cmd.code == KeyCode::kModeSwitch) {
      mode_ = Mode::kChineseEmpty;
      consumed = true;
    }
  }

  // 5.2 ChineseEmpty：仅字母进入组合与 kModeSwitch 被消费。
  void dispatchChineseEmpty(const KeyCommand& cmd, std::vector<Effect>& effects,
                            bool& consumed) {
    (void)effects;
    switch (cmd.code) {
      case KeyCode::kCharacter:
        if (isAsciiLetter(cmd.unicode)) {
          buffer_.push_back(static_cast<char>(toLowerAscii(cmd.unicode)));
          mode_ = Mode::kComposing;
          page_ = 0;
          highlight_ = -1;
          lookupAt(0);
          consumed = true;
        }
        // 非字母按 kPunctuation 处理 → 不消费、无变化。
        break;
      case KeyCode::kModeSwitch:
        mode_ = Mode::kEnglish;
        consumed = true;
        break;
      default:
        break;  // 不消费，原样转发
    }
  }

  // 5.3 Composing：候选页预览（highlight = -1），数字与翻页按未处理键。
  void dispatchComposing(const KeyCommand& cmd, std::vector<Effect>& effects,
                         bool& consumed) {
    switch (cmd.code) {
      case KeyCode::kCharacter:
        if (isAsciiLetter(cmd.unicode)) {
          if (!appendLetter(cmd.unicode)) {
            break;  // 溢出：不入缓冲、不消费、状态不变（字符原样转发）。
          }
          page_ = 0;
          highlight_ = -1;
          lookupAt(0);
          consumed = true;
          break;
        }
        nonHandledKey(effects, consumed);  // 非字母降级为标点
        break;
      case KeyCode::kPunctuation:
        nonHandledKey(effects, consumed);
        break;
      case KeyCode::kBackspace:
        buffer_.pop_back();
        if (buffer_.empty()) {
          clearComposition();
        } else {
          page_ = 0;
          highlight_ = -1;
          lookupAt(0);
        }
        consumed = true;
        break;
      case KeyCode::kEnter:
        // 提交原文（规范化缓冲），不提交猜测候选。
        effects.push_back(commitTextEffect(buffer_));
        clearComposition();
        consumed = true;
        break;
      case KeyCode::kEscape:
        effects.push_back(clearCompositionEffect());
        clearComposition();
        consumed = true;
        break;
      case KeyCode::kSpace:
        if (!candidates_.empty()) {
          mode_ = Mode::kSelecting;
          highlight_ = 0;  // 页不变（Composing 恒为第 0 页）
          consumed = true;
        } else if (config_.spaceNoCandidatePolicy ==
                   SpaceNoCandidatePolicy::kCommitRawAndForward) {
          effects.push_back(commitTextEffect(buffer_));
          clearComposition();
          consumed = false;  // 空格仍转发给应用
        }
        // kKeepBufferAndForward：保留组合，不消费。
        break;
      case KeyCode::kDigit:
      case KeyCode::kPageUp:
      case KeyCode::kPageDown:
      case KeyCode::kUnknown:
        nonHandledKey(effects, consumed);
        break;
      case KeyCode::kModeSwitch:
        applyModeSwitch(effects);
        consumed = true;
        break;
    }
  }

  // 5.4 Selecting：候选页激活（highlight ≥ 0）。
  void dispatchSelecting(const KeyCommand& cmd, std::vector<Effect>& effects,
                         bool& consumed) {
    switch (cmd.code) {
      case KeyCode::kDigit: {
        const std::size_t idx = static_cast<std::size_t>(cmd.digit - 1);
        if (idx < candidates_.size()) {
          // 与高亮位置无关：数字直接索引页内第 d 项。
          effects.push_back(commitTextEffect(candidates_[idx].text));
          clearComposition();
          consumed = true;
        }
        // 页内无对应项：不消费、状态不变、键转发（缓冲保留以便继续编辑）。
        break;
      }
      case KeyCode::kSpace:
        // 提交当前高亮候选（v1 高亮恒为页首）。
        if (highlight_ >= 0 &&
            static_cast<std::size_t>(highlight_) < candidates_.size()) {
          effects.push_back(commitTextEffect(
              candidates_[static_cast<std::size_t>(highlight_)].text));
          clearComposition();
        }
        consumed = true;
        break;
      case KeyCode::kCharacter:
        if (isAsciiLetter(cmd.unicode)) {
          if (!appendLetter(cmd.unicode)) {
            break;  // 溢出策略同 Composing。
          }
          mode_ = Mode::kComposing;  // 回到预览态（无候选的 Selecting 不存在）
          page_ = 0;
          highlight_ = -1;
          lookupAt(0);
          consumed = true;
          break;
        }
        nonHandledKey(effects, consumed);
        break;
      case KeyCode::kPunctuation:
        nonHandledKey(effects, consumed);
        break;
      case KeyCode::kBackspace:
        buffer_.pop_back();
        if (buffer_.empty()) {
          clearComposition();
        } else {
          mode_ = Mode::kComposing;
          page_ = 0;
          highlight_ = -1;
          lookupAt(0);
        }
        consumed = true;
        break;
      case KeyCode::kPageUp:
        if (page_ > 0 && pageOffset(page_ - 1) <= kMaxOffset) {
          --page_;
          highlight_ = 0;
          lookupAt(pageOffset(page_));
        }
        consumed = true;  // 首页边界：消费但无变化
        break;
      case KeyCode::kPageDown:
        if (page_ + 1 < pageCount_ && pageOffset(page_ + 1) <= kMaxOffset) {
          ++page_;
          highlight_ = 0;
          lookupAt(pageOffset(page_));
        }
        consumed = true;  // 末页边界：消费但无变化
        break;
      case KeyCode::kEnter:
        effects.push_back(commitTextEffect(buffer_));  // 原文，非候选
        clearComposition();
        consumed = true;
        break;
      case KeyCode::kEscape:
        effects.push_back(clearCompositionEffect());
        clearComposition();
        consumed = true;
        break;
      case KeyCode::kUnknown:
        nonHandledKey(effects, consumed);
        break;
      case KeyCode::kModeSwitch:
        applyModeSwitch(effects);
        consumed = true;
        break;
    }
  }

  // 4.3 未处理键策略（冻结）：提交原文后转发；键本身不消费。
  void nonHandledKey(std::vector<Effect>& effects, bool& consumed) {
    if (!buffer_.empty()) {
      effects.push_back(commitTextEffect(buffer_));
      clearComposition();
    }
    consumed = false;
  }

  // 5.3/5.4 kModeSwitch：按策略处理缓冲后切至英文。
  void applyModeSwitch(std::vector<Effect>& effects) {
    if (!buffer_.empty()) {
      if (config_.modeSwitchCompositionPolicy ==
          ModeSwitchCompositionPolicy::kCommitRaw) {
        effects.push_back(commitTextEffect(buffer_));
      } else {
        effects.push_back(clearCompositionEffect());  // 显式取消，非静默丢失
      }
    }
    clearComposition();
    mode_ = Mode::kEnglish;
  }

  // ---- 缓冲与查询 ----

  // 尝试并入字母；超过 maxBufferChars 时不修改状态并返回 false（溢出策略）。
  bool appendLetter(char32_t ch) {
    if (buffer_.size() >= static_cast<std::size_t>(config_.maxBufferChars)) {
      return false;
    }
    buffer_.push_back(static_cast<char>(toLowerAscii(ch)));
    return true;
  }

  // 5.5 lookup 时机：prefix = 缓冲，limit = pageSize，offset 由调用方给出。
  // 结果同时维护 pageCount/highlight 及不变量 2/3/4。
  void lookupAt(uint32_t offset) {
    candidates_.clear();
    page_ = 0;
    pageCount_ = 0;
    highlight_ = -1;
    if (buffer_.empty() || dictionary_ == nullptr) {
      return;
    }
    const LookupResult lr =
        dictionary_->lookup(buffer_, config_.pageSize, offset);
    if (lr.status != DictionaryStatus::kOk) {
      // 5.5：lookup 失败按无候选分支处理，缓冲与组合保留，不重试。
      lastError_ = EngineError::kDictionaryError;
      if (mode_ == Mode::kSelecting) {
        mode_ = Mode::kComposing;  // 不变量 4：无候选的 Selecting 不存在
      }
      return;
    }
    lastError_ = EngineError::kNone;
    if (lr.batch.items.empty()) {
      // 防御：total>0 但本页为空等词典不一致情形同样回落预览态。
      if (mode_ == Mode::kSelecting) {
        mode_ = Mode::kComposing;
      }
      return;
    }
    candidates_ = lr.batch.items;
    page_ = offset / config_.pageSize;
    const uint64_t total = lr.batch.total;
    const uint64_t pages =
        (total + config_.pageSize - 1) / config_.pageSize;  // ceil
    pageCount_ = static_cast<uint32_t>(
        std::min<uint64_t>(pages, std::numeric_limits<uint32_t>::max()));
    highlight_ = (mode_ == Mode::kSelecting) ? 0 : -1;
  }

  uint64_t pageOffset(uint32_t page) const {
    return static_cast<uint64_t>(page) * config_.pageSize;
  }

  static constexpr uint64_t kMaxOffset = 0xFFFFFFFFu;  // lookup 的 uint32 上限

  void clearComposition() {
    buffer_.clear();
    mode_ = Mode::kChineseEmpty;
    page_ = 0;
    pageCount_ = 0;
    highlight_ = -1;
    candidates_.clear();
  }

  EngineResult makeResult(std::vector<Effect> effects, bool consumed,
                          EngineError error) const {
    EngineResult r;
    r.consumed = consumed;
    r.mode = mode_;
    r.preedit = buffer_;
    r.candidates = candidates_;
    r.page = page_;
    r.pageCount = pageCount_;
    r.highlight = highlight_;
    r.effects = std::move(effects);
    r.error = error;
    return r;
  }

  EngineConfig config_;
  Dictionary* dictionary_;  // 借用，不拥有；生命期须覆盖会话

  Mode mode_ = Mode::kChineseEmpty;
  std::string buffer_;  // 规范化小写字母缓冲；仅本会话持有
  std::vector<Candidate> candidates_;  // 当前页候选（最近一次 lookup 结果）
  uint32_t page_ = 0;
  uint32_t pageCount_ = 0;
  int32_t highlight_ = -1;
  EngineError lastError_ = EngineError::kNone;  // 本次 handle 内最近 lookup 状态
};

}  // namespace

std::unique_ptr<EngineSession> createEngineSession(const EngineConfig& config,
                                                   Dictionary* dictionary) {
  // engine.md 第 3、6 节工厂校验；词典为空按防御性拒绝处理。
  if (dictionary == nullptr) {
    return nullptr;
  }
  if (config.pageSize < 1 || config.pageSize > kMaxPageSize) {
    return nullptr;
  }
  if (config.maxBufferChars < 1 ||
      config.maxBufferChars > kAbsoluteMaxBufferChars) {
    return nullptr;
  }
  if (config.initialMode != Mode::kEnglish &&
      config.initialMode != Mode::kChineseEmpty) {
    return nullptr;
  }
  if (config.spaceNoCandidatePolicy != SpaceNoCandidatePolicy::kCommitRawAndForward &&
      config.spaceNoCandidatePolicy != SpaceNoCandidatePolicy::kKeepBufferAndForward) {
    return nullptr;
  }
  if (config.modeSwitchCompositionPolicy !=
          ModeSwitchCompositionPolicy::kCommitRaw &&
      config.modeSwitchCompositionPolicy !=
          ModeSwitchCompositionPolicy::kClearComposition) {
    return nullptr;
  }
  return std::unique_ptr<EngineSession>(
      new EngineSessionImpl(config, dictionary));
}

}  // namespace abc
