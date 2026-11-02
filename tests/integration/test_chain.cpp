// A6 集成测试 — A2 编译最小 fixture → A3 从索引加载/查询 → A1 状态机。
//
// 本测试只使用公共头文件与各模块公开目标，不触碰模块内部件。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "abc/engine.h"
#include "abc/dictionary.h"
#include "dict_importer.hpp"
#include "test_harness.hpp"

#ifdef ABC_HAVE_CONFIG
#include "abc_config.h"
#endif

namespace {

using abc::Effect;
using abc::EngineConfig;
using abc::EngineError;
using abc::EngineResult;
using abc::EngineSession;
using abc::KeyCommand;
using abc::KeyCode;
using abc::Mode;
namespace fs = std::filesystem;

class FixtureDictionary {
 public:
  FixtureDictionary() {
    const fs::path root = fs::temp_directory_path() / "abc-a6-integration";
    fs::create_directories(root);
    const fs::path input = root / "minimal.dict.yaml";
    const fs::path index = root / "minimal.abcdix";
    const fs::path fixture = ABC_A6_FIXTURE_PATH;
    if (!fs::is_regular_file(fixture)) {
      error_ = "configured A2 fixture is missing";
      return;
    }
    std::error_code ec;
    if (!fs::remove(index, ec) && ec) {
      error_ = "cannot clear previous integration index";
      return;
    }
    ec.clear();
    fs::copy_file(fixture, input, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error_ = "cannot copy A2 fixture";
      return;
    }
    std::ifstream fixture_stream(fixture, std::ios::binary);
    std::ifstream copied_stream(input, std::ios::binary);
    const std::string fixture_bytes((std::istreambuf_iterator<char>(fixture_stream)),
                                    std::istreambuf_iterator<char>());
    const std::string copied_bytes((std::istreambuf_iterator<char>(copied_stream)),
                                   std::istreambuf_iterator<char>());
    if (!fixture_stream || !copied_stream || fixture_bytes != copied_bytes) {
      error_ = "cannot verify copied A2 fixture";
      return;
    }
    abc::dict_import::ImportOptions options;
    options.source = "a6-fixture";
    const auto compiled = abc::dict_import::compileIndexFile(
        input.string(), index.string(), options);
    if (compiled.exit_code != abc::dict_import::ExitCode::kOk) {
      error_ = "A2 fixture compilation failed";
      return;
    }
    auto loaded = abc::loadDictionaryFromFile(index.string());
    if (loaded.status != abc::DictionaryStatus::kOk || !loaded.dictionary) {
      error_ = "A3 rejected A2-generated index";
      return;
    }
    dictionary_ = std::move(loaded.dictionary);
  }

  abc::Dictionary* get() const { return dictionary_.get(); }
  const std::string& error() const { return error_; }

 private:
  std::unique_ptr<abc::Dictionary> dictionary_;
  std::string error_;
};

std::unique_ptr<EngineSession> makeSession(FixtureDictionary& dict,
                                           const EngineConfig& cfg = {}) {
  return abc::createEngineSession(cfg, dict.get());
}

KeyCommand letter(char32_t c) {
  KeyCommand k;
  k.code = KeyCode::kCharacter;
  k.unicode = c;
  return k;
}
KeyCommand digit(uint8_t d) {
  KeyCommand k;
  k.code = KeyCode::kDigit;
  k.digit = d;
  return k;
}
KeyCommand punct(char32_t c) {
  KeyCommand k;
  k.code = KeyCode::kPunctuation;
  k.unicode = c;
  return k;
}

void typeLetters(EngineSession& s, const std::string& letters) {
  for (char c : letters) s.handle(letter(static_cast<char32_t>(
      static_cast<unsigned char>(c))));
}

// ---- 契约不变量独立复核（A6 复核用，不依赖 A1 测试内部件）----

void checkSnapshotInvariants(const EngineResult& r, const EngineConfig& cfg) {
  IEXPECT_EQ(r.preedit.empty(),
             r.mode == Mode::kEnglish || r.mode == Mode::kChineseEmpty);
  if (r.pageCount == 0) {
    IEXPECT_TRUE(r.candidates.empty());
    IEXPECT_EQ(r.page, 0u);
    IEXPECT_EQ(r.highlight, -1);
  } else {
    IEXPECT_TRUE(!r.candidates.empty());
    IEXPECT_TRUE(r.candidates.size() <= cfg.pageSize);
    if (r.mode == Mode::kSelecting) {
      IEXPECT_TRUE(r.highlight >= 0);
      IEXPECT_TRUE(static_cast<std::size_t>(r.highlight) < r.candidates.size());
    } else if (r.mode == Mode::kComposing) {
      IEXPECT_EQ(r.highlight, -1);
    }
  }
  for (const Effect& e : r.effects) {
    if (e.kind == Effect::Kind::kCommitText) {
      IEXPECT_EQ(r.preedit, "");
      IEXPECT_EQ(r.pageCount, 0u);
      IEXPECT_TRUE(r.mode == Mode::kChineseEmpty || r.mode == Mode::kEnglish);
    }
  }
}

// 字符不静默丢失核算（engine.md 4.1 不变量 7）。
struct ChainProbe {
  EngineSession* s = nullptr;
  std::string committedText;
  long typed = 0, committedLetters = 0, removed = 0, forwarded = 0;

  explicit ChainProbe(EngineSession& session) : s(&session) {}

  EngineResult step(const KeyCommand& k) {
    const std::size_t lenBefore = s->snapshot().preedit.size();
    const EngineResult r = s->handle(k);
    if (k.code == KeyCode::kCharacter && k.unicode && isLetter(k.unicode))
      ++typed;
    std::size_t commits = 0;
    for (const Effect& e : r.effects) {
      if (e.kind == Effect::Kind::kCommitText) {
        ++commits;
        committedText += e.text;
      }
    }
    if (commits > 0) committedLetters += static_cast<long>(lenBefore);
    if (k.code == KeyCode::kCharacter && isLetter(k.unicode) && !r.consumed)
      ++forwarded;
    const long left = static_cast<long>(lenBefore) -
                      static_cast<long>(r.preedit.size());
    if (left > 0)
      removed += left - static_cast<long>(commits) * static_cast<long>(lenBefore);
    const long accounted = committedLetters +
                           static_cast<long>(r.preedit.size()) + removed +
                           forwarded;
    IEXPECT_EQ(typed, accounted);
    checkSnapshotInvariants(r, s->config());
    return r;
  }

  static bool isLetter(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
  }
};

// 经核算探针逐键输入字母（探针用例必须走此路径，保证 typed 计数完整）。
void typeProbe(ChainProbe& p, const std::string& letters) {
  for (char c : letters) {
    p.step(letter(static_cast<char32_t>(static_cast<unsigned char>(c))));
  }
}

// ---- 链路用例 ----

ITEST(ChainConfigDefaultsToSession) {
  // A5→A1 交接点：config.md 第 4 节默认值经 EngineConfig 进入会话。
  EngineConfig cfg;  // 默认：chinese / 9 / 64 / commit_raw / commit_raw
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto s = makeSession(dict, cfg);
  IEXPECT_TRUE(static_cast<bool>(s));
  IEXPECT_TRUE(s->config().initialMode == Mode::kChineseEmpty);
  IEXPECT_EQ(s->config().pageSize, 9u);
  IEXPECT_EQ(s->config().maxBufferChars, 64u);
  const EngineResult snap = s->snapshot();
  IEXPECT_TRUE(snap.mode == Mode::kChineseEmpty);
  IEXPECT_EQ(snap.preedit, "");
  checkSnapshotInvariants(snap, cfg);
}

ITEST(ChainFactoryRejectsOutOfRangeConfig) {
  // 防御性复核：即使上游解析被绕过，A1 工厂仍必须拒绝越界配置。
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  EngineConfig cfg;
  cfg.pageSize = 0;
  IEXPECT_TRUE(abc::createEngineSession(cfg, dict.get()) == nullptr);
  cfg.pageSize = 10;
  IEXPECT_TRUE(abc::createEngineSession(cfg, dict.get()) == nullptr);
  cfg.pageSize = 9;
  cfg.maxBufferChars = 0;
  IEXPECT_TRUE(abc::createEngineSession(cfg, dict.get()) == nullptr);
  cfg.maxBufferChars = 257;
  IEXPECT_TRUE(abc::createEngineSession(cfg, dict.get()) == nullptr);
  cfg.maxBufferChars = 64;
  cfg.initialMode = Mode::kSelecting;
  IEXPECT_TRUE(abc::createEngineSession(cfg, dict.get()) == nullptr);
  IEXPECT_TRUE(abc::createEngineSession(cfg, nullptr) == nullptr);
}

ITEST(ChainTypingToCandidatesToSelection) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  IEXPECT_TRUE(dict.error().empty());
  auto s = makeSession(dict);
  ChainProbe p(*s);
  // 输入 "nihao"：每字母触发一次 offset=0 查询，前缀 = 规范化缓冲。
  for (char c : std::string("nihao")) {
    const EngineResult r = p.step(letter(static_cast<char32_t>(
        static_cast<unsigned char>(c))));
    IEXPECT_TRUE(r.consumed);
    IEXPECT_TRUE(r.mode == Mode::kComposing);
    IEXPECT_EQ(r.highlight, -1);  // 预览态不激活
  }
  IEXPECT_EQ(s->snapshot().preedit, "nihao");
  IEXPECT_EQ(s->snapshot().candidates.size(), 1u);
  const auto query = dict.get()->lookup("nihao", 9, 0);
  IEXPECT_EQ(query.batch.total, 1u);
  IEXPECT_EQ(query.batch.items.front().text, "你好");
  // 空格进入候选选择（highlight 0）。
  const EngineResult sel = p.step(KeyCommand{KeyCode::kSpace});
  IEXPECT_TRUE(sel.consumed);
  IEXPECT_TRUE(sel.mode == Mode::kSelecting);
  IEXPECT_EQ(sel.highlight, 0);
  IEXPECT_TRUE(sel.effects.empty());
  // A2 minimal fixture 中 nihao 唯一候选为“你好”。
  const EngineResult pick = p.step(digit(1));
  IEXPECT_TRUE(pick.consumed);
  IEXPECT_EQ(pick.effects.size(), 1u);
  IEXPECT_EQ(pick.effects[0].text, "你好");
  IEXPECT_TRUE(pick.mode == Mode::kChineseEmpty);
  IEXPECT_EQ(p.committedText, "你好");
  IEXPECT_EQ(p.committedLetters, 5);
}

ITEST(ChainA2GeneratedIndexLoadsAndQueriesThroughA3) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  IEXPECT_TRUE(dict.error().empty());
  const auto exact = dict.get()->lookup("nihao", 9, 0);
  IEXPECT_TRUE(exact.status == abc::DictionaryStatus::kOk);
  IEXPECT_EQ(exact.batch.total, 1u);
  IEXPECT_EQ(exact.batch.items.size(), 1u);
  IEXPECT_EQ(exact.batch.items.front().text, "你好");

  const auto prefix = dict.get()->lookup("ma", 9, 0);
  IEXPECT_TRUE(prefix.status == abc::DictionaryStatus::kOk);
  IEXPECT_EQ(prefix.batch.total, 4u);
  IEXPECT_EQ(prefix.batch.items.front().text, "马");
  IEXPECT_EQ(prefix.batch.items.back().text, "妈妈");
}

ITEST(ChainPagingAcrossDictionaryPages) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto s = makeSession(dict);
  typeLetters(*s, "ma");
  EngineResult r = s->handle(KeyCommand{KeyCode::kSpace});
  IEXPECT_EQ(r.pageCount, 1u);
  IEXPECT_EQ(r.candidates.size(), 4u);
  IEXPECT_EQ(r.candidates[0].text, "马");
  r = s->handle(KeyCommand{KeyCode::kPageDown});
  IEXPECT_EQ(r.page, 0u);
  r = s->handle(KeyCommand{KeyCode::kPageUp});
  IEXPECT_EQ(r.page, 0u);
  r = s->handle(KeyCommand{KeyCode::kPageUp});
  IEXPECT_EQ(r.page, 0u);
  r = s->handle(KeyCommand{KeyCode::kPageUp});  // 首页边界
  IEXPECT_EQ(r.page, 0u);
  checkSnapshotInvariants(s->snapshot(), s->config());
}

ITEST(ChainEnterRawAndEscAndBackspace) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  {
    auto s = makeSession(dict);
    ChainProbe p(*s);
    typeProbe(p, "nihao");
    const EngineResult r = p.step(KeyCommand{KeyCode::kEnter});
    IEXPECT_EQ(r.effects[0].text, "nihao");  // 原文，而非候选“你好”
    IEXPECT_TRUE(r.mode == Mode::kChineseEmpty);
  }
  {
    auto s = makeSession(dict);
    ChainProbe p(*s);
    typeProbe(p, "nihao");
    const EngineResult r = p.step(KeyCommand{KeyCode::kEscape});
    IEXPECT_EQ(r.effects.size(), 1u);
    IEXPECT_TRUE(r.effects[0].kind == Effect::Kind::kClearComposition);
    IEXPECT_TRUE(r.effects[0].text.empty());
    IEXPECT_EQ(p.committedText, "");
    IEXPECT_EQ(p.removed, 5);
  }
  {
    auto s = makeSession(dict);
    ChainProbe p(*s);
    typeProbe(p, "nihao");
    EngineResult r;
    for (int i = 0; i < 5; ++i) r = p.step(KeyCommand{KeyCode::kBackspace});
    IEXPECT_TRUE(r.mode == Mode::kChineseEmpty);
    IEXPECT_EQ(p.removed, 5);
    // 空缓冲退格转发给应用。
    r = p.step(KeyCommand{KeyCode::kBackspace});
    IEXPECT_FALSE(r.consumed);
  }
}

ITEST(ChainNonHandledKeyCommitsRawAndForwards) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto s = makeSession(dict);
  ChainProbe p(*s);
  typeProbe(p, "nihao");
  const EngineResult r = p.step(punct(U','));
  IEXPECT_FALSE(r.consumed);  // 键转发给应用
  IEXPECT_EQ(r.effects.size(), 1u);
  IEXPECT_EQ(r.effects[0].text, "nihao");  // 原文先提交，不静默丢字符
  IEXPECT_TRUE(r.mode == Mode::kChineseEmpty);
}

ITEST(ChainModeSwitchPolicies) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  {
    EngineConfig cfg;  // 默认 commit_raw
    auto s = makeSession(dict, cfg);
    ChainProbe p(*s);
    typeProbe(p, "ni");
    const EngineResult r = p.step(KeyCommand{KeyCode::kModeSwitch});
    IEXPECT_EQ(r.effects[0].text, "ni");
    IEXPECT_TRUE(r.mode == Mode::kEnglish);
    // 英文态字母全部转发。
    const EngineResult e1 = p.step(letter(U'x'));
    IEXPECT_FALSE(e1.consumed);
    // 切回中文后恢复缓冲。
    p.step(KeyCommand{KeyCode::kModeSwitch});
    const EngineResult back = p.step(letter(U'n'));
    IEXPECT_TRUE(back.consumed);
    IEXPECT_EQ(back.preedit, "n");
  }
  {
    EngineConfig cfg;
    cfg.modeSwitchCompositionPolicy =
        abc::ModeSwitchCompositionPolicy::kClearComposition;
    auto s = makeSession(dict, cfg);
    ChainProbe p(*s);
    typeProbe(p, "ni");
    const EngineResult r = p.step(KeyCommand{KeyCode::kModeSwitch});
    IEXPECT_EQ(r.effects.size(), 1u);
    IEXPECT_TRUE(r.effects[0].kind == Effect::Kind::kClearComposition);
    IEXPECT_EQ(p.committedText, "");
  }
}

ITEST(ChainConfiguredPageSizePropagates) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  EngineConfig cfg;
  cfg.pageSize = 5;
  auto s = makeSession(dict, cfg);
  typeLetters(*s, "ma");
  EngineResult r = s->handle(KeyCommand{KeyCode::kSpace});
  IEXPECT_EQ(r.pageCount, 1u);
  IEXPECT_EQ(r.candidates.size(), 4u);
  // 页大小 5 下选择页首候选。
  r = s->handle(digit(1));
  IEXPECT_EQ(r.effects[0].text, "马");
  // 超出当前候选数的数字不消费。
  auto s2 = makeSession(dict, cfg);
  typeLetters(*s2, "ma");
  s2->handle(KeyCommand{KeyCode::kSpace});
  const EngineResult bad = s2->handle(digit(6));
  IEXPECT_FALSE(bad.consumed);
  IEXPECT_EQ(s2->snapshot().preedit, "ma");
}

ITEST(ChainResetToInitialModeWithoutCommit) {
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto s = makeSession(dict);
  typeLetters(*s, "nihao");
  s->handle(KeyCommand{KeyCode::kSpace});
  const EngineResult r = s->reset();
  IEXPECT_TRUE(r.mode == Mode::kChineseEmpty);
  IEXPECT_EQ(r.preedit, "");
  IEXPECT_TRUE(r.effects.empty());
  // 重置后从头输入。
  const EngineResult again = s->handle(letter(U'n'));
  IEXPECT_EQ(again.preedit, "n");
}

ITEST(ChainDeterministicLongWalk) {
  // 确定性伪随机长序列：每步核算 + 不变量复核；两次独立会话结果一致。
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto run = [&dict]() {
    auto s = makeSession(dict);
    ChainProbe p(*s);
    std::vector<EngineResult> trace;
    uint64_t seed = 0x12345678;
    auto next = [&seed]() {
      seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
      return static_cast<uint32_t>(seed >> 33);
    };
    for (int i = 0; i < 400; ++i) {
      const uint32_t roll = next() % 100;
      KeyCommand k;
      if (roll < 40) {
        static const char* syllables[] = {"n", "i", "h", "a", "o", "m",
                                          "z", "x", "q", "b"};
        k = letter(static_cast<char32_t>(
            syllables[next() % 10][0]));
      } else if (roll < 50) {
        k = KeyCommand{KeyCode::kBackspace};
      } else if (roll < 58) {
        k = KeyCommand{KeyCode::kSpace};
      } else if (roll < 68) {
        k = digit(static_cast<uint8_t>(next() % 9 + 1));
      } else if (roll < 74) {
        k = KeyCommand{KeyCode::kPageUp};
      } else if (roll < 82) {
        k = KeyCommand{KeyCode::kPageDown};
      } else if (roll < 88) {
        k = KeyCommand{KeyCode::kEnter};
      } else if (roll < 92) {
        k = KeyCommand{KeyCode::kEscape};
      } else if (roll < 96) {
        k = punct(U',');
      } else {
        k = KeyCommand{KeyCode::kModeSwitch};
      }
      trace.push_back(p.step(k));
    }
    return trace;
  };
  const std::vector<EngineResult> t1 = run();
  const std::vector<EngineResult> t2 = run();
  IEXPECT_EQ(t1.size(), t2.size());
  for (std::size_t i = 0; i < t1.size(); ++i) {
    IEXPECT_EQ(t1[i].consumed, t2[i].consumed);
    IEXPECT_TRUE(t1[i].mode == t2[i].mode);
    IEXPECT_EQ(t1[i].preedit, t2[i].preedit);
    IEXPECT_EQ(t1[i].page, t2[i].page);
    IEXPECT_EQ(t1[i].pageCount, t2[i].pageCount);
    IEXPECT_EQ(t1[i].highlight, t2[i].highlight);
    IEXPECT_EQ(t1[i].candidates.size(), t2[i].candidates.size());
    IEXPECT_EQ(t1[i].effects.size(), t2[i].effects.size());
  }
}

#ifdef ABC_HAVE_CONFIG
ITEST(ChainConfigModuleSeam) {
  // A5→A1 seam（A5 提交并纳入构建后自动启用）：
  // 解析默认与越界回退，产出的 EngineConfig 直接交给 A1 工厂。
  abc::config::ParseResult r = abc::config::parseString(
      "initialMode = english\npageSize = 7\nindexPath = ~/data/idx.abcdix\n");
  IEXPECT_TRUE(r.engine.initialMode == Mode::kEnglish);
  IEXPECT_EQ(r.engine.pageSize, 7u);
  IEXPECT_TRUE(r.warnings.empty());
  FixtureDictionary dict;
  IEXPECT_TRUE(dict.get() != nullptr);
  auto s = abc::createEngineSession(r.engine, dict.get());
  IEXPECT_TRUE(static_cast<bool>(s));
  // 越界值回退默认 ⇒ 工厂必须接受（A5 保证范围合法）。
  abc::config::ParseResult bad = abc::config::parseString("pageSize = 99\n");
  IEXPECT_EQ(bad.engine.pageSize, 9u);
  auto s2 = abc::createEngineSession(bad.engine, dict.get());
  IEXPECT_TRUE(static_cast<bool>(s2));
}
#endif

}  // namespace

int main() { return abc_itest::runAll(); }
