// A1 测试：中英文模式、英文态转发、未处理键策略（标点/未知键/Composing 下的
// 数字与翻页键）、kModeSwitch 组合策略选项。

#include <memory>
#include <string>

#include "fake_dictionary.hpp"
#include "test_util.hpp"

namespace {

using abc::EngineConfig;
using abc::EngineError;
using abc::Mode;
using abc::SpaceNoCandidatePolicy;
using abc_test::FakeDictionary;

std::unique_ptr<abc::EngineSession> makeSession(FakeDictionary& dict,
                                                const EngineConfig& cfg = {}) {
  return abc::createEngineSession(cfg, &dict);
}

void populateNi(FakeDictionary& dict) {
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  dict.add("nihao", "你好", 20);
}

ABC_TEST(EnglishForwardsEverythingExceptModeSwitch) {
  FakeDictionary dict;
  populateNi(dict);
  EngineConfig cfg;
  cfg.initialMode = abc::Mode::kEnglish;
  auto s = makeSession(dict, cfg);
  const abc::KeyCommand keys[] = {
      abc_test::letter(U'n'),   abc_test::letter(U'N'),
      abc_test::digit(5),       abc_test::punct(U','),
      abc::KeyCommand{abc::KeyCode::kEnter},
      abc::KeyCommand{abc::KeyCode::kBackspace},
      abc::KeyCommand{abc::KeyCode::kEscape},
      abc::KeyCommand{abc::KeyCode::kSpace},
      abc::KeyCommand{abc::KeyCode::kPageUp},
      abc::KeyCommand{abc::KeyCode::kPageDown},
      abc::KeyCommand{abc::KeyCode::kUnknown, 0xABCD, 0},
  };
  for (const abc::KeyCommand& k : keys) {
    const abc::EngineResult r = s->handle(k);
    EXPECT_FALSE(r.consumed);
    abc_test::expectState(r, Mode::kEnglish, "", 0, 0, 0, -1);
    EXPECT_TRUE(r.effects.empty());
  }
  EXPECT_TRUE(dict.calls().empty());  // 英文态不查询词典
  // 切换热键是唯一被消费的键。
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_TRUE(r.effects.empty());
}

ABC_TEST(ModeSwitchRoundTripResumesChinese) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  const abc::EngineResult r = abc_test::typeLetters(*s, "ni");
  EXPECT_TRUE(r.consumed);
  // 前缀 "ni"：精确组(你,泥) + "nihao" 组(你好) = 3 条。
  abc_test::expectState(r, Mode::kComposing, "ni", 0, 1, 3, -1);
}

ABC_TEST(ModeSwitchWhileComposingDefaultCommitsRaw) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_TRUE(r.effects[0].kind == abc::Effect::Kind::kCommitText);
  EXPECT_EQ(r.effects[0].text, "nihao");  // 默认：切换前提交原文，数据不丢
  abc_test::expectState(r, Mode::kEnglish, "", 0, 0, 0, -1);
  // 切换后字母转发给应用。
  const abc::EngineResult r2 = s->handle(abc_test::letter(U'x'));
  EXPECT_FALSE(r2.consumed);
}

ABC_TEST(ModeSwitchWhileComposingClearPolicy) {
  FakeDictionary dict;
  populateNi(dict);
  EngineConfig cfg;
  cfg.modeSwitchCompositionPolicy = abc::ModeSwitchCompositionPolicy::kClearComposition;
  auto s = makeSession(dict, cfg);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_TRUE(r.effects[0].kind == abc::Effect::Kind::kClearComposition);
  abc_test::expectState(r, Mode::kEnglish, "", 0, 0, 0, -1);
}

ABC_TEST(ModeSwitchInSelectingCommitsRaw) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});  // Selecting
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  EXPECT_EQ(r.effects[0].text, "ni");  // 提交原文而非候选
  abc_test::expectState(r, Mode::kEnglish, "", 0, 0, 0, -1);
}

ABC_TEST(ModeSwitchInChineseEmptyConsumedWithoutEffects) {
  FakeDictionary dict;
  auto s = makeSession(dict);
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kModeSwitch});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kEnglish, "", 0, 0, 0, -1);
  EXPECT_TRUE(r.effects.empty());
}

ABC_TEST(PunctuationInComposingCommitsRawAndForwards) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc_test::punct(U','));
  EXPECT_FALSE(r.consumed);            // 标点转发给应用
  EXPECT_EQ(r.effects.size(), 1u);     // 原文先行提交，不静默丢字符
  EXPECT_EQ(r.effects[0].text, "nihao");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(PunctuationInSelectingCommitsRawAndForwards) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  const abc::EngineResult r = s->handle(abc_test::punct(U'。'));
  EXPECT_FALSE(r.consumed);
  EXPECT_EQ(r.effects[0].text, "ni");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(UnknownKeyInComposingCommitsRawAndForwards) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r =
      s->handle(abc::KeyCommand{abc::KeyCode::kUnknown, 0x007F, 0});
  EXPECT_FALSE(r.consumed);
  EXPECT_EQ(r.effects[0].text, "nihao");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(UnknownKeyInChineseEmptyForwardsWithoutCommit) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  const abc::EngineResult r =
      s->handle(abc::KeyCommand{abc::KeyCode::kUnknown, 0x007F, 0});
  EXPECT_FALSE(r.consumed);
  EXPECT_TRUE(r.effects.empty());  // 无组合可结束，原样转发
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(DigitInComposingIsNonHandledKey) {
  // 冻结转移表 5.3：数字选择仅在 Selecting 定义；Composing 下按未处理键策略。
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc_test::digit(2));
  EXPECT_FALSE(r.consumed);            // 数字本身转发给应用
  EXPECT_EQ(r.effects[0].text, "nihao");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(PageKeyInComposingIsNonHandledKey) {
  // 冻结转移表 5.3：翻页仅在 Selecting 定义；Composing 下按未处理键策略。
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  EXPECT_FALSE(r.consumed);
  EXPECT_EQ(r.effects[0].text, "nihao");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(SpacePoliciesBothLossless) {
  // 两种无候选空格策略都不丢字符：CommitRawAndForward 进提交；
  // KeepBufferAndForward 留在 preedit（空格均转发）。
  {
    FakeDictionary dict;
    EngineConfig cfg;
    cfg.spaceNoCandidatePolicy = SpaceNoCandidatePolicy::kCommitRawAndForward;
    auto s = makeSession(dict, cfg);
    abc_test::Probe probe(*s);
    for (char c : std::string("zz")) probe.step(abc_test::letter(U'a' + (c - 'a')));
    probe.step(abc::KeyCommand{abc::KeyCode::kSpace});
    EXPECT_EQ(probe.committedText, "zz");
  }
  {
    FakeDictionary dict;
    EngineConfig cfg;
    cfg.spaceNoCandidatePolicy = SpaceNoCandidatePolicy::kKeepBufferAndForward;
    auto s = makeSession(dict, cfg);
    abc_test::Probe probe(*s);
    for (char c : std::string("zz")) probe.step(abc_test::letter(U'a' + (c - 'a')));
    probe.step(abc::KeyCommand{abc::KeyCode::kSpace});
    EXPECT_EQ(s->snapshot().preedit, "zz");
    probe.step(abc::KeyCommand{abc::KeyCode::kEscape});  // 显式取消
  }
}

}  // namespace
