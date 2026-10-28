// A1 测试：字符不静默丢失核算（不变量 7）、会话隔离（缓冲唯一所有权）、
// 确定性（无全局状态）、全状态×全按键不变量扫描。

#include <memory>
#include <string>
#include <vector>

#include "fake_dictionary.hpp"
#include "test_util.hpp"

namespace {

using abc::EngineConfig;
using abc::EngineResult;
using abc::EngineSession;
using abc::KeyCommand;
using abc::KeyCode;
using abc::Mode;
using abc_test::FakeDictionary;
using abc_test::Probe;

std::unique_ptr<EngineSession> makeSession(FakeDictionary& dict,
                                           const EngineConfig& cfg = {}) {
  return abc::createEngineSession(cfg, &dict);
}

void populateNi(FakeDictionary& dict) {
  dict.add("ni", "你", 100);
  dict.add("ni", "泥", 10);
  dict.add("ni", "呢", 3);
  dict.add("nihao", "你好", 20);
  dict.add("nihao", "你号", 8);
}

void populateMa(FakeDictionary& dict) {
  for (int i = 1; i <= 23; ++i) {
    char text[16];
    std::snprintf(text, sizeof(text), "妈%02d", i);
    dict.add("ma", text, 100.0 - i);
  }
}

void typeProbe(Probe& p, const std::string& letters) {
  for (char c : letters) {
    p.step(abc_test::letter(
        static_cast<char32_t>(static_cast<unsigned char>(c))));
  }
}

ABC_TEST(AccountingEnterCommit) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  p.step(KeyCommand{KeyCode::kEnter});
  EXPECT_EQ(p.committedText, "nihao");
  EXPECT_EQ(p.committedLetters, 5);
}

ABC_TEST(AccountingEscCancel) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  p.step(KeyCommand{KeyCode::kEscape});
  EXPECT_EQ(p.committedText, "");
  EXPECT_EQ(p.removed, 5);  // Esc = 明确取消，不是静默丢失
}

ABC_TEST(AccountingCandidateSelection) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  p.step(KeyCommand{KeyCode::kSpace});
  p.step(abc_test::digit(1));  // 选择“你好”
  EXPECT_EQ(p.committedText, "你好");
  EXPECT_EQ(p.committedLetters, 5);  // 5 个编码字母随选择进入提交
}

ABC_TEST(AccountingBackspaceAll) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  for (int i = 0; i < 6; ++i) p.step(KeyCommand{KeyCode::kBackspace});
  EXPECT_EQ(p.removed, 5);   // 前 5 次逐字符明确删除
  EXPECT_EQ(p.forwarded, 0); // 第 6 次在空缓冲转发（退格非字母键，不计 forwarded）
}

ABC_TEST(AccountingModeSwitchThenEnglish) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "ni");
  p.step(KeyCommand{KeyCode::kModeSwitch});  // 默认策略：提交原文
  typeProbe(p, "abc");                       // 英文态：转发给应用
  p.step(KeyCommand{KeyCode::kModeSwitch});
  typeProbe(p, "de");
  p.step(KeyCommand{KeyCode::kEnter});
  EXPECT_EQ(p.committedText, "nide");
  EXPECT_EQ(p.forwarded, 3);
}

ABC_TEST(AccountingOverflow) {
  FakeDictionary dict;
  auto s = makeSession(dict);
  Probe p(*s);
  for (int i = 0; i < 70; ++i) p.step(abc_test::letter(U'x'));
  p.step(KeyCommand{KeyCode::kEnter});  // 提交 64 字符缓冲
  EXPECT_EQ(p.committedLetters, 64);
  EXPECT_EQ(p.forwarded, 6);  // 溢出字母逐个转发给应用，不无声丢失
  EXPECT_EQ(p.committedText.size(), 64u);
}

ABC_TEST(AccountingSelectionPagingAndNonHandled) {
  FakeDictionary dict;
  populateMa(dict);
  // 末页（第 2 页 5 项）中 '9' 无对应项：不消费、状态不变；'3' 正常选择。
  {
    auto s = makeSession(dict);
    Probe p(*s);
    typeProbe(p, "ma");
    p.step(KeyCommand{KeyCode::kSpace});
    p.step(KeyCommand{KeyCode::kPageDown});
    p.step(KeyCommand{KeyCode::kPageDown});
    p.step(abc_test::digit(9));
    EXPECT_TRUE(p.committedText.empty());
    EXPECT_TRUE(s->snapshot().mode == Mode::kSelecting);
    p.step(abc_test::digit(3));  // 第 2 页第 3 项
    EXPECT_EQ(p.committedText, "妈21");
  }
  // 无效数字 '0'？—— '0' 由适配层映射为 kPunctuation（契约 2.1），
  // Selecting 下按未处理键策略提交原文并转发。
  {
    auto s = makeSession(dict);
    Probe p(*s);
    typeProbe(p, "ma");
    p.step(KeyCommand{KeyCode::kSpace});
    p.step(abc_test::punct(U'0'));
    EXPECT_EQ(p.committedText, "ma");
  }
  // 标点：未处理键策略。
  {
    auto s = makeSession(dict);
    Probe p(*s);
    typeProbe(p, "ma");
    p.step(KeyCommand{KeyCode::kSpace});
    p.step(KeyCommand{KeyCode::kPageDown});
    p.step(KeyCommand{KeyCode::kPageUp});
    p.step(abc_test::punct(U','));
    EXPECT_EQ(p.committedText, "ma");
  }
  // 无候选空格（默认策略）：提交原文 + 空格转发。
  {
    auto s = makeSession(dict);
    Probe p(*s);
    typeProbe(p, "zz");  // 无匹配编码
    p.step(KeyCommand{KeyCode::kSpace});
    EXPECT_EQ(p.committedText, "zz");
  }
}

ABC_TEST(ResetAccountedAsExplicitCancel) {
  FakeDictionary dict;
  populateNi(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  s->reset();  // 焦点丢失：显式清空（无 effects，直接核对终态）
  EXPECT_TRUE(s->snapshot().mode == Mode::kChineseEmpty);
  EXPECT_EQ(s->snapshot().preedit, "");
  // 探针以 Esc 等价路径核对：reset 相当于 removed += 5。
  p.removed += 5;
  p.check(s->snapshot());
}

ABC_TEST(TwoSessionsHoldIndependentBuffers) {
  // 缓冲由各会话唯一持有：互不污染，addon 无需也不得复制缓冲。
  FakeDictionary dict;
  populateNi(dict);
  auto a = makeSession(dict);
  auto b = makeSession(dict);
  abc_test::typeLetters(*a, "ni");
  abc_test::typeLetters(*b, "nihao");
  EXPECT_EQ(a->snapshot().preedit, "ni");
  EXPECT_EQ(b->snapshot().preedit, "nihao");
  EXPECT_EQ(a->snapshot().candidates.size(), 5u);  // "ni": ni(3)+nihao(2)
  EXPECT_EQ(b->snapshot().candidates.size(), 2u);  // "nihao": nihao(2)
  const EngineResult ra = a->handle(KeyCommand{KeyCode::kEnter});
  const EngineResult rb = b->handle(KeyCommand{KeyCode::kEscape});
  EXPECT_EQ(ra.effects[0].text, "ni");
  // 冻结契约：Esc 产生 ClearComposition effect（不提交文本）。
  EXPECT_EQ(rb.effects.size(), 1u);
  EXPECT_TRUE(rb.effects[0].kind == abc::Effect::Kind::kClearComposition);
  EXPECT_TRUE(rb.effects[0].text.empty());
  EXPECT_TRUE(a->snapshot().mode == Mode::kChineseEmpty);
  EXPECT_TRUE(b->snapshot().mode == Mode::kChineseEmpty);
}

ABC_TEST(SameInputSequenceIsDeterministic) {
  FakeDictionary dict;
  populateNi(dict);
  populateMa(dict);
  const std::vector<KeyCommand> seq = {
      abc_test::letter(U'n'), abc_test::letter(U'I'),
      abc_test::letter(U'h'), KeyCommand{KeyCode::kBackspace},
      abc_test::letter(U'h'), abc_test::letter(U'a'),
      abc_test::letter(U'o'), KeyCommand{KeyCode::kSpace},
      KeyCommand{KeyCode::kPageDown},
  };
  auto s1 = makeSession(dict);
  auto s2 = makeSession(dict);
  for (const KeyCommand& k : seq) {
    const EngineResult r1 = s1->handle(k);
    const EngineResult r2 = s2->handle(k);
    EXPECT_EQ(r1.consumed, r2.consumed);
    EXPECT_TRUE(r1.mode == r2.mode);
    EXPECT_EQ(r1.preedit, r2.preedit);
    EXPECT_EQ(r1.page, r2.page);
    EXPECT_EQ(r1.pageCount, r2.pageCount);
    EXPECT_EQ(r1.highlight, r2.highlight);
    EXPECT_EQ(r1.candidates.size(), r2.candidates.size());
    EXPECT_EQ(r1.effects.size(), r2.effects.size());
  }
}

ABC_TEST(InvariantSweepAllStatesAllKeys) {
  // 四个起点（英文/中文空/Composing/Selecting）× 11 类按键：
  // 每步后字符核算成立（Probe::step 内部）、快照满足契约不变量 1/2/3/4/5、
  // 且每个按键要么被消费要么完整转发。
  FakeDictionary dict;
  populateNi(dict);
  populateMa(dict);

  for (int start = 0; start < 4; ++start) {
    auto s = makeSession(dict);
    Probe p(*s);
    if (start == 0) {
      p.step(KeyCommand{KeyCode::kModeSwitch});
    } else if (start >= 2) {
      typeProbe(p, "ma");
      if (start == 3) p.step(KeyCommand{KeyCode::kSpace});
    }
    for (int kind = 0; kind < 11; ++kind) {
      const KeyCommand k = [kind]() {
        switch (kind) {
          case 0: return abc_test::letter(U'z');
          case 1: return abc_test::digit(7);
          case 2: return abc_test::punct(U',');
          case 3: return abc_test::punct(U'0');  // '0' 按适配层映射为标点
          case 4: return KeyCommand{KeyCode::kBackspace};
          case 5: return KeyCommand{KeyCode::kEnter};
          case 6: return KeyCommand{KeyCode::kEscape};
          case 7: return KeyCommand{KeyCode::kSpace};
          case 8: return KeyCommand{KeyCode::kPageUp};
          case 9: return KeyCommand{KeyCode::kPageDown};
          default: return KeyCommand{KeyCode::kUnknown, 0x00FF, 0};
        }
      }();
      p.step(k);
      const EngineResult snap = s->snapshot();
      abc_test::checkInvariants(snap, s->config());
      EXPECT_TRUE(static_cast<int>(snap.candidates.size()) <= 9);
    }
    if (s->snapshot().mode != Mode::kEnglish &&
        s->snapshot().preedit.size() == 0) {
      EXPECT_TRUE(s->snapshot().mode == Mode::kChineseEmpty);
    }
  }
}

ABC_TEST(DictionaryQueryShapeAcrossBattery) {
  FakeDictionary dict;
  populateNi(dict);
  populateMa(dict);
  auto s = makeSession(dict);
  Probe p(*s);
  typeProbe(p, "nihao");
  p.step(KeyCommand{KeyCode::kSpace});
  p.step(KeyCommand{KeyCode::kPageDown});
  p.step(KeyCommand{KeyCode::kPageUp});
  p.step(KeyCommand{KeyCode::kBackspace});
  EXPECT_FALSE(dict.calls().empty());
  for (const auto& call : dict.calls()) {
    EXPECT_FALSE(call.prefix.empty());  // 绝不以空前缀查询
    EXPECT_EQ(call.limit, 9u);
    for (char c : call.prefix) {
      EXPECT_TRUE(c >= 'a' && c <= 'z');  // 查询前缀为规范化小写
    }
  }
  // 缓冲变化查询 offset=0；翻页查询 offset=page*pageSize。
  EXPECT_EQ(dict.calls().front().offset, 0u);
}

}  // namespace
