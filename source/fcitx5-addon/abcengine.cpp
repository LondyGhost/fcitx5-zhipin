// abcengine.cpp — fcitx5 原生引擎 addon 的适配实现（A4）。
//
// effects 执行顺序严格按 docs/interfaces/engine.md 4.4：
//   1. 按数组顺序执行 effects（CommitText / ClearComposition）；
//   2. 再按快照同步 preedit、候选列表、高亮、页码；
//   3. consumed == false 时不调用 filterAndAccept，键交还 fcitx5。
// consumed == false 时 effects 仍可能非空（未处理键策略），必须照常执行。
#include "abcengine.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include "abc/dictionary.h"
#include "abc/version.h"
#include "ui_mapping.h"

#if __has_include("abc_config.h")
#define ABC_ADDON_HAS_CONFIG 1
#include "abc_config.h"
#else
#define ABC_ADDON_HAS_CONFIG 0
#endif

namespace abc {
namespace addon {
namespace {

constexpr char kPropertyName[] = "abcState";

#if ABC_ADDON_HAS_CONFIG
// 用户配置查找顺序：XDG_CONFIG_HOME/abc/config.ini，否则 ~/.config/abc/config.ini。
std::string userConfigPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    return std::string(xdg) + "/abc/config.ini";
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.config/abc/config.ini";
  }
  return "abc/config.ini";
}
#endif

fcitx::Text preeditText(const std::string& preedit) {
  fcitx::Text text;
  text.append(preedit, fcitx::TextFormatFlag::Underline);
  if (!preedit.empty()) {
    text.setCursor(static_cast<int>(preedit.size()));
  }
  return text;
}

// 候选标签固定 1..9：与核心 kDigit 的页内序号一致。
std::vector<std::string> candidateLabels() {
  std::vector<std::string> labels;
  labels.reserve(9);
  for (int i = 1; i <= 9; ++i) {
    labels.emplace_back(std::to_string(i) + ".");
  }
  return labels;
}

const char* modeSwitchKeysymName(ModeSwitchKey key) {
  switch (key) {
    case ModeSwitchKey::kShift:
      return "Shift_L";
    case ModeSwitchKey::kControl:
      return "Control_L";
    case ModeSwitchKey::kAlt:
      return "Alt_L";
    case ModeSwitchKey::kSuper:
      return "Super_L";
    case ModeSwitchKey::kNone:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

AbcState::AbcState(AbcEngine* engine, fcitx::InputContext* ic)
    : engine_(engine), ic_(ic) {
  // 词典不可用时不创建会话：按键全部不消费，输入法保持可用（engine.md 8）。
  if (engine_->dictionary() != nullptr) {
    session_ = abc::createEngineSession(engine_->engineConfig(),
                                        engine_->dictionary());
  }
}

AbcState::~AbcState() {
  // 上下文销毁：清掉面板上可能残留的 preedit/候选，再释放会话。
  // 不提交——销毁不是用户确认动作。
  clearUi();
  session_.reset();
}

void AbcState::reset() {
  if (session_ == nullptr) {
    return;
  }
  // reset() 不提交（engine.md 第 6 节），只清 UI。
  session_->reset();
  clearUi();
  ic_->updatePreedit();
  ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AbcState::redraw() {
  if (session_ == nullptr) {
    return;
  }
  apply(session_->snapshot());
}

void AbcState::apply(const abc::EngineResult& result) {
  // 4.4：先按 effects 顺序执行副作用，再按快照同步 UI。
  // 规则与 ui_mapping.cpp 共用，测试覆盖的就是这里执行的序列。
  for (const UiAction& action :
       mapResult(result, engine_->preeditInClient())) {
    switch (action.kind) {
      case UiAction::Kind::kCommitText:
        if (!action.text.empty()) {
          ic_->commitString(action.text);
        }
        break;
      case UiAction::Kind::kClearComposition:
        clearUi();
        break;
      case UiAction::Kind::kSetPreedit:
        setPreedit(action.text);
        break;
      case UiAction::Kind::kSetCandidates:
        setCandidates(action.candidates, action.highlight);
        break;
      case UiAction::Kind::kClearCandidates:
        ic_->inputPanel().setCandidateList(nullptr);
        break;
    }
  }
  ic_->updatePreedit();
  ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AbcState::setPreedit(const std::string& preedit) {
  auto& panel = ic_->inputPanel();
  fcitx::Text text = preeditText(preedit);
  panel.setPreedit(text);
  if (engine_->preeditInClient()) {
    panel.setClientPreedit(text);
  } else {
    panel.setClientPreedit(fcitx::Text());
  }
}

void AbcState::setCandidates(const std::vector<std::string>& candidates,
                             int32_t highlight) {
  auto list = std::make_unique<fcitx::CommonCandidateList>();
  list->setLabels(candidateLabels());
  list->setPageSize(static_cast<int>(candidates.size()));
  list->setLayoutHint(fcitx::CandidateLayoutHint::Horizontal);
  for (const std::string& candidate : candidates) {
    list->append<fcitx::DisplayOnlyCandidateWord>(fcitx::Text(candidate));
  }
  if (highlight >= 0 && highlight < static_cast<int32_t>(candidates.size())) {
    list->setGlobalCursorIndex(highlight);
  }
  ic_->inputPanel().setCandidateList(std::move(list));
}

void AbcState::clearUi() {
  auto& panel = ic_->inputPanel();
  panel.setPreedit(fcitx::Text());
  panel.setClientPreedit(fcitx::Text());
  panel.setCandidateList(nullptr);
}

AbcEngine::AbcEngine(fcitx::Instance* instance)
    : instance_(instance),
      factory_([this](fcitx::InputContext& ic) {
        return new AbcState(this, &ic);
      }) {
  loadUserConfig();
  instance_->inputContextManager().registerProperty(kPropertyName, &factory_);
}

AbcEngine::~AbcEngine() = default;

void AbcEngine::loadUserConfig() {
#if ABC_ADDON_HAS_CONFIG
  const abc::config::ParseResult parsed =
      abc::config::parseFile(userConfigPath());
  engineConfig_ = parsed.engine;
  preeditInClient_ = parsed.addon.preeditInClient;
  modeSwitchKey_ = parseModeSwitchKey(parsed.addon.modeSwitchKey.c_str());
  for (const std::string& warning : parsed.warnings) {
    FCITX_WARN() << "abc config: " << warning;
  }
  if (!parsed.addon.indexPath.empty()) {
    abc::DictionaryLoadResult loaded =
        abc::loadDictionaryFromFile(parsed.addon.indexPath);
    if (loaded.status == abc::DictionaryStatus::kOk) {
      dictionary_ = std::move(loaded.dictionary);
    } else {
      // 索引缺失或损坏：不崩溃，引擎以降级模式运行（按键不消费）。
      FCITX_WARN() << "abc dictionary load failed: " << loaded.detail;
    }
  }
#else
  // A5 配置模块未纳入本次构建：使用契约默认值。
  modeSwitchKey_ = ModeSwitchKey::kShift;
  FCITX_INFO() << "abc config module unavailable; using contract defaults ("
               << ABC_INTERFACE_VERSION_STRING << ")";
#endif
}

AbcState* AbcEngine::stateFor(fcitx::InputContext* ic) {
  return ic->propertyFor(&factory_);
}

void AbcEngine::keyEvent(const fcitx::InputMethodEntry& entry,
                         fcitx::KeyEvent& event) {
  FCITX_UNUSED(entry);
  AbcState* state = stateFor(event.inputContext());
  if (state == nullptr) {
    return;
  }

  if (state->session() == nullptr) {
    return;
  }
  const fcitx::Key& key = event.key();
  if (!key.isValid()) {
    return;
  }

  KeyInput input;
  input.keysym = static_cast<uint32_t>(key.sym());
  input.states = static_cast<uint32_t>(key.states().toInteger());
  input.isRelease = event.isRelease();
  if (input.isRelease) {
    if (const char* name = modeSwitchKeysymName(modeSwitchKey_)) {
      input.isModifierRelease = key.isReleaseOfModifier(fcitx::Key(name));
    }
  }

  const KeyCommand command = toKeyCommand(input, modeSwitchKey_);
  const abc::EngineResult result = state->session()->handle(command);
  state->apply(result);
  // 4.4 第 3 步：只有核心声明消费时才过滤该键。
  if (result.consumed) {
    event.filterAndAccept();
  }
}

void AbcEngine::activate(const fcitx::InputMethodEntry& entry,
                         fcitx::InputContextEvent& event) {
  FCITX_UNUSED(entry);
  if (AbcState* state = stateFor(event.inputContext())) {
    state->redraw();
  }
}

void AbcEngine::deactivate(const fcitx::InputMethodEntry& entry,
                           fcitx::InputContextEvent& event) {
  FCITX_UNUSED(entry);
  // fcitx5 5.1.23：focusOut 只提交 client preedit，不调用 engine->reset()。
  // 停用时必须主动清组合，否则切走再切回会看到陈旧 preedit。
  if (AbcState* state = stateFor(event.inputContext())) {
    state->reset();
  }
}

void AbcEngine::reset(const fcitx::InputMethodEntry& entry,
                      fcitx::InputContextEvent& event) {
  FCITX_UNUSED(entry);
  if (AbcState* state = stateFor(event.inputContext())) {
    state->reset();
  }
}

}  // namespace addon
}  // namespace abc

FCITX_ADDON_FACTORY(abc::addon::AbcEngineFactory);
