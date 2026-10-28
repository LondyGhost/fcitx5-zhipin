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

  const auto mapped = abc::addon::mapResult(session->handle(character(U'n')), true);
  expect(lastPreedit(mapped) == "n", "letter updates preedit");
  expect(commitsOf(mapped).empty(), "composing does not commit");
  bool sawCandidate = false;
  for (const auto& action : mapped) {
    if (action.kind == abc::addon::UiAction::Kind::kSetCandidates &&
        !action.candidates.empty() && action.candidates.front() == "你") {
      sawCandidate = true;
    }
  }
  expect(sawCandidate, "preedit update also refreshes candidates");
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
  const auto mapped = abc::addon::mapResult(session->handle(digit), true);
  const auto commits = commitsOf(mapped);
  expect(commits.size() == 1 && commits.front() == "你",
         "digit selection commits the candidate");
  expect(lastPreedit(mapped).empty(), "commit clears preedit");
}

void testResetClearsWithoutCommit() {
  FixedDictionary dictionary;
  auto session = abc::createEngineSession(abc::EngineConfig{}, &dictionary);
  session->handle(character(U'n'));
  const auto mapped = abc::addon::mapResult(session->reset(), true);
  expect(commitsOf(mapped).empty(), "reset/deactivation commits nothing");
  expect(lastPreedit(mapped).empty(), "reset clears preedit");
  expect(session->snapshot().mode == abc::Mode::kChineseEmpty,
         "reset returns to the configured initial mode");
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
  const auto commits = commitsOf(abc::addon::mapResult(result, true));
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
  expect(abc::addon::mapResult(result, true)[0].text.empty(),
         "english mode produces no preedit");
  expect(commitsOf(abc::addon::mapResult(result, true)).empty(),
         "english mode produces no commit");
}

}  // namespace

int main() {
  testComposingPreedit();
  testSelectCommitsCandidate();
  testResetClearsWithoutCommit();
  testUnconsumedKeyForwardsAfterCommit();
  testEnglishKeyNotConsumed();
  if (g_failed != 0) {
    std::cerr << g_failed << " session driver assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "abc_addon_session_driver: ok\n";
  return EXIT_SUCCESS;
}
