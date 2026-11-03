// abc_fcitx_config.h — fcitx5 配置界面定义。
//
// fcitx5-configtool 读取本 Configuration 的选项元数据（类型 + 描述 + 约束），
// 自动生成「附加组件 → 智能ABC → 配置」页面，无需自写 Qt 界面。
// 配置文件由 fcitx5 保存到 ~/.config/fcitx5/conf/abc.conf。
#ifndef ABC_ADDON_FCITX_CONFIG_H_
#define ABC_ADDON_FCITX_CONFIG_H_

#include <string>

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/key.h>

namespace abc {
namespace addon {

// 初始输入状态。
enum class AbcInitialMode { Chinese, English };
FCITX_CONFIG_ENUM_NAME(AbcInitialMode, "中文", "英文");

// 组合中按空格但没有候选时的策略。
enum class AbcSpacePolicy { CommitRaw, KeepBuffer };
FCITX_CONFIG_ENUM_NAME(AbcSpacePolicy, "提交原文", "保留编码");

// 中英切换时对未提交组合的处理。
enum class AbcSwitchPolicy { CommitRaw, Clear };
FCITX_CONFIG_ENUM_NAME(AbcSwitchPolicy, "提交原文", "清除编码");

FCITX_CONFIGURATION(
    AbcFcitxConfig,
    // 候选词数量：一页最多 9 个（数字键 1-9）。
    fcitx::Option<int, fcitx::IntConstrain> pageSize{
        this, "PageSize", "候选词数量", 9, fcitx::IntConstrain(1, 9)};
    // 初始输入状态：中文 / 英文。
    fcitx::Option<AbcInitialMode> initialMode{
        this, "InitialMode", "初始输入状态", AbcInitialMode::Chinese};
    // 输入缓冲上限：超限字符直接转发给应用。
    fcitx::Option<int, fcitx::IntConstrain> maxBufferChars{
        this, "MaxBufferChars", "最大编码长度", 64,
        fcitx::IntConstrain(1, 256)};
    // 空格无候选：提交原文 / 保留编码等待继续输入。
    fcitx::Option<AbcSpacePolicy> spaceNoCandidate{
        this, "SpaceNoCandidatePolicy", "空格无候选时",
        AbcSpacePolicy::CommitRaw};
    // 切换中英文时，未提交的编码如何处理。
    fcitx::Option<AbcSwitchPolicy> modeSwitchPolicy{
        this, "ModeSwitchCompositionPolicy", "切换中英文时未提交编码",
        AbcSwitchPolicy::CommitRaw};
    // 自研词典索引路径（仅在 librime 不可用时使用）。
    fcitx::Option<std::string> indexPath{this, "IndexPath", "词典索引路径",
                                         ""};
    // 中英文切换键。允许纯修饰键（Shift/Control/Alt/Super）。
    fcitx::Option<fcitx::Key, fcitx::KeyConstrain> modeSwitchKey{
        this, "ModeSwitchKey", "中英文切换键", fcitx::Key("Shift_L"),
        fcitx::KeyConstrain(fcitx::KeyConstrainFlags(
            fcitx::KeyConstrainFlag::AllowModifierOnly) |
            fcitx::KeyConstrainFlag::AllowModifierLess)};
    // 预编辑文本是否送入应用窗口（内联绘制）。
    fcitx::Option<bool> preeditInClient{
        this, "PreeditInClient", "预编辑文本送入应用窗口", false};
);

}  // namespace addon
}  // namespace abc

#endif  // ABC_ADDON_FCITX_CONFIG_H_
