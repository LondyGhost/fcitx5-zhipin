// ui_mapping.h — EngineResult 到 UI 动作的映射（A4，无 fcitx5 依赖）。
//
// abcengine.cpp 里对 fcitx5 InputPanel 的写入与这里的规则一一对应：
// 先按 effects 顺序产出 commit/clear，再按快照产出 preedit 与候选。
// 抽出这一层是为了在没有 fcitx5 的环境下验证映射，而不是为了第二份缓冲。
#ifndef ABC_ADDON_UI_MAPPING_H_
#define ABC_ADDON_UI_MAPPING_H_

#include <string>
#include <vector>

#include "abc/engine.h"

namespace abc {
namespace addon {

struct UiAction {
  enum class Kind {
    kCommitText,        // InputContext::commitString
    kClearComposition,  // 清空 preedit 与候选
    kSetPreedit,        // InputPanel preedit / client preedit
    kSetCandidates,     // CommonCandidateList（含高亮）
    kClearCandidates,   // setCandidateList(nullptr)
  };
  Kind kind = Kind::kClearComposition;
  std::string text;
  std::vector<std::string> candidates;
  int32_t highlight = -1;
};

// preeditInClient 对应 AddonSettings.preeditInClient：
// false 时 kSetPreedit 的 text 为空（不向应用窗口送预编辑）。
std::vector<UiAction> mapResult(const abc::EngineResult& result,
                                bool preeditInClient);

}  // namespace addon
}  // namespace abc

#endif  // ABC_ADDON_UI_MAPPING_H_
