// keyconv.h — fcitx5 键事件到 abc::KeyCommand 的转换（A4）。
//
// 契约：docs/interfaces/engine.md 2.1（唯一合法映射）。
//
// 本头不包含任何 fcitx5/X11/Wayland 头：addon 与单元测试共用同一实现。
// 调用方把 fcitx5 Key 拆成原始 keysym、修饰状态位、"是否按键释放"和
// "是否修饰键释放"（fcitx::Key::isReleaseOfModifier）后传入。
// keysym 取值与 fcitx5 的 FcitxKeySym（X11 keysym）一致，常量集中定义于此。
#ifndef ABC_ADDON_KEYCONV_H_
#define ABC_ADDON_KEYCONV_H_

#include <cstdint>

#include "abc/key.h"

namespace abc {
namespace addon {

// 与 fcitx-utils/keysymgen.h 中 FcitxKeySym 一致的子集。
namespace keysym {
constexpr uint32_t kBackSpace = 0xff08;
constexpr uint32_t kReturn = 0xff0d;
constexpr uint32_t kEscape = 0xff1b;
constexpr uint32_t kPrior = 0xff55;  // PageUp
constexpr uint32_t kNext = 0xff56;   // PageDown
constexpr uint32_t kKpEnter = 0xff8d;
constexpr uint32_t kKp0 = 0xffb0;
constexpr uint32_t kKp9 = 0xffb9;
constexpr uint32_t kKpPrior = 0xff9a;
constexpr uint32_t kKpNext = 0xff9b;
constexpr uint32_t kShiftL = 0xffe1;
constexpr uint32_t kShiftR = 0xffe2;
constexpr uint32_t kSpace = 0x0020;
constexpr uint32_t kDigit0 = 0x0030;
constexpr uint32_t kDigit9 = 0x0039;
constexpr uint32_t kUpperA = 0x0041;
constexpr uint32_t kUpperZ = 0x005a;
constexpr uint32_t kLowerA = 0x0061;
constexpr uint32_t kLowerZ = 0x007a;
}  // namespace keysym

// 与 fcitx::KeyState 一致的修饰位。
namespace state {
constexpr uint32_t kShift = 1u << 0;
constexpr uint32_t kCtrl = 1u << 2;
constexpr uint32_t kAlt = 1u << 3;
constexpr uint32_t kSuper = 1u << 6;
constexpr uint32_t kSuper2 = 1u << 26;  // Gtk virtual Super
constexpr uint32_t kMeta = 1u << 28;
// 任一组合修饰存在时，字母/数字/标点不得进入缓冲（engine.md 2.1）。
constexpr uint32_t kCombo = kCtrl | kAlt | kSuper | kSuper2 | kMeta;
}  // namespace state

struct KeyInput {
  uint32_t keysym = 0;    // 规范化后的 keysym（fcitx::KeyEvent::key().sym()）
  uint32_t states = 0;    // 修饰状态位
  bool isRelease = false; // 按键释放事件
  // 该释放事件是否是 modeSwitchKey 的修饰键释放
  // （fcitx::Key::isReleaseOfModifier）。
  bool isModifierRelease = false;
};

// modeSwitchKey 支持的热键。config.md 默认 "Shift"；其余为常用别名。
enum class ModeSwitchKey {
  kNone = 0,  // 未配置或无法识别：不产生 kModeSwitch
  kShift,     // Shift / Shift_L / Shift_R
  kControl,   // Control / Control_L / Control_R
  kAlt,       // Alt / Alt_L / Alt_R
  kSuper,     // Super / Super_L / Super_R
};

// 解析 config.md 的 modeSwitchKey 字符串（fcitx5 键名）。
// 无法识别时返回 kNone：切换功能关闭，按键走常规转换表，不静默丢键。
ModeSwitchKey parseModeSwitchKey(const char* name);

// engine.md 2.1 转换表的唯一实现。
// 按键释放一律为 kUnknown（不消费、交还 fcitx5），唯一例外是修饰键热键的
// 释放事件，它映射为 kModeSwitch。
KeyCommand toKeyCommand(const KeyInput& input, ModeSwitchKey modeSwitchKey);

}  // namespace addon
}  // namespace abc

#endif  // ABC_ADDON_KEYCONV_H_
