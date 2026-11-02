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

// 映射总是产出预编辑文本：kComposing 为字母缓冲，kSelecting 为当前高亮候选
// （对齐智能ABC/QQ拼音的组字框：按空格后框里换成候选词）。
// 至于这段文本是送到客户端内联，还是由输入窗自己画，由执行层按
// preeditInClient 配置与客户端能力决定，不在本层。
//
// 候选呈现策略：只有 kSelecting（空格后进入的选择态）才产出 kSetCandidates；
// kComposing 阶段一律 kClearCandidates，即「先看字母、按空格才出候选」。
std::vector<UiAction> mapResult(const abc::EngineResult& result);

}  // namespace addon
}  // namespace abc

#endif  // ABC_ADDON_UI_MAPPING_H_
