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

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include "abc/dictionary.h"
#include "abc/version.h"
#include "ui_mapping.h"

// B 方案：librime 后端。缺模块时自动回退到自研引擎。
#if __has_include("rime_engine.h")
#define ABC_ADDON_HAS_RIME 1
#include "rime_engine.h"
#else
#define ABC_ADDON_HAS_RIME 0
#endif

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

// fcitx5 标准 addon 配置路径：XDG_CONFIG_HOME/fcitx5/conf/abc.conf。
std::string fcitxConfigPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    return std::string(xdg) + "/fcitx5/conf/abc.conf";
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home) + "/.config/fcitx5/conf/abc.conf";
  }
  return "abc.conf";
}

bool fileExists(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "r");
  if (f == nullptr) return false;
  std::fclose(f);
  return true;
}
#endif

#if ABC_ADDON_HAS_RIME
std::string homeDir() {
  const char* home = std::getenv("HOME");
  return (home != nullptr && home[0] != '\0') ? std::string(home) : std::string();
}

// Rime 用户目录：ABC_RIME_USER_DIR 可覆盖，默认与 fcitx5-rime 共用。
std::string rimeUserDir() {
  const char* env = std::getenv("ABC_RIME_USER_DIR");
  if (env != nullptr && env[0] != '\0') return env;
  const std::string home = homeDir();
  if (home.empty()) return "rime";
  return home + "/.local/share/fcitx5/rime";
}

abc::RimeOptions rimeOptions() {
  abc::RimeOptions opts;
  opts.sharedDataDir = "/usr/share/rime-data";
  opts.userDataDir = rimeUserDir();
  const std::string home = homeDir();
  opts.logDir = home.empty() ? "/tmp" : home + "/.local/share/abc";
  opts.schemaId = "abc";
  return opts;
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

bool isConfiguredModifierRelease(const fcitx::Key& key,
                                 ModeSwitchKey modeSwitchKey) {
  const uint32_t sym = static_cast<uint32_t>(key.sym());
  switch (modeSwitchKey) {
    case ModeSwitchKey::kShift:
      return (sym == keysym::kShiftL || sym == keysym::kShiftR) &&
             (key.isReleaseOfModifier(fcitx::Key("Shift_L")) ||
              key.isReleaseOfModifier(fcitx::Key("Shift_R")));
    case ModeSwitchKey::kControl:
      return (sym == keysym::kControlL || sym == keysym::kControlR) &&
             (key.isReleaseOfModifier(fcitx::Key("Control_L")) ||
              key.isReleaseOfModifier(fcitx::Key("Control_R")));
    case ModeSwitchKey::kAlt:
      return (sym == keysym::kAltL || sym == keysym::kAltR) &&
             (key.isReleaseOfModifier(fcitx::Key("Alt_L")) ||
              key.isReleaseOfModifier(fcitx::Key("Alt_R")));
    case ModeSwitchKey::kSuper:
      return (sym == keysym::kSuperL || sym == keysym::kSuperR) &&
             (key.isReleaseOfModifier(fcitx::Key("Super_L")) ||
              key.isReleaseOfModifier(fcitx::Key("Super_R")));
    case ModeSwitchKey::kNone:
      return false;
  }
  return false;
}

}  // namespace

AbcState::AbcState(AbcEngine* engine, fcitx::InputContext* ic)
    : engine_(engine), ic_(ic) {
  createSession();
}

void AbcState::createSession() {
  session_.reset();
#if ABC_ADDON_HAS_RIME
  // B 方案优先：每个上下文一个 Rime 会话。
  if (engine_->useRime()) {
    session_ = abc::createRimeEngineSession(engine_->engineConfig());
  }
#endif
  // 回退：librime 不可用时才加载自研索引（省 ~100MB RSS）。
  if (session_ == nullptr && engine_->dictionary() != nullptr) {
    session_ = abc::createEngineSession(engine_->engineConfig(),
                                        engine_->dictionary());
  }
}

void AbcState::rebuildSession() {
  // 配置变更：EngineConfig 是会话构造参数，只能重建。
  // 重建不提交未完成的组合（与 reset 语义一致）。
  clearUi();
  createSession();
  ic_->updatePreedit();
  ic_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
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
  //
  // 上屏（kCommitText）必须放在「清 UI + updateUserInterface」之后：
  // commitString 会让 fcitx5 立刻重绘输入窗，若此时候选列表还没清，
  // 旧候选会多显示一帧——表现为选词后候选框留下残影再消失。
  const std::vector<UiAction> actions = mapResult(result);
  std::vector<std::string> commits;
  for (const UiAction& action : actions) {
    switch (action.kind) {
      case UiAction::Kind::kCommitText:
        // 延后到 UI 收起之后再上屏。
        commits.push_back(action.text);
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
  for (const std::string& text : commits) {
    if (!text.empty()) {
      ic_->commitString(text);
    }
  }
}

void AbcState::setPreedit(const std::string& preedit) {
  auto& panel = ic_->inputPanel();
  fcitx::Text text = preeditText(preedit);
  // 预编辑只能交给一方绘制，否则会出现「拖影/重影」：
  //   - setClientPreedit  → 客户端在光标处内联绘制（GTK/Qt 声明了 Preedit 能力时）；
  //   - setPreedit        → fcitx 的输入窗自己画（classicui 无条件读这个字段）。
  // classicui 的 InputWindow::update() 不看客户端能力，只要面板 preedit 非空就画，
  // 因此两者同时设置时同一串字会被画两遍：内联一份 + 悬浮窗一份。
  const bool clientSide =
      engine_->preeditInClient() &&
      ic_->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
  if (clientSide) {
    panel.setClientPreedit(text);
    panel.setPreedit(fcitx::Text());
  } else {
    panel.setClientPreedit(fcitx::Text());
    panel.setPreedit(text);
  }
}

void AbcState::setCandidates(const std::vector<std::string>& candidates,
                             int32_t highlight) {
  auto list = std::make_unique<fcitx::CommonCandidateList>();
  list->setLabels(candidateLabels());
  list->setPageSize(static_cast<int>(candidates.size()));
  // 不强制布局：让 classicui 的「垂直候选列表」配置生效（智能ABC 是竖排）。
  // 智能ABC 的「当前候选」标记：高亮项后追加 ✓（仅显示，提交的仍是候选原文）。
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    std::string text = candidates[i];
    if (static_cast<int32_t>(i) == highlight) {
      text += " \u2713";
    }
    list->append<fcitx::DisplayOnlyCandidateWord>(fcitx::Text(text));
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
#if ABC_ADDON_HAS_RIME
  useRime_ = abc::initializeRime(rimeOptions());
  if (useRime_) {
    FCITX_INFO() << "abc: librime backend ready (schema=abc)";
  } else {
    FCITX_WARN() << "abc: librime backend unavailable, using builtin engine";
  }
#endif
#if ABC_ADDON_HAS_CONFIG
  // 首次运行：把旧的 ~/.config/abc/config.ini 迁移到 fcitx5 的配置体系。
  importLegacyConfigIfNeeded();
  applyFcitxConfig();

  // 自研索引路径仍从旧配置读取（迁移后同时存在于 fcitx5 配置里）。
  const abc::config::ParseResult parsed =
      abc::config::parseFile(userConfigPath());
  for (const std::string& warning : parsed.warnings) {
    FCITX_WARN() << "abc config: " << warning;
  }
  // 用 librime 时不再加载自研索引。
  if (!useRime_ && !parsed.addon.indexPath.empty()) {
    abc::DictionaryLoadResult loaded =
        abc::loadDictionaryFromFile(parsed.addon.indexPath);
    if (loaded.status == abc::DictionaryStatus::kOk) {
      dictionary_ = std::move(loaded.dictionary);
      FCITX_INFO() << "abc dictionary loaded";
    } else {
      // 索引缺失或损坏：不崩溃，引擎以降级模式运行（按键不消费）。
      FCITX_WARN() << "abc dictionary load failed: " << loaded.detail;
    }
  }
#else
  FCITX_ERROR() << "abc addon built without required config module";
#endif
}

void AbcEngine::importLegacyConfigIfNeeded() {
#if ABC_ADDON_HAS_CONFIG
  // fcitx5 配置已存在（用户改过设置）：以它为准，不再迁移。
  if (fileExists(fcitxConfigPath())) {
    return;
  }
  if (!fileExists(userConfigPath())) {
    return;
  }
  const abc::config::ParseResult parsed =
      abc::config::parseFile(userConfigPath());
  config_.pageSize.setValue(static_cast<int>(parsed.engine.pageSize));
  config_.maxBufferChars.setValue(static_cast<int>(parsed.engine.maxBufferChars));
  config_.initialMode.setValue(
      (parsed.engine.initialMode == abc::Mode::kEnglish)
          ? AbcInitialMode::English
          : AbcInitialMode::Chinese);
  config_.spaceNoCandidate.setValue(
      (parsed.engine.spaceNoCandidatePolicy ==
       abc::SpaceNoCandidatePolicy::kKeepBufferAndForward)
          ? AbcSpacePolicy::KeepBuffer
          : AbcSpacePolicy::CommitRaw);
  config_.modeSwitchPolicy.setValue(
      (parsed.engine.modeSwitchCompositionPolicy ==
       abc::ModeSwitchCompositionPolicy::kClearComposition)
          ? AbcSwitchPolicy::Clear
          : AbcSwitchPolicy::CommitRaw);
  config_.indexPath.setValue(parsed.addon.indexPath);
  config_.preeditInClient.setValue(parsed.addon.preeditInClient);
  if (!parsed.addon.modeSwitchKey.empty()) {
    // fcitx::Key 只认 "Shift_L" 这类 keysym 名，旧配置里的 "Shift" 需规范化。
    std::string keyName = parsed.addon.modeSwitchKey;
    if (keyName == "Shift") {
      keyName = "Shift_L";
    } else if (keyName == "Control" || keyName == "Ctrl") {
      keyName = "Control_L";
    } else if (keyName == "Alt") {
      keyName = "Alt_L";
    } else if (keyName == "Super") {
      keyName = "Super_L";
    }
    fcitx::Key key(keyName);
    config_.modeSwitchKey.setValue(key.isValid() ? key : fcitx::Key("Shift_L"));
  }
  fcitx::safeSaveAsIni(config_, fcitxConfigPath());
  FCITX_INFO() << "abc: migrated legacy config.ini to " << fcitxConfigPath();
#endif
}

void AbcEngine::applyFcitxConfig() {
  const int pageSize = *config_.pageSize;
  engineConfig_.pageSize =
      static_cast<uint32_t>(pageSize > 0 ? pageSize : 9);
  const int maxChars = *config_.maxBufferChars;
  engineConfig_.maxBufferChars =
      static_cast<uint32_t>(maxChars > 0 ? maxChars : 64);
  engineConfig_.initialMode = (*config_.initialMode == AbcInitialMode::Chinese)
                                  ? abc::Mode::kChineseEmpty
                                  : abc::Mode::kEnglish;
  engineConfig_.spaceNoCandidatePolicy =
      (*config_.spaceNoCandidate == AbcSpacePolicy::CommitRaw)
          ? abc::SpaceNoCandidatePolicy::kCommitRawAndForward
          : abc::SpaceNoCandidatePolicy::kKeepBufferAndForward;
  engineConfig_.modeSwitchCompositionPolicy =
      (*config_.modeSwitchPolicy == AbcSwitchPolicy::CommitRaw)
          ? abc::ModeSwitchCompositionPolicy::kCommitRaw
          : abc::ModeSwitchCompositionPolicy::kClearComposition;
  preeditInClient_ = *config_.preeditInClient;
  const fcitx::Key& key = *config_.modeSwitchKey;
  if (key.isValid()) {
    modeSwitchKey_ = parseModeSwitchKey(key.toString().c_str());
  }
}

void AbcEngine::setConfig(const fcitx::RawConfig& config) {
  // fcitx5 启动时会先读取 ~/.config/fcitx5/conf/abc.conf，再调用本函数。
  // 首次运行时该文件不存在，raw 为空；此时保留 importLegacyConfigIfNeeded()
  // 已从旧 config.ini 迁移进来的值，避免被默认值覆盖。
  const bool hasAny =
      config.subItemsSize() > 0 || !config.value().empty();
  if (hasAny) {
    config_.load(config, true);
  }
  applyFcitxConfig();
  configLoaded_ = true;
  // 配置项（页大小、初始状态、切换键等）是会话构造参数，需重建。
  instance_->inputContextManager().foreach ([this](fcitx::InputContext* ic) {
    if (AbcState* state = stateFor(ic)) {
      state->rebuildSession();
    }
    return true;
  });
  FCITX_INFO() << "abc: configuration applied (pageSize="
               << engineConfig_.pageSize << ")";
}

void AbcEngine::save() {
#if ABC_ADDON_HAS_CONFIG
  // fcitx5 保存 addon 配置时调用；显式落盘，避免仅存于内存。
  fcitx::safeSaveAsIni(config_, fcitxConfigPath());
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
    input.isModifierRelease = isConfiguredModifierRelease(key, modeSwitchKey_);
  }

  const KeyCommand command = toKeyCommand(input, modeSwitchKey_);
  // 按键释放事件不进入状态机。
  // engine.md 2.1 把释放一律映射为 kUnknown，而 4.3 的「未处理键」策略会
  // 提交缓冲原文并清空组合；真实键盘每个字母都会先 press 再 release，若不拦截，
  // 刚出现的预编辑会在 release 时立刻被提交上屏（表现为「字母闪一下就上屏」）。
  // 唯一例外是配置热键的修饰键释放（映射为 kModeSwitch），它必须进状态机。
  if (!isDispatchableKey(command, input.isRelease)) {
    return;
  }
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
