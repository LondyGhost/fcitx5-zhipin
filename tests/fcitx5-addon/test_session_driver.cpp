// test_session_driver.cpp — 通过真实 EngineSession 验证适配层契约
// （engine.md 4.3、4.4、第 6 节）。不链接 fcitx5。
//
// 覆盖：preedit 更新、候选提交、停用/reset 清理、未消费键仍先提交原文。
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "abc/engine.h"
#include "ui_mapping.h"

namespace {

int g_failed = 0;

void expect(bool cond, const std::string& message) {
  if (!cond) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failed;
  }
}

class FixedDictionary final : public abc::Dictionary {
 public:
  abc::LookupResult lookup(std::string_view prefix, uint32_t limit,
                           uint32_t offset) const override {
    abc::LookupResult result;
    if (prefix.empty() || limit > 256) {
      result.status = abc::DictionaryStatus::kInvalidArgument;
      return result;
    }
    std::vector<abc::Candidate> all;
    if (prefix == "n" || prefix == "ni") {
      abc::Candidate first;
      first.text = "你";
      first.code = "ni";
      first.weight = 2.0;
      first.source = "test";
      abc::Candidate second;
      second.text = "尼";
      second.code = "ni";
      second.weight = 1.0;
      second.source = "test";
      all = {first, second};
    }
    result.batch.total = all.size();
    result.batch.offset = offset;
    for (uint32_t i = offset; i < all.size() && result.batch.items.size() < limit;
         ++i) {
      result.batch.items.push_back(all[i]);
    }
    result.batch.hasMore =
        static_cast<uint64_t>(offset) + result.batch.items.size() < all.size();
    return result;
  }
};


// 多页词典：用于验证选择态的方向键行为（页大小 9，共 20 条）。
class PagedDictionary final : public abc::Dictionary {
 public:
  abc::LookupResult lookup(std::string_view prefix, uint32_t limit,
                           uint32_t offset) const override {
    abc::LookupResult result;
    if (prefix.empty() || limit > 256) {
      result.status = abc::DictionaryStatus::kInvalidArgument;
      return result;
    }
    std::vector<abc::Candidate> all;
    if (prefix == "n" || prefix == "ni") {
      for (int i = 0; i < 20; ++i) {
        abc::Candidate item;
        item.text = "候" + std::to_string(i + 1);
        item.code = "ni";
        item.weight = 100.0 - i;
        item.source = "test";
        all.push_back(item);
      }
    }
    result.batch.total = all.size();
    result.batch.offset = offset;
    for (uint32_t i = offset; i < all.size() && result.batch.items.size() < limit;
         ++i) {
      result.batch.items.push_back(all[i]);
    }
    result.batch.hasMore =
        static_cast<uint64_t>(offset) + result.batch.items.size() < all.size();
    return result;
  }
};

abc::KeyCommand character(char32_t c) {
  abc::KeyCommand command;
  command.code = abc::KeyCode::kCharacter;
  command.unicode = c;
  return command;
}

abc::KeyCommand code(abc::KeyCode value) {
  abc::KeyCommand command;
  command.code = value;
  return command;
}

std::vector<std::string> commitsOf(const std::vector<abc::addon::UiAction>& actions) {
  std::vector<std::string> commits;
  for (const auto& action : actions) {
    if (action.kind == abc::addon::UiAction::Kind::kCommitText &&
        !action.text.empty()) {
      commits.push_back(action.text);
    }
  }
  return commits;
}

std::string lastPreedit(const std::vector<abc::addon::UiAction>& actions) {
  std::string preedit;
  for (const auto& action : actions) {
    if (action.kind == abc::addon::UiAction::Kind::kSetPreedit) {
      preedit = action.text;
    }
  }
  return preedit;
}

void testComposingPreedit() {
  FixedDictionary dictionary;
  abc::EngineConfig config;
  auto session = abc::createEngineSession(config, &dictionary);
  expect(session != nullptr, "session created");
  if (session == nullptr) {
    return;
  }

  const auto mapped = abc::addon::mapResult(session->handle(character(U'n')));
  expect(lastPreedit(mapped) == "n", "letter updates preedit");
  expect(commitsOf(mapped).empty(), "composing does not commit");
  // 交互约定：组合中只显示字母串，不弹候选；空格进入选择态后才出候选。
  bool sawCandidate = false;
  for (const auto& action : mapped) {
    if (action.kind == abc::addon::UiAction::Kind::kSetCandidates) {
      sawCandidate = true;
    }
  }
  expect(!sawCandidate, "composing hides candidates until space");

  const auto selecting =
      abc::addon::mapResult(session->handle(code(abc::KeyCode::kSpace)));
  bool sawCandidateAfterSpace = false;
  for (const auto& action : selecting) {
    if (action.kind == abc::addon::UiAction::Kind::kSetCandidates &&
        !action.candidates.empty() && action.candidates.front() == "你") {
      sawCandidateAfterSpace = true;
    }
  }
  expect(sawCandidateAfterSpace, "space shows the candidate list");
}


void testSelectingArrowNavigation() {
  // 选择态：上下翻页、左右移动高亮；移动本身不提交。
  PagedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  expect(session != nullptr, "paged session created");
  if (session == nullptr) {
    return;
  }
  session->handle(character(U'n'));
  const auto selecting = session->handle(code(abc::KeyCode::kSpace));
  expect(selecting.mode == abc::Mode::kSelecting && selecting.highlight == 0,
         "space enters selecting at the first candidate");
  expect(selecting.candidates.size() == 9, "first page holds pageSize candidates");

  auto right = session->handle(code(abc::KeyCode::kRight));
  expect(right.consumed, "right is consumed in selecting");
  expect(right.highlight == 1, "right moves the highlight");
  expect(right.mode == abc::Mode::kSelecting, "right stays in selecting");
  expect(right.effects.empty(), "right does not commit");
  expect(right.preedit == "n", "right keeps the preedit");

  for (int i = 0; i < 20; ++i) {
    right = session->handle(code(abc::KeyCode::kRight));
  }
  expect(right.highlight == 8, "right clamps at the last candidate of the page");

  const auto left = session->handle(code(abc::KeyCode::kLeft));
  expect(left.consumed && left.highlight == 7 && left.effects.empty(),
         "left moves the highlight back without commit");

  const auto down = session->handle(code(abc::KeyCode::kDown));
  expect(down.consumed && down.page == 1 && down.highlight == 0,
         "down pages forward and resets the highlight");
  expect(down.effects.empty(), "down does not commit");

  const auto up = session->handle(code(abc::KeyCode::kUp));
  expect(up.consumed && up.page == 0 && up.highlight == 0, "up pages back");

  const auto upAtFirst = session->handle(code(abc::KeyCode::kUp));
  expect(upAtFirst.consumed && upAtFirst.page == 0,
         "up at the first page is consumed without paging");

  session->handle(code(abc::KeyCode::kRight));
  const auto space = session->handle(code(abc::KeyCode::kSpace));
  expect(space.effects.size() == 1 && space.effects[0].text == "候2",
         "space commits the highlighted candidate after moving");
}

void testSelectCommitsCandidate() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  session->handle(character(U'i'));
  const abc::EngineResult space = session->handle(code(abc::KeyCode::kSpace));
  expect(space.consumed && space.mode == abc::Mode::kSelecting,
         "space with candidates enters selecting");

  abc::KeyCommand digit;
  digit.code = abc::KeyCode::kDigit;
  digit.digit = 1;
  const auto mapped = abc::addon::mapResult(session->handle(digit));
  const auto commits = commitsOf(mapped);
  expect(commits.size() == 1 && commits.front() == "你",
         "digit selection commits the candidate");
  expect(lastPreedit(mapped).empty(), "commit clears preedit");
}

void testSpaceSelectsHighlightedCandidate() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  session->handle(character(U'i'));
  const auto selecting = session->handle(code(abc::KeyCode::kSpace));
  const auto selectionUi = abc::addon::mapResult(selecting);
  bool highlightedFirst = false;
  for (const auto& action : selectionUi) {
    if (action.kind == abc::addon::UiAction::Kind::kSetCandidates) {
      highlightedFirst = action.highlight == 0 &&
                         !action.candidates.empty() &&
                         action.candidates.front() == "你";
    }
  }
  expect(highlightedFirst,
         "space selection exposes the core highlighted first candidate");
  const auto committed = session->handle(code(abc::KeyCode::kSpace));
  const auto commits = commitsOf(abc::addon::mapResult(committed));
  expect(commits.size() == 1 && commits.front() == "你",
         "space commits the highlighted candidate");
}

void testModeSwitchCommitsThenChangesMode() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  const auto switched = session->handle(code(abc::KeyCode::kModeSwitch));
  const auto actions = abc::addon::mapResult(switched);
  const auto commits = commitsOf(actions);
  expect(switched.consumed && switched.mode == abc::Mode::kEnglish,
         "mode switch is consumed and enters English mode");
  expect(commits.size() == 1 && commits.front() == "n",
         "mode switch commits the composing buffer according to default policy");
  const auto englishKey = session->handle(character(U'a'));
  expect(!englishKey.consumed,
         "ordinary key in English mode is returned to fcitx5");
}

void testResetClearsWithoutCommit() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  const auto mapped = abc::addon::mapResult(session->reset());
  expect(commitsOf(mapped).empty(), "reset/deactivation commits nothing");
  expect(lastPreedit(mapped).empty(), "reset clears preedit");
  expect(session->snapshot().mode == abc::Mode::kChineseEmpty,
         "reset returns to the configured initial mode");
}

void testSessionsAreIsolated() {
  FixedDictionary dictionary;
  auto first = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  auto second = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  first->handle(character(U'n'));
  first->handle(character(U'i'));
  second->handle(character(U'a'));
  expect(first->snapshot().preedit == "ni" &&
             second->snapshot().preedit == "a",
         "separate input-context sessions keep independent buffers");

  const auto shown = abc::addon::mapResult(first->snapshot());
  expect(lastPreedit(shown) == "ni", "composing preedit is presented");

  const auto focusOut = abc::addon::mapResult(first->reset());
  expect(commitsOf(focusOut).empty(), "focus loss reset does not commit");
  expect(lastPreedit(focusOut).empty(), "focus loss reset clears preedit");
  expect(first->snapshot().preedit.empty() &&
             second->snapshot().preedit == "a",
         "resetting one context does not change another context");
}

void testUnconsumedKeyForwardsAfterCommit() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  // 组合态下的标点是未处理键：提交原文后转发（engine.md 4.3）。
  abc::KeyCommand punctuation;
  punctuation.code = abc::KeyCode::kPunctuation;
  punctuation.unicode = U',';
  const abc::EngineResult result = session->handle(punctuation);
  expect(!result.consumed, "unhandled punctuation is not consumed");
  const auto commits = commitsOf(abc::addon::mapResult(result));
  expect(commits.size() == 1 && commits.front() == "n",
         "unconsumed key still commits the raw buffer first");
  expect(session->snapshot().preedit.empty(), "buffer is not left behind");
}

void testEnglishKeyNotConsumed() {
  FixedDictionary dictionary;
  abc::EngineConfig config;
  config.initialMode = abc::Mode::kEnglish;
  auto session = abc::createEngineSession(config, &dictionary);
  const abc::EngineResult result = session->handle(character(U'a'));
  expect(!result.consumed, "english mode does not consume letters");
  expect(abc::addon::mapResult(result)[0].text.empty(),
         "english mode produces no preedit");
  expect(commitsOf(abc::addon::mapResult(result)).empty(),
         "english mode produces no commit");
}

}  // namespace

int main() {
  testComposingPreedit();
  testSelectingArrowNavigation();
  testSelectCommitsCandidate();
  testSpaceSelectsHighlightedCandidate();
  testModeSwitchCommitsThenChangesMode();
  testResetClearsWithoutCommit();
  testSessionsAreIsolated();
  testUnconsumedKeyForwardsAfterCommit();
  testEnglishKeyNotConsumed();
  if (g_failed != 0) {
    std::cerr << g_failed << " session driver assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "abc_addon_session_driver: ok\n";
  return EXIT_SUCCESS;
}
