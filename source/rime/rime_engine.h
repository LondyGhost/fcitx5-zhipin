// rime_engine.h — 以 librime 为后端的 EngineSession 实现（B 方案）
//
// 设计要点：
//   - addon 层（abcengine.cpp）完全不变，只把 createEngineSession 换成
//     createRimeEngineSession；preedit/候选/按键语义仍由 EngineResult 表达。
//   - 智能ABC 的「打字不出候选、空格才出候选」不再需要 lua_filter：
//     我们自己就是前端，未「放出」时直接把候选列表清空即可。
//   - 「词后回落首音节单字」由我们自己追加到候选尾部（Rime 原生不做）。
//     音节表不内嵌：直接用 Rime 会话当 oracle，查询某串是否有单字候选。
#ifndef ABC_RIME_ENGINE_H_
#define ABC_RIME_ENGINE_H_

#include <memory>
#include <string>

#include "abc/engine.h"

namespace abc {

struct RimeOptions {
  std::string sharedDataDir;  // 默认 /usr/share/rime-data
  std::string userDataDir;    // 默认 $HOME/.local/share/fcitx5/rime
  std::string logDir;         // 默认 $HOME/.local/share/abc
  std::string schemaId;       // 默认 abc
};

// 进程内一次性初始化 librime（含部署）。可重复调用，幂等。
// 返回 false 表示 librime 不可用或初始化失败，调用方应回退到自研引擎。
bool initializeRime(const RimeOptions& options);
void finalizeRime();
bool rimeReady();

// 创建会话；librime 未就绪时返回 nullptr。
std::unique_ptr<EngineSession> createRimeEngineSession(const EngineConfig& config);

}  // namespace abc

#endif  // ABC_RIME_ENGINE_H_
