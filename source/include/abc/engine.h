// abc/engine.h — 引擎会话接口（A0 维护）
// 契约：docs/interfaces/engine.md（转换表、转移表、生命周期、线程约束）。
// 实现方：A1（createEngineSession 与 EngineSession 具体类）。
// EngineSession 是输入缓冲与组合状态的唯一所有者；addon 不得持有缓冲。
#ifndef ABC_ENGINE_H_
#define ABC_ENGINE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "abc/key.h"

namespace abc {

// 输入模式。“停用”不是核心模式：addon 在输入法停用/焦点丢失时调用 reset()。
enum class Mode : int8_t {
  kEnglish = 0,       // 英文：除 kModeSwitch 外一律不消费
  kChineseEmpty = 1,  // 中文空缓冲
  kComposing = 2,     // 组合中：候选页预览（highlight = -1），数字不选择
  kSelecting = 3,     // 候选页激活：数字/空格选择（highlight >= 0）
};

inline constexpr uint32_t kMaxPageSize = 9;           // 数字 1..9 选择
inline constexpr uint32_t kDefaultPageSize = 9;
inline constexpr uint32_t kDefaultMaxBufferChars = 64;
inline constexpr uint32_t kAbsoluteMaxBufferChars = 256;

// 会话级行为策略（来源见 docs/interfaces/config.md）。
enum class SpaceNoCandidatePolicy : int8_t {
  kCommitRawAndForward = 0,  // 默认：提交原文缓冲，空格转发（不丢数据）
  kKeepBufferAndForward = 1, // 保留组合，空格转发
};

enum class ModeSwitchCompositionPolicy : int8_t {
  kCommitRaw = 0,         // 默认：切换时提交原文缓冲
  kClearComposition = 1,  // 切换时清除组合（用户显式操作）
};

// 构造会话时注入的不可变配置。工厂校验规则见 engine.md 第 3 节。
struct EngineConfig {
  Mode initialMode = Mode::kChineseEmpty;  // 仅允许 kEnglish / kChineseEmpty
  uint32_t pageSize = kDefaultPageSize;
  uint32_t maxBufferChars = kDefaultMaxBufferChars;
  SpaceNoCandidatePolicy spaceNoCandidatePolicy =
      SpaceNoCandidatePolicy::kCommitRawAndForward;
  ModeSwitchCompositionPolicy modeSwitchCompositionPolicy =
      ModeSwitchCompositionPolicy::kCommitRaw;
};

// 副作用命令；addon 按数组顺序执行（先 effects 后按快照同步 UI）。
struct Effect {
  enum class Kind : int8_t {
    kCommitText = 0,        // 提交 text（UTF-8）到应用
    kClearComposition = 1,  // 清除当前组合窗口
  };
  Kind kind = Kind::kClearComposition;
  std::string text;  // 仅 kCommitText 使用
};

enum class EngineError : int32_t {
  kNone = 0,
  kDictionaryUnavailable = 1,  // 保留值：会话未绑定可用词典（v1.0 不应出现）
  kDictionaryError = 2,        // 词典查询错误；按无候选处理并标注
  kInvalidCommand = 3,         // 命令字段非法（engine.md 2.2）
};

// 一次 handle/reset/snapshot 的完整输出：状态快照 + 有序副作用。
// 不变量见 engine.md 4.1；consumed == false 时 effects 仍可能非空。
struct EngineResult {
  bool consumed = false;
  Mode mode = Mode::kEnglish;
  std::string preedit;                // 预编辑 = 规范化缓冲；空 = 无
  std::vector<Candidate> candidates;  // 当前页候选（冻结排序的副本）
  uint32_t page = 0;                  // 当前页，0 基
  uint32_t pageCount = 0;             // 总页数；0 = 无候选页
  int32_t highlight = -1;             // 页内高亮 0 基；-1 = 无
  // 选择态下组字框默认显示高亮候选（智能ABC 手感）。分阶段选字时
  // preedit 已是「已选字 + 剩余编码」，置 true 让适配层不要覆盖它。
  bool keepPreedit = false;
  std::vector<Effect> effects;        // 按序执行
  EngineError error = EngineError::kNone;
};

// 输入会话。非线程安全：所有调用限于拥有该上下文的单线程（engine.md 第 7 节）。
class EngineSession {
 public:
  EngineSession() = default;
  virtual ~EngineSession() = default;
  EngineSession(const EngineSession&) = delete;
  EngineSession& operator=(const EngineSession&) = delete;

  // 处理一个按键命令，返回处理后的快照与副作用。
  virtual EngineResult handle(const KeyCommand& command) = 0;

  // 焦点丢失/输入法停用/上下文销毁前调用：清空组合，不提交任何文本，
  // mode = config.initialMode。
  virtual EngineResult reset() = 0;

  // 当前状态快照，无副作用。
  virtual EngineResult snapshot() const = 0;

  // 构造配置的副本。
  virtual const EngineConfig& config() const = 0;
};

// 会话工厂。由 A1 提供实现；声明由 A0 冻结。
// dictionary 必须非空，且在会话生存期内保持有效（会话借用，不拥有）。
// config 非法（engine.md 第 3 节）时返回 nullptr。
std::unique_ptr<EngineSession> createEngineSession(const EngineConfig& config,
                                                   Dictionary* dictionary);

}  // namespace abc

#endif  // ABC_ENGINE_H_
