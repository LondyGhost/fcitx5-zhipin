// abcengine.h — fcitx5 原生输入法引擎 addon（A4 薄适配层）。
//
// 职责（docs/独立引擎整体方案.md 4.1、tasks/A4）：
//   fcitx5 KeyEvent → abc::KeyCommand → EngineSession::handle →
//   fcitx5 commit / preedit / candidate list。
// 适配层不持有输入缓冲、不解析词库、不排序候选；缓冲唯一所有者是
// abc::EngineSession（每个输入上下文一个，挂在 InputContextProperty 上）。
//
// 目标 API：fcitx5 5.1.23（目标机实测）。该版本没有独立的
// destroyInputContext 钩子，上下文销毁清理靠属性析构；focusOut 由 fcitx5
// 核心提交 client preedit 后调用 deactivate()，本引擎在 deactivate 中
// reset() 会话，避免陈旧组合。
#ifndef ABC_ADDON_ABCENGINE_H_
#define ABC_ADDON_ABCENGINE_H_

#include <memory>
#include <string>

#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "abc/engine.h"
#include "abc_fcitx_config.h"
#include "keyconv.h"

namespace abc {
namespace config {
struct AddonSettings;
}  // namespace config
}  // namespace abc

namespace abc {
namespace addon {

class AbcEngine;

// 每个输入上下文一份：独占一个 EngineSession。
// 析构即上下文销毁：先清 UI 再释放会话，保证不留陈旧 preedit。
class AbcState : public fcitx::InputContextProperty {
 public:
  explicit AbcState(AbcEngine* engine, fcitx::InputContext* ic);
  ~AbcState() override;

  AbcState(const AbcState&) = delete;
  AbcState& operator=(const AbcState&) = delete;

  void apply(const abc::EngineResult& result);
  // 焦点丢失 / 引擎停用 / 显式 reset：清空组合，不提交。
  void reset();
  // 无按键重绘（activate 时恢复显示）。
  void redraw();
  // 配置变更后重建会话（EngineConfig 是会话构造参数，不可原地修改）。
  void rebuildSession();

  abc::EngineSession* session() const { return session_.get(); }

 private:
  void createSession();
  void setPreedit(const std::string& preedit);
  void setCandidates(const std::vector<std::string>& candidates,
                     int32_t highlight);
  void clearUi();

  AbcEngine* engine_;
  fcitx::InputContext* ic_;
  std::unique_ptr<abc::EngineSession> session_;
};

class AbcEngine : public fcitx::InputMethodEngine {
 public:
  explicit AbcEngine(fcitx::Instance* instance);
  ~AbcEngine() override;

  AbcEngine(const AbcEngine&) = delete;
  AbcEngine& operator=(const AbcEngine&) = delete;

  void keyEvent(const fcitx::InputMethodEntry& entry,
                fcitx::KeyEvent& event) override;
  void activate(const fcitx::InputMethodEntry& entry,
                fcitx::InputContextEvent& event) override;
  void deactivate(const fcitx::InputMethodEntry& entry,
                  fcitx::InputContextEvent& event) override;
  void reset(const fcitx::InputMethodEntry& entry,
             fcitx::InputContextEvent& event) override;

  // fcitx5 配置界面：返回 Configuration 后，fcitx5-configtool 会据此自动
  // 生成「附加组件 → 智能ABC → 配置」页面（abc.conf 需 Configurable=True）。
  const fcitx::Configuration* getConfig() const override { return &config_; }
  void setConfig(const fcitx::RawConfig& config) override;
  void save() override;

  const abc::EngineConfig& engineConfig() const { return engineConfig_; }
  abc::Dictionary* dictionary() const { return dictionary_.get(); }
  ModeSwitchKey modeSwitchKey() const { return modeSwitchKey_; }
  bool preeditInClient() const { return preeditInClient_; }
  // B 方案：librime 后端是否就绪。就绪时每个上下文创建 Rime 会话，
  // 不再加载自研索引（省 ~100MB RSS）。
  bool useRime() const { return useRime_; }

 private:
  AbcState* stateFor(fcitx::InputContext* ic);
  void loadUserConfig();
  // 把 fcitx5 配置项转换到引擎运行配置，并重建所有上下文的会话。
  void applyFcitxConfig();
  // 首次启动时从旧的 ~/.config/abc/config.ini 迁移配置。
  void importLegacyConfigIfNeeded();

  fcitx::Instance* instance_;
  fcitx::FactoryFor<AbcState> factory_;
  std::unique_ptr<abc::Dictionary> dictionary_;
  abc::EngineConfig engineConfig_;
  AbcFcitxConfig config_;
  bool configLoaded_ = false;
  ModeSwitchKey modeSwitchKey_ = ModeSwitchKey::kShift;
  bool preeditInClient_ = true;
  bool useRime_ = false;
};

class AbcEngineFactory : public fcitx::AddonFactory {
 public:
  fcitx::AddonInstance* create(fcitx::AddonManager* manager) override {
    return new AbcEngine(manager->instance());
  }
};

}  // namespace addon
}  // namespace abc

#endif  // ABC_ADDON_ABCENGINE_H_
