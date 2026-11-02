// ui_mapping.cpp — ui_mapping.h 的实现。
#include "ui_mapping.h"

namespace abc {
namespace addon {

std::vector<UiAction> mapResult(const abc::EngineResult& result) {
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

  // 组字框内容：组合中是字母缓冲；选择态换成当前高亮候选（智能ABC 手感）。
  // 分阶段选字（keepPreedit）时核心已给出「已选字 + 剩余编码」，不要覆盖。
  std::string preeditText = result.preedit;
  if (!result.keepPreedit && result.mode == abc::Mode::kSelecting &&
      result.highlight >= 0 &&
      static_cast<std::size_t>(result.highlight) < result.candidates.size()) {
    preeditText =
        result.candidates[static_cast<std::size_t>(result.highlight)].text;
  }
  // 智能ABC 的翻页指示：多页时在组字框末尾画 ◀ ▶。
  // 纯装饰（fcitx5 的经典 UI 不支持在组字框里放可点按钮），翻页仍用 ←/→。
  if (result.mode == abc::Mode::kSelecting && result.pageCount > 1) {
    preeditText += "  \u25C0 \u25B6";
  }
  UiAction preedit;
  preedit.kind = UiAction::Kind::kSetPreedit;
  preedit.text = std::move(preeditText);
  actions.push_back(std::move(preedit));

  UiAction candidates;
  // 交互约定（对齐 Rime / 智能ABC 手感）：组合中（kComposing）只显示字母串，
  // 不弹候选窗；按空格进入 kSelecting 后才显示候选列表。
  // 候选数据仍由核心在 Composing 阶段查好（space 才能直接进选择态），
  // 这里只决定「是否呈现」。
  const bool showCandidates = result.mode == abc::Mode::kSelecting &&
                              !result.candidates.empty();
  if (!showCandidates) {
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
