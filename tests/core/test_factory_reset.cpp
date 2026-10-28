// A1 测试：会话工厂校验、reset()、非法命令字段（EngineError::kInvalidCommand）。

#include <memory>

#include "fake_dictionary.hpp"
#include "test_util.hpp"

namespace {

using abc::EngineConfig;
using abc::EngineError;
using abc::Mode;
using abc_test::FakeDictionary;

void populateNi(FakeDictionary& dict) {
  dict.add("ni", "你", 100);
  dict.add("nihao", "你好", 20);
}

ABC_TEST(FactoryRejectsNullDictionary) {
  EngineConfig cfg;
  EXPECT_TRUE(abc::createEngineSession(cfg, nullptr) == nullptr);
}

ABC_TEST(FactoryRejectsPageSizeOutOfRange) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.pageSize = 0;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
  cfg.pageSize = 10;  // 上限 9：数字 1..9 必须能索引页内元素
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
  cfg.pageSize = 9;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) != nullptr);
  cfg.pageSize = 1;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) != nullptr);
}

ABC_TEST(FactoryRejectsMaxBufferOutOfRange) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.maxBufferChars = 0;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
  cfg.maxBufferChars = 257;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
  cfg.maxBufferChars = 256;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) != nullptr);
  cfg.maxBufferChars = 1;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) != nullptr);
}

ABC_TEST(FactoryRejectsCompositionInitialMode) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.initialMode = abc::Mode::kComposing;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
  cfg.initialMode = abc::Mode::kSelecting;
  EXPECT_TRUE(abc::createEngineSession(cfg, &dict) == nullptr);
}

ABC_TEST(FactoryAcceptsDefaults) {
  FakeDictionary dict;
  EngineConfig cfg;  // 默认值必须可用（pageSize=9, maxBufferChars=64）
  auto s = abc::createEngineSession(cfg, &dict);
  EXPECT_TRUE(s != nullptr);
  EXPECT_TRUE(s->config().pageSize == 9);
  EXPECT_TRUE(s->config().maxBufferChars == 64);
  EXPECT_TRUE(s->config().initialMode == Mode::kChineseEmpty);
}

ABC_TEST(ResetClearsCompositionWithoutCommit) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = abc::createEngineSession(EngineConfig{}, &dict);
  abc_test::typeLetters(*s, "nihao");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});  // Selecting
  const abc::EngineResult r = s->reset();
  EXPECT_TRUE(r.mode == Mode::kChineseEmpty);  // = config.initialMode
  EXPECT_EQ(r.preedit, "");
  EXPECT_EQ(r.pageCount, 0u);
  EXPECT_EQ(r.highlight, -1);
  EXPECT_TRUE(r.effects.empty());  // 不提交任何文本
  // 重置后重新输入从零开始。
  const abc::EngineResult r2 = s->handle(abc_test::letter(U'n'));
  EXPECT_EQ(r2.preedit, "n");
}

ABC_TEST(ResetFromEnglishKeepsInitialMode) {
  FakeDictionary dict;
  EngineConfig cfg;
  cfg.initialMode = Mode::kEnglish;
  auto s = abc::createEngineSession(cfg, &dict);
  const abc::EngineResult r = s->reset();
  EXPECT_TRUE(r.mode == Mode::kEnglish);
  EXPECT_EQ(r.preedit, "");
}

ABC_TEST(InvalidDigitCommandReturnsErrorWithoutStateChange) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = abc::createEngineSession(EngineConfig{}, &dict);
  abc_test::typeLetters(*s, "ni");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});  // Selecting
  const abc::EngineResult before = s->snapshot();
  for (const uint8_t bad : {0, 10, 255}) {
    const abc::EngineResult r = s->handle(abc_test::digit(bad));
    EXPECT_FALSE(r.consumed);
    EXPECT_TRUE(r.error == EngineError::kInvalidCommand);
    EXPECT_TRUE(r.effects.empty());
    EXPECT_TRUE(r.mode == before.mode);
    EXPECT_EQ(r.preedit, before.preedit);
    EXPECT_EQ(r.candidates.size(), before.candidates.size());
  }
  // 合法数字仍可用。
  const abc::EngineResult ok = s->handle(abc_test::digit(1));
  EXPECT_TRUE(ok.consumed);
  EXPECT_EQ(ok.effects[0].text, "你");
}

ABC_TEST(InvalidCharacterCommandReturnsError) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = abc::createEngineSession(EngineConfig{}, &dict);
  const abc::EngineResult r1 = s->handle(abc_test::letter(U'\0'));
  EXPECT_FALSE(r1.consumed);
  EXPECT_TRUE(r1.error == EngineError::kInvalidCommand);
  const abc::EngineResult r2 = s->handle(abc_test::punct(U'\0'));
  EXPECT_FALSE(r2.consumed);
  EXPECT_TRUE(r2.error == EngineError::kInvalidCommand);
  abc_test::expectState(s->snapshot(), Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(InvalidCommandInComposingLeavesStateUntouched) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = abc::createEngineSession(EngineConfig{}, &dict);
  abc_test::typeLetters(*s, "nihao");
  const abc::EngineResult r = s->handle(abc_test::digit(200));
  EXPECT_FALSE(r.consumed);
  EXPECT_TRUE(r.error == EngineError::kInvalidCommand);
  // 前缀 "nihao" 在本夹具中命中 1 条（你好）。
  abc_test::expectState(r, Mode::kComposing, "nihao", 0, 1, 1, -1);
  // 状态未变：数字修正后 Enter 仍提交原文。
  const abc::EngineResult re = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});
  EXPECT_EQ(re.effects[0].text, "nihao");
}

}  // namespace
