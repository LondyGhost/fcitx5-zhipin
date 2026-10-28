// A1 测试公共工具：枚举显示、快照断言、契约不变量检查（engine.md 4.1）、
// 按键辅助、字符不静默丢失核算探针。

#ifndef ABC_CORE_TEST_UTIL_HPP
#define ABC_CORE_TEST_UTIL_HPP

#include <string>
#include <vector>

#include "abc/engine.h"
#include "test_harness.hpp"

namespace abc_test {

using abc::EngineConfig;
using abc::EngineResult;
using abc::EngineSession;
using abc::KeyCommand;
using abc::KeyCode;
using abc::Mode;

inline std::string show(Mode m) {
  switch (m) {
    case Mode::kEnglish: return "kEnglish";
    case Mode::kChineseEmpty: return "kChineseEmpty";
    case Mode::kComposing: return "kComposing";
    case Mode::kSelecting: return "kSelecting";
  }
  return "?";
}
inline std::string show(KeyCode k) {
  switch (k) {
    case KeyCode::kUnknown: return "kUnknown";
    case KeyCode::kCharacter: return "kCharacter";
    case KeyCode::kDigit: return "kDigit";
    case KeyCode::kPunctuation: return "kPunctuation";
    case KeyCode::kBackspace: return "kBackspace";
    case KeyCode::kEnter: return "kEnter";
    case KeyCode::kSpace: return "kSpace";
    case KeyCode::kEscape: return "kEscape";
    case KeyCode::kPageUp: return "kPageUp";
    case KeyCode::kPageDown: return "kPageDown";
    case KeyCode::kModeSwitch: return "kModeSwitch";
  }
  return "?";
}

// 快照断言（不含 error/effects）。
inline void expectState(const EngineResult& r, Mode mode,
                        const std::string& preedit, uint32_t page,
                        uint32_t pageCount, std::size_t candidateCount,
                        int32_t highlight) {
  EXPECT_TRUE(r.mode == mode);
  EXPECT_EQ(r.preedit, preedit);
  EXPECT_EQ(r.page, page);
  EXPECT_EQ(r.pageCount, pageCount);
  EXPECT_EQ(r.candidates.size(), candidateCount);
  EXPECT_EQ(r.highlight, highlight);
}

// 契约 4.1 不变量检查（每次结果后均可调用）。
inline void checkInvariants(const EngineResult& r, const EngineConfig& cfg) {
  // 不变量 1：preedit 为空 ⟺ 英文/中文空。
  EXPECT_EQ(r.preedit.empty(),
            r.mode == Mode::kEnglish || r.mode == Mode::kChineseEmpty);
  // 不变量 3：无候选页 ⇒ candidates 空、page 0、highlight -1。
  if (r.pageCount == 0) {
    EXPECT_TRUE(r.candidates.empty());
    EXPECT_EQ(r.page, 0u);
    EXPECT_EQ(r.highlight, -1);
  } else {
    // 不变量 2：有候选页 ⇒ 0 < size <= pageSize。
    EXPECT_TRUE(!r.candidates.empty());
    EXPECT_TRUE(r.candidates.size() <= cfg.pageSize);
    // 不变量 4：Selecting ⇒ highlight 有效；Composing 且有候选 ⇒ highlight -1。
    if (r.mode == Mode::kSelecting) {
      EXPECT_TRUE(r.highlight >= 0);
      EXPECT_TRUE(static_cast<std::size_t>(r.highlight) < r.candidates.size());
    } else if (r.mode == Mode::kComposing) {
      EXPECT_EQ(r.highlight, -1);
    }
  }
  // 不变量 5：出现 kCommitText ⇒ 组合已清空。
  for (const abc::Effect& e : r.effects) {
    if (e.kind == abc::Effect::Kind::kCommitText) {
      EXPECT_EQ(r.preedit, "");
      EXPECT_EQ(r.pageCount, 0u);
      EXPECT_TRUE(r.mode == Mode::kChineseEmpty || r.mode == Mode::kEnglish);
    }
  }
  // 不变量 6（无 effects 的未消费键）：快照不变由调用方对比（见 Probe）。
  (void)cfg;
}

// 依次输入一串 ASCII 字母（kCharacter 键）。
inline EngineResult typeLetters(EngineSession& s, const std::string& letters) {
  EngineResult r;
  for (char c : letters) {
    KeyCommand k;
    k.code = KeyCode::kCharacter;
    k.unicode = static_cast<char32_t>(static_cast<unsigned char>(c));
    r = s.handle(k);
  }
  return r;
}

inline KeyCommand letter(char32_t c) {
  KeyCommand k;
  k.code = KeyCode::kCharacter;
  k.unicode = c;
  return k;
}
inline KeyCommand digit(uint8_t d) {
  KeyCommand k;
  k.code = KeyCode::kDigit;
  k.digit = d;
  return k;
}
inline KeyCommand punct(char32_t c) {
  KeyCommand k;
  k.code = KeyCode::kPunctuation;
  k.unicode = c;
  return k;
}

inline bool isLetterUnicode(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

// 字符不静默丢失核算（契约 4.1 不变量 7）：每步之后
//   typed == committedLetters + 当前 preedit 长度 + removed + forwarded
// committedLetters 在提交效果发生时记入提交前 preedit 长度（无论提交的是
// 原文还是候选——编码字符都随选择进入提交）；removed 统计 Esc/退格/reset/
// 切换策略 Clear 等明确取消的字符；forwarded 统计未消费而转发的字母键
// （英文态、缓冲溢出）。
struct Probe {
  EngineSession* s = nullptr;
  std::string committedText;
  long typed = 0;
  long committedLetters = 0;
  long removed = 0;
  long forwarded = 0;

  explicit Probe(EngineSession& session) : s(&session) {}

  EngineResult step(const KeyCommand& k) {
    const EngineResult before = s->snapshot();
    const std::size_t lenBefore = before.preedit.size();
    const EngineResult r = s->handle(k);

    if (k.code == KeyCode::kCharacter && isLetterUnicode(k.unicode)) {
      ++typed;
    }

    std::size_t commitCount = 0;
    for (const abc::Effect& e : r.effects) {
      if (e.kind == abc::Effect::Kind::kCommitText) {
        ++commitCount;
        committedText += e.text;
      }
    }
    if (commitCount > 0) {
      committedLetters += static_cast<long>(lenBefore);
    }

    if (k.code == KeyCode::kCharacter && isLetterUnicode(k.unicode) &&
        !r.consumed) {
      ++forwarded;
    }

    // 离开 preedit 的字符 = 提交吸收的 + 明确删除的；为负（新字符并入）不计。
    const long left = static_cast<long>(lenBefore) -
                      static_cast<long>(r.preedit.size());
    if (left > 0) {
      removed += left - static_cast<long>(commitCount) *
                                static_cast<long>(lenBefore);
    }

    check(r);
    // 未消费且无 effects 的按键：快照必须与按键前一致（不变量 6 无副作用面）。
    if (!r.consumed && r.effects.empty()) {
      EXPECT_TRUE(r.mode == before.mode);
      EXPECT_EQ(r.preedit, before.preedit);
      EXPECT_EQ(r.page, before.page);
      EXPECT_EQ(r.pageCount, before.pageCount);
      EXPECT_EQ(r.highlight, before.highlight);
    }
    checkInvariants(r, s->config());
    return r;
  }

  void check(const EngineResult& r) const {
    const long accounted = committedLetters +
                           static_cast<long>(r.preedit.size()) + removed +
                           forwarded;
    EXPECT_EQ(typed, accounted);
  }
};

}  // namespace abc_test

#endif  // ABC_CORE_TEST_UTIL_HPP
