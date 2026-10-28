// ui_mapping.cpp — ui_mapping.h 的实现。
#include "ui_mapping.h"

namespace abc {
namespace addon {

std::vector<UiAction> mapResult(const abc::EngineResult& result,
                                bool preeditInClient) {
  std::vector<UiAction> actions;
  actions.reserve(result.effects.size() + 2);

  for (const abc::Effect& effect : result.effects) {
    UiAction action;
    switch (effect.kind) {
      case abc::Effect::Kind::kCommitText:
        action.kind = UiAction::Kind::kCommitText;
        action.text = effect.text;
        break;
      case abc::Effect::Kind::kClearComposition:
        action.kind = UiAction::Kind::kClearComposition;
        break;
    }
    actions.push_back(std::move(action));
  }

  UiAction preedit;
  preedit.kind = UiAction::Kind::kSetPreedit;
  if (preeditInClient) {
    preedit.text = result.preedit;
  }
  actions.push_back(std::move(preedit));

  UiAction candidates;
  if (result.candidates.empty()) {
    candidates.kind = UiAction::Kind::kClearCandidates;
  } else {
    candidates.kind = UiAction::Kind::kSetCandidates;
    candidates.highlight = result.highlight;
    candidates.candidates.reserve(result.candidates.size());
    for (const abc::Candidate& candidate : result.candidates) {
      candidates.candidates.push_back(candidate.text);
    }
  }
  actions.push_back(std::move(candidates));
  return actions;
}

}  // namespace addon
}  // namespace abc
