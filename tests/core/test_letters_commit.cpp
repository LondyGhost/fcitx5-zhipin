// A1 测试：字母输入、大小写规范化、Enter 原文提交、Backspace、Esc、
// 缓冲上限溢出、ChineseEmpty 态转发与词典查询形态。

#include <memory>
#include <string>

#include "fake_dictionary.hpp"
#include "test_util.hpp"

namespace {

using abc::EngineConfig;
using abc::EngineError;
using abc::Mode;
using abc_test::FakeDictionary;

std::unique_ptr<abc::EngineSession> makeSession(FakeDictionary& dict,
                                                const EngineConfig& cfg = {}) {
  return abc::createEngineSession(cfg, &dict);
}

void populateNi(FakeDictionary& dict) {
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  dict.add("ni", "呢", 3);
  dict.add("nihao", "你好", 20);
  dict.add("nihao", "你号", 8);
  dict.add("nihao", "尼豪", 2);
}

ABC_TEST(InitialSnapshotIsChineseEmpty) {
  FakeDictionary dict;
  auto s = makeSession(dict);
  abc_test::expectState(s->snapshot(), Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_TRUE(s->snapshot().effects.empty());
  EXPECT_TRUE(s->snapshot().error == abc::EngineError::kNone);
}

ABC_TEST(InitialModeEnglishRespected) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.initialMode = abc::Mode::kEnglish;
  auto s = makeSession(dict, cfg);
  abc_test::expectState(s->snapshot(), Mode::kEnglish, "", 0, 0, 0, -1);
}

ABC_TEST(LetterEntersComposingWithFrozenOrdering) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  const abc::EngineResult r = s->handle(abc_test::letter(U'n'));
  EXPECT_TRUE(r.consumed);
  // 前缀 "n"：精确组无 → code "ni" 组(你100,泥10,呢3) → "nihao" 组(你好20,你号8,尼豪2)。
  abc_test::expectState(r, Mode::kComposing, "n", 0, 1, 6, -1);
  EXPECT_EQ(r.candidates[0].text, "你");
  EXPECT_EQ(r.candidates[2].text, "呢");
  EXPECT_EQ(r.candidates[3].text, "你好");
  EXPECT_EQ(r.candidates[5].text, "尼豪");
  EXPECT_FALSE(dict.calls().empty());
  EXPECT_EQ(dict.calls().back().prefix, "n");
  EXPECT_EQ(dict.calls().back().limit, 9u);
  EXPECT_EQ(dict.calls().back().offset, 0u);
}

ABC_TEST(TypingNormalizesCaseIntoBuffer) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  const abc::EngineResult r = abc_test::typeLetters(*s, "NIHAO");
  abc_test::expectState(r, Mode::kComposing, "nihao", 0, 1, 3, -1);
  // 每次字母触发一次 offset=0 的查询；前缀逐字符增长且全为小写。
  EXPECT_FALSE(dict.calls().empty());
  EXPECT_EQ(dict.calls().back().prefix, "nihao");
  for (const auto& call : dict.calls()) {
    EXPECT_FALSE(call.prefix.empty());
    EXPECT_EQ(call.limit, 9u);
    EXPECT_EQ(call.offset, 0u);
    for (char c : call.prefix) {
      EXPECT_TRUE(c >= 'a' && c <= 'z');
    }
  }
}

ABC_TEST(EnterCommitsRawNormalizedBufferNotCandidate) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult re = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});
  EXPECT_TRUE(re.consumed);
  abc_test::expectState(re, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_EQ(re.effects.size(), 1u);
  EXPECT_TRUE(re.effects[0].kind == abc::Effect::Kind::kCommitText);
  EXPECT_EQ(re.effects[0].text, "nihao");  // 原始缓冲，而非“你好”
}

ABC_TEST(EnterAfterUppercaseCommitsLowerCase) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "AbC");
  EXPECT_EQ(s->snapshot().preedit, "abc");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});
  EXPECT_EQ(r.effects[0].text, "abc");
}

ABC_TEST(ChineseEmptyForwardsAllNonLetterKeys) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  const abc::KeyCommand keys[] = {
      abc::KeyCommand{abc::KeyCode::kPunctuation, U',', 0},
      abc::KeyCommand{abc::KeyCode::kSpace},
      abc::KeyCommand{abc::KeyCode::kDigit, 0, 5},
      abc::KeyCommand{abc::KeyCode::kBackspace},
      abc::KeyCommand{abc::KeyCode::kEnter},
      abc::KeyCommand{abc::KeyCode::kEscape},
      abc::KeyCommand{abc::KeyCode::kPageUp},
      abc::KeyCommand{abc::KeyCode::kPageDown},
      abc::KeyCommand{abc::KeyCode::kUnknown, 0x7F, 0},
  };
  for (const abc::KeyCommand& k : keys) {
    const abc::EngineResult r = s->handle(k);
    EXPECT_FALSE(r.consumed);
    abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
    EXPECT_TRUE(r.effects.empty());
    EXPECT_TRUE(r.error == abc::EngineError::kNone);
  }
}

ABC_TEST(BackspaceDeletesLastCharAndRefreshes) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  EXPECT_TRUE(r.consumed);
  // "niha" 仍是 "nihao" 前缀 ⇒ 3 条候选。
  abc_test::expectState(r, Mode::kComposing, "niha", 0, 1, 3, -1);
  EXPECT_EQ(dict.calls().back().prefix, "niha");
}

ABC_TEST(BackspaceToEmptyReturnsToChineseEmpty) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  abc_test::expectState(r, Mode::kComposing, "n", 0, 1, 6, -1);
  r = s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  // 空缓冲后再退格：转发给应用（删除应用已有文本），不消费。
  r = s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  EXPECT_FALSE(r.consumed);
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(EscClearsWithClearCompositionEffect) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kEscape});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_TRUE(r.effects[0].kind == abc::Effect::Kind::kClearComposition);
  EXPECT_TRUE(r.effects[0].text.empty());  // 无提交
  // 取消后从零开始。
  const abc::EngineResult r2 = s->handle(abc_test::letter(U'n'));
  EXPECT_EQ(r2.preedit, "n");
}

ABC_TEST(EscInChineseEmptyForwards) {
  FakeDictionary dict;
  auto s = makeSession(dict);
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kEscape});
  EXPECT_FALSE(r.consumed);
}

ABC_TEST(BufferOverflowForwardsLettersWithoutLoss) {
  FakeDictionary dict;  // 空词典：无候选
  auto s = makeSession(dict);  // 默认 maxBufferChars = 64
  abc::EngineResult r;
  for (int i = 0; i < 70; ++i) {
    r = s->handle(abc_test::letter(U'x'));
    if (i < 64) {
      EXPECT_TRUE(r.consumed);
      EXPECT_EQ(r.preedit.size(), static_cast<std::size_t>(i + 1));
    } else {
      // 超限：不入缓冲、不消费、状态不变；字符原样转发给应用，不无声丢失。
      EXPECT_FALSE(r.consumed);
      EXPECT_EQ(r.preedit.size(), 64u);
    }
  }
  const abc::EngineResult commit = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});
  EXPECT_EQ(commit.effects.size(), 1u);
  EXPECT_EQ(commit.effects[0].text.size(), 64u);  // 64 个 x 全部提交
}

ABC_TEST(OverflowWithCustomLimit) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.maxBufferChars = 3;
  auto s = makeSession(dict, cfg);
  abc::EngineResult r = s->handle(abc_test::letter(U'a'));
  r = s->handle(abc_test::letter(U'b'));
  r = s->handle(abc_test::letter(U'c'));
  abc_test::expectState(r, Mode::kComposing, "abc", 0, 0, 0, -1);
  r = s->handle(abc_test::letter(U'd'));
  EXPECT_FALSE(r.consumed);  // 'd' 转发，不无声丢失
  EXPECT_EQ(s->snapshot().preedit, "abc");
}

ABC_TEST(SpaceNoCandidateDefaultCommitsRawAndForwardsSpace) {
  FakeDictionary dict;
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "xyz");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_FALSE(r.consumed);  // 冻结策略：不消费 + 提交原文 + 空格转发
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_EQ(r.effects[0].text, "xyz");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(SpaceNoCandidateKeepBufferAndForwardOption) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.spaceNoCandidatePolicy = abc::SpaceNoCandidatePolicy::kKeepBufferAndForward;
  auto s = makeSession(dict, cfg);
  abc_test::typeLetters(*s, "xyz");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_FALSE(r.consumed);
  EXPECT_TRUE(r.effects.empty());  // 保留组合，可继续编辑
  abc_test::expectState(r, Mode::kComposing, "xyz", 0, 0, 0, -1);
}

ABC_TEST(NonLetterCharacterDegeneratesToPunctuation) {
  // kCharacter 携带非字母码点：核心按 kPunctuation 对待（契约 2.1 宽容降级）。
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  // ChineseEmpty：不消费、无变化。
  abc::EngineResult r = s->handle(abc_test::letter(U'，'));
  EXPECT_FALSE(r.consumed);
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  // Composing：未处理键策略 → 提交原文并转发。
  abc_test::typeLetters(*s, "ni");
  r = s->handle(abc_test::letter(U'，'));
  EXPECT_FALSE(r.consumed);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_EQ(r.effects[0].text, "ni");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(NoEmptyPrefixQueries) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  s->handle(abc::KeyCommand{abc::KeyCode::kEscape});
  s->handle(abc_test::letter(U'n'));
  for (const auto& call : dict.calls()) {
    EXPECT_FALSE(call.prefix.empty());  // 空态绝不查询词典
    EXPECT_TRUE(call.limit > 0);
  }
}

}  // namespace
