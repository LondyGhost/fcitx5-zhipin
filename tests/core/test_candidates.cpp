// A1 测试：候选选择（数字/空格）、翻页与页边界、pageCount 语义、冻结排序、
// 无效数字、词典错误路径与病理词典。

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

// "ma" 下 23 个候选，权重严格递减 ⇒ 冻结排序即录入顺序，便于分页断言。
void populateMa(FakeDictionary& dict) {
  for (int i = 1; i <= 23; ++i) {
    char text[16];
    std::snprintf(text, sizeof(text), "妈%02d", i);
    dict.add("ma", text, 100.0 - i);
  }
}

ABC_TEST(SpaceWithCandidatesEntersSelecting) {
  FakeDictionary dict;
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kSelecting, "ni", 0, 1, 2, 0);
  EXPECT_TRUE(r.effects.empty());  // 进入选择态本身不提交
}

ABC_TEST(DigitSelectsPageItemIndependentOfHighlight) {
  FakeDictionary dict;
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  dict.add("ni", "呢", 3);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  const abc::EngineResult r = s->handle(abc_test::digit(2));
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_EQ(r.effects[0].text, "泥");  // 页内第 2 项
  EXPECT_TRUE(r.error == EngineError::kNone);
}

ABC_TEST(InvalidDigitNotConsumedStateUnchanged) {
  FakeDictionary dict;
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  // 页内仅 2 项，'5' 无对应项：不消费、状态不变、键转发、缓冲保留。
  const abc::EngineResult r = s->handle(abc_test::digit(5));
  EXPECT_FALSE(r.consumed);
  EXPECT_TRUE(r.effects.empty());
  abc_test::expectState(r, Mode::kSelecting, "ni", 0, 1, 2, 0);
  // 之后仍可正常选择。
  const abc::EngineResult r2 = s->handle(abc_test::digit(1));
  EXPECT_TRUE(r2.consumed);
  EXPECT_EQ(r2.effects[0].text, "你");
}

ABC_TEST(PageNavigationAndBoundaries) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  // 23 条 / 页大小 9 ⇒ 3 页；第 0 页 9 条。
  abc_test::expectState(r, Mode::kSelecting, "ma", 0, 3, 9, 0);
  EXPECT_EQ(r.candidates[0].text, "妈01");
  EXPECT_EQ(r.candidates[8].text, "妈09");

  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kSelecting, "ma", 1, 3, 9, 0);
  EXPECT_EQ(r.candidates[0].text, "妈10");
  EXPECT_EQ(r.candidates[8].text, "妈18");
  EXPECT_EQ(dict.calls().back().offset, 9u);  // 翻页查询 offset = page*pageSize

  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  abc_test::expectState(r, Mode::kSelecting, "ma", 2, 3, 5, 0);
  EXPECT_EQ(r.candidates[0].text, "妈19");
  EXPECT_EQ(r.candidates[4].text, "妈23");
  EXPECT_EQ(dict.calls().back().offset, 18u);

  // 末页再下翻：消费但无变化（页边界）。
  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kSelecting, "ma", 2, 3, 5, 0);

  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageUp});
  abc_test::expectState(r, Mode::kSelecting, "ma", 1, 3, 9, 0);
  EXPECT_EQ(r.candidates[0].text, "妈10");

  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageUp});
  abc_test::expectState(r, Mode::kSelecting, "ma", 0, 3, 9, 0);
  EXPECT_EQ(dict.calls().back().offset, 0u);

  // 首页再上翻：消费但无变化。
  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageUp});
  abc_test::expectState(r, Mode::kSelecting, "ma", 0, 3, 9, 0);
}

ABC_TEST(SpaceInSelectingCommitsHighlightedPageFirst) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});  // 第 1 页（0 基）
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_EQ(r.effects[0].text, "妈10");  // 当前页高亮（页首）候选
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(PagingResetsOnEdit) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  // 继续输入：回到 Composing，页复位，候选按新前缀刷新。
  const abc::EngineResult r = s->handle(abc_test::letter(U'x'));
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kComposing, "max", 0, 0, 0, -1);
}

ABC_TEST(BackspaceInSelectingReturnsToComposing) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kBackspace});
  EXPECT_TRUE(r.consumed);
  // 前缀 "m" 命中全部 23 条；高亮复位 -1（预览态）。
  abc_test::expectState(r, Mode::kComposing, "m", 0, 3, 9, -1);
  EXPECT_EQ(r.candidates[0].text, "妈01");
}

ABC_TEST(EnterInSelectingCommitsRawBuffer) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});
  EXPECT_EQ(r.effects[0].text, "ma");  // 原文，而非当前页候选
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(LetterInSelectingResumesComposing) {
  FakeDictionary dict;
  populateMa(dict);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  // Selecting 下输入字母：追加并回到 Composing（有匹配 → 预览态）。
  const abc::EngineResult r = s->handle(abc_test::letter(U'X'));  // 大写规范化
  EXPECT_TRUE(r.consumed);
  abc_test::expectState(r, Mode::kComposing, "max", 0, 0, 0, -1);
}

ABC_TEST(DictionaryErrorTreatedAsNoCandidatesKeepsBuffer) {
  FakeDictionary dict;  // 查询全部由脚本接管
  bool fail = false;
  dict.setScript([&fail](std::string_view prefix, uint32_t limit,
                         uint32_t offset) -> abc::LookupResult {
    if (fail) {
      abc::LookupResult bad;
      bad.status = abc::DictionaryStatus::kCorruptIndex;
      return bad;
    }
    // 非失败路径退回默认行为的最小实现（本用例只覆盖 "n" 首查）。
    FakeDictionary inner;
    inner.add("ni", "你", 100);
    return inner.lookup(prefix, limit, offset);
  });
  auto s = makeSession(dict);
  abc::EngineResult r = s->handle(abc_test::letter(U'x'));
  abc_test::expectState(r, Mode::kComposing, "x", 0, 0, 0, -1);
  fail = true;
  r = s->handle(abc_test::letter(U'y'));
  EXPECT_TRUE(r.consumed);
  EXPECT_TRUE(r.error == EngineError::kDictionaryError);  // 标注错误
  abc_test::expectState(r, Mode::kComposing, "xy", 0, 0, 0, -1);  // 缓冲保留
  EXPECT_TRUE(r.candidates.empty());
  // 无候选 Space：默认策略提交原文并转发，数据不丢。
  r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_FALSE(r.consumed);
  EXPECT_EQ(r.effects[0].text, "xy");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(DictionaryErrorNotStickyAcrossKeystrokes) {
  // error 字段描述本次调用：lookup 失败后的后续按键（未发生查询）
  // 不得沿用上一次的 kDictionaryError 标注（提交/快照路径无查询）。
  FakeDictionary dict;
  populateMa(dict);
  bool fail = false;
  dict.setScript([&fail](std::string_view prefix, uint32_t limit,
                         uint32_t offset) -> abc::LookupResult {
    if (fail) {
      abc::LookupResult bad;
      bad.status = abc::DictionaryStatus::kIoError;
      return bad;
    }
    FakeDictionary inner;
    for (int i = 1; i <= 23; ++i) {
      char text[16];
      std::snprintf(text, sizeof(text), "妈%02d", i);
      inner.add("ma", text, 100.0 - i);
    }
    (void)prefix;
    return inner.lookup(std::string(prefix), limit, offset);
  });
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  fail = true;
  abc::EngineResult r = s->handle(abc_test::letter(U'x'));  // 查询失败
  EXPECT_TRUE(r.error == EngineError::kDictionaryError);
  r = s->handle(abc::KeyCommand{abc::KeyCode::kEnter});  // 提交原文：本次无查询
  EXPECT_TRUE(r.error == EngineError::kNone);
  EXPECT_EQ(r.effects.size(), 1u);
  EXPECT_EQ(r.effects[0].text, "max");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
  EXPECT_TRUE(s->snapshot().error == EngineError::kNone);
}

ABC_TEST(DictionaryErrorDuringPagingFallsBackToComposing) {
  // 翻页时查询失败：按无候选处理，组合保留，Selecting 回落 Composing
  //（不变量 4：无候选的 Selecting 不存在）。
  FakeDictionary dict;
  populateMa(dict);
  bool fail = false;
  dict.setScript([&fail](std::string_view prefix, uint32_t limit,
                         uint32_t offset) -> abc::LookupResult {
    if (fail) {
      abc::LookupResult bad;
      bad.status = abc::DictionaryStatus::kIoError;
      return bad;
    }
    FakeDictionary inner;
    for (int i = 1; i <= 23; ++i) {
      char text[16];
      std::snprintf(text, sizeof(text), "妈%02d", i);
      inner.add("ma", text, 100.0 - i);
    }
    (void)prefix;
    return inner.lookup(std::string(prefix), limit, offset);
  });
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ma");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  fail = true;
  const abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  EXPECT_TRUE(r.consumed);
  EXPECT_TRUE(r.error == EngineError::kDictionaryError);
  abc_test::expectState(r, Mode::kComposing, "ma", 0, 0, 0, -1);  // 缓冲保留
}

ABC_TEST(FrozenOrderingExactCodeFirst) {
  // 契约 4.1：精确编码组在前，其余 code 升序，组内 weight 降序、text 升序。
  FakeDictionary dict;
  dict.add("nihaoma", "你好吗", 200);
  dict.add("ni", "泥", 10);
  dict.add("nihao", "你好", 90);
  dict.add("ni", "你", 100);
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ni");
  const abc::EngineResult r = s->snapshot();
  EXPECT_EQ(r.candidates.size(), 4u);
  EXPECT_EQ(r.candidates[0].text, "你");    // 精确组，weight 降序
  EXPECT_EQ(r.candidates[1].text, "泥");
  EXPECT_EQ(r.candidates[2].text, "你好");    // code "nihao" < "nihaoma"
  EXPECT_EQ(r.candidates[3].text, "你好吗");
  s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  const abc::EngineResult r2 = s->handle(abc_test::digit(3));
  EXPECT_EQ(r2.effects[0].text, "你好");
}

ABC_TEST(DuplicateCandidatesAreDistinctSlots) {
  // 词典返回重复候选：核心按槽位选择，去重是词典层职责。
  FakeDictionary dict;
  bool second = false;
  dict.setScript([&second](std::string_view, uint32_t limit,
                           uint32_t offset) -> abc::LookupResult {
    abc::LookupResult r;
    if (offset == 0) {
      for (int i = 0; i < 2 && static_cast<uint32_t>(i) < limit; ++i) {
        abc::Candidate c;
        c.text = "阿姨";
        c.code = "ay";
        c.weight = 5.0;
        c.source = "fake";
        c.stableId = abc::computeStableId("ay", "阿姨", "fake") +
                     static_cast<uint64_t>(i);  // 人为区分两槽
        r.batch.items.push_back(c);
      }
      r.batch.total = 2;
      (void)second;
    }
    return r;
  });
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "ay");
  abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  EXPECT_EQ(r.candidates.size(), 2u);
  r = s->handle(abc_test::digit(2));
  EXPECT_TRUE(r.consumed);
  EXPECT_EQ(r.effects[0].text, "阿姨");
  abc_test::expectState(r, Mode::kChineseEmpty, "", 0, 0, 0, -1);
}

ABC_TEST(PageSizeOnePagingWalksOneByOne) {
  FakeDictionary dict;
  populateMa(dict);
  EngineConfig cfg;
  cfg.pageSize = 1;
  auto s = makeSession(dict, cfg);
  abc_test::typeLetters(*s, "ma");
  abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  abc_test::expectState(r, Mode::kSelecting, "ma", 0, 23, 1, 0);
  EXPECT_EQ(r.candidates[0].text, "妈01");
  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  EXPECT_EQ(r.page, 1u);
  EXPECT_EQ(r.candidates[0].text, "妈02");
  EXPECT_EQ(dict.calls().back().limit, 1u);
  EXPECT_EQ(dict.calls().back().offset, 1u);
  // 页大小 1 时 '2' 无对应项：不消费、状态不变。
  r = s->handle(abc_test::digit(2));
  EXPECT_FALSE(r.consumed);
  EXPECT_TRUE(r.effects.empty());
  EXPECT_EQ(s->snapshot().preedit, "ma");
  r = s->handle(abc_test::digit(1));
  EXPECT_EQ(r.effects[0].text, "妈02");
}

ABC_TEST(PageCountMathMatchesTotalAndPageSize) {
  FakeDictionary dict;
  // 18 条 ⇒ 页大小 9 时 2 页；第 2 页恰好 9 条（无零头页）。
  for (int i = 1; i <= 18; ++i) {
    char text[16];
    std::snprintf(text, sizeof(text), "词%02d", i);
    dict.add("cc", text, 200.0 - i);
  }
  auto s = makeSession(dict);
  abc_test::typeLetters(*s, "cc");
  abc::EngineResult r = s->handle(abc::KeyCommand{abc::KeyCode::kSpace});
  abc_test::expectState(r, Mode::kSelecting, "cc", 0, 2, 9, 0);
  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  abc_test::expectState(r, Mode::kSelecting, "cc", 1, 2, 9, 0);
  r = s->handle(abc::KeyCommand{abc::KeyCode::kPageDown});
  abc_test::expectState(r, Mode::kSelecting, "cc", 1, 2, 9, 0);  // 末页边界
}

}  // namespace
