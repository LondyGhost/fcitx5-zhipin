// abc/key.h — 按键命令类型（A0 维护）
// 契约：docs/interfaces/engine.md 第 2 节（含 A4 适配层转换表）。
// 纯核心接口：仅依赖 C++ 标准库，禁止包含 fcitx5/X11/Wayland。
#ifndef ABC_KEY_H_
#define ABC_KEY_H_

#include <cstdint>

namespace abc {

// 语义化按键命令。fcitx5 键事件由 addon 适配层按 engine.md 2.1 转换表
// 翻译为本类型；核心不感知任何输入框架类型。
enum class KeyCode {
  kUnknown = 0,      // 其余一切按键；按未处理键策略处理（engine.md 4.3）
  kCharacter = 1,    // 缓冲字符；unicode 携带 UTF-32 码点
  kDigit = 2,        // 候选选择数字；digit 携带 1..9
  kPunctuation = 3,  // 标点；unicode 携带 UTF-32 码点
  kBackspace = 4,
  kEnter = 5,
  kSpace = 6,
  kEscape = 7,
  kPageUp = 8,
  kPageDown = 9,
  kModeSwitch = 10,  // 配置的中英文切换热键，由适配层识别后发出
  kLeft = 11,        // 方向键左：选择态内移动高亮（不提交）
  kRight = 12,       // 方向键右：选择态内移动高亮（不提交）
  kUp = 13,          // 方向键上：选择态翻页（等价 kPageUp）
  kDown = 14,        // 方向键下：选择态翻页（等价 kPageDown）
};

struct KeyCommand {
  KeyCode code = KeyCode::kUnknown;
  char32_t unicode = 0;  // 仅 kCharacter / kPunctuation 使用；必须非 0
  uint8_t digit = 0;     // 仅 kDigit 使用；合法值 1..9
};

}  // namespace abc

#endif  // ABC_KEY_H_
