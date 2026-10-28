// test_ui_mapping.cpp — preedit、commit、候选与未消费键的映射
// （engine.md 4.3、4.4；不依赖 fcitx5）。
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "ui_mapping.h"

namespace {

int g_failed = 0;

void expect(bool cond, const std::string& message) {
  if (!cond) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failed;
  }
}

abc::Candidate candidate(const std::string& text) {
  abc::Candidate item;
  item.text = text;
  item.code = "ni";
  item.weight = 1.0;
  item.source = "test";
  return item;
}

bool hasKind(const std::vector<abc::addon::UiAction>& actions,
             abc::addon::UiAction::Kind kind) {
  for (const auto& action : actions) {
    if (action.kind == kind) {
      return true;
    }
  }
  return false;
}

void testPreeditUpdate() {
  abc::EngineResult result;
  result.consumed = true;
  result.mode = abc::Mode::kComposing;
  result.preedit = "ni";
  result.candidates = {candidate("你"), candidate("尼")};
  result.pageCount = 1;
  result.highlight = -1;

  const auto actions = abc::addon::mapResult(result, true);
  expect(actions.size() == 2, "composing result has preedit + candidates");
  expect(actions[0].kind == abc::addon::UiAction::Kind::kSetPreedit &&
             actions[0].text == "ni",
         "preedit text is the normalized buffer");
  expect(actions[1].kind == abc::addon::UiAction::Kind::kSetCandidates &&
             actions[1].candidates.size() == 2 &&
             actions[1].candidates[0] == "你" && actions[1].highlight == -1,
         "candidate list keeps core order and highlight");

  const auto hidden = abc::addon::mapResult(result, false);
  expect(hidden[0].kind == abc::addon::UiAction::Kind::kSetPreedit &&
             hidden[0].text.empty(),
         "preeditInClient=false sends an empty client preedit");
}

void testCommitOrder() {
  abc::EngineResult result;
  result.consumed = true;
  result.mode = abc::Mode::kChineseEmpty;
  abc::Effect commit;
  commit.kind = abc::Effect::Kind::kCommitText;
  commit.text = "你";
  abc::Effect clear;
  clear.kind = abc::Effect::Kind::kClearComposition;
  result.effects = {commit, clear};

  const auto actions = abc::addon::mapResult(result, true);
  expect(actions.size() == 4, "effects precede the snapshot actions");
  expect(actions[0].kind == abc::addon::UiAction::Kind::kCommitText &&
             actions[0].text == "你",
         "commit text comes first");
  expect(actions[1].kind == abc::addon::UiAction::Kind::kClearComposition,
         "clear composition follows commit");
  expect(actions[2].kind == abc::addon::UiAction::Kind::kSetPreedit &&
             actions[2].text.empty(),
         "snapshot preedit is empty after commit");
  expect(actions[3].kind == abc::addon::UiAction::Kind::kClearCandidates,
         "snapshot clears the candidate list");
}

void testUnconsumedKeyStillAppliesEffects() {
  // engine.md 4.1.6 / 4.3：未消费键仍可能先提交原文再转发。
  abc::EngineResult result;
  result.consumed = false;
  result.mode = abc::Mode::kChineseEmpty;
  abc::Effect commit;
  commit.kind = abc::Effect::Kind::kCommitText;
  commit.text = "nihao";
  result.effects = {commit};

  const auto actions = abc::addon::mapResult(result, true);
  expect(actions[0].kind == abc::addon::UiAction::Kind::kCommitText &&
             actions[0].text == "nihao",
         "unconsumed key still commits the raw buffer");
  expect(!hasKind(actions, abc::addon::UiAction::Kind::kClearComposition) ||
             actions[0].kind == abc::addon::UiAction::Kind::kCommitText,
         "commit stays ahead of any later action");
}

void testDeactivateClearsWithoutCommit() {
  // reset()/deactivate 的结果：effects 为空、preedit 为空、无候选。
  abc::EngineResult result;
  result.consumed = false;
  result.mode = abc::Mode::kChineseEmpty;

  const auto actions = abc::addon::mapResult(result, true);
  expect(!hasKind(actions, abc::addon::UiAction::Kind::kCommitText),
         "deactivation reset does not commit");
  expect(actions[0].kind == abc::addon::UiAction::Kind::kSetPreedit &&
             actions[0].text.empty(),
         "deactivation clears preedit");
  expect(actions[1].kind == abc::addon::UiAction::Kind::kClearCandidates,
         "deactivation clears candidates");
}

void testEmptyCommitIsNotEmitted() {
  abc::EngineResult result;
  abc::Effect commit;
  commit.kind = abc::Effect::Kind::kCommitText;
  result.effects = {commit};
  const auto actions = abc::addon::mapResult(result, true);
  expect(actions[0].kind == abc::addon::UiAction::Kind::kCommitText &&
             actions[0].text.empty(),
         "empty commit is represented and skipped by the engine");
}

}  // namespace

int main() {
  testPreeditUpdate();
  testCommitOrder();
  testUnconsumedKeyStillAppliesEffects();
  testDeactivateClearsWithoutCommit();
  testEmptyCommitIsNotEmitted();
  if (g_failed != 0) {
    std::cerr << g_failed << " ui mapping assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "abc_addon_ui_mapping: ok\n";
  return EXIT_SUCCESS;
}
