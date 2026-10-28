// keyconv.cpp — keyconv.h 的实现。不包含 fcitx5 头。
#include "keyconv.h"

#include <cstring>

namespace abc {
namespace addon {
namespace {

bool nameEquals(const char* name, const char* expected) {
  return name != nullptr && std::strcmp(name, expected) == 0;
}

bool isPageUp(uint32_t sym) {
  return sym == keysym::kPrior || sym == keysym::kKpPrior;
}

bool isPageDown(uint32_t sym) {
  return sym == keysym::kNext || sym == keysym::kKpNext;
}

}  // namespace

ModeSwitchKey parseModeSwitchKey(const char* name) {
  if (nameEquals(name, "Shift") || nameEquals(name, "Shift_L") ||
      nameEquals(name, "Shift_R")) {
    return ModeSwitchKey::kShift;
  }
  if (nameEquals(name, "Control") || nameEquals(name, "Control_L") ||
      nameEquals(name, "Control_R") || nameEquals(name, "Ctrl")) {
    return ModeSwitchKey::kControl;
  }
  if (nameEquals(name, "Alt") || nameEquals(name, "Alt_L") ||
      nameEquals(name, "Alt_R")) {
    return ModeSwitchKey::kAlt;
  }
  if (nameEquals(name, "Super") || nameEquals(name, "Super_L") ||
      nameEquals(name, "Super_R")) {
    return ModeSwitchKey::kSuper;
  }
  return ModeSwitchKey::kNone;
}

KeyCommand toKeyCommand(const KeyInput& input, ModeSwitchKey modeSwitchKey) {
  KeyCommand command;

  // 按键释放：只有配置热键对应的修饰键释放才是切换动作。
  if (input.isRelease) {
    if (input.isModifierRelease && modeSwitchKey != ModeSwitchKey::kNone) {
      command.code = KeyCode::kModeSwitch;
    }
    return command;
  }

  const uint32_t sym = input.keysym;
  const bool combo = (input.states & state::kCombo) != 0;

  // 功能键不受组合修饰影响：带 Ctrl/Alt 的 Backspace 等仍按语义键处理，
  // 是否消费由核心状态机决定。
  if (sym == keysym::kBackSpace) {
    command.code = KeyCode::kBackspace;
    return command;
  }
  if (sym == keysym::kReturn || sym == keysym::kKpEnter) {
    command.code = KeyCode::kEnter;
    return command;
  }
  if (sym == keysym::kEscape) {
    command.code = KeyCode::kEscape;
    return command;
  }
  if (isPageUp(sym)) {
    command.code = KeyCode::kPageUp;
    return command;
  }
  if (isPageDown(sym)) {
    command.code = KeyCode::kPageDown;
    return command;
  }

  // 组合修饰下的可打印键一律 kUnknown，禁止进入缓冲（engine.md 2.1）。
  if (combo) {
    return command;
  }

  if (sym == keysym::kSpace) {
    command.code = KeyCode::kSpace;
    return command;
  }

  const bool upper = sym >= keysym::kUpperA && sym <= keysym::kUpperZ;
  const bool lower = sym >= keysym::kLowerA && sym <= keysym::kLowerZ;
  if (upper || lower) {
    // 推荐形式：大写规范化为小写后交给核心（engine.md 2.1）。
    command.code = KeyCode::kCharacter;
    command.unicode = lower ? sym : sym + (keysym::kLowerA - keysym::kUpperA);
    return command;
  }

  // 主键盘数字与小键盘数字等价；1..9 选择候选，0 是标点。
  int digit = -1;
  if (sym >= keysym::kDigit0 && sym <= keysym::kDigit9) {
    digit = static_cast<int>(sym - keysym::kDigit0);
  } else if (sym >= keysym::kKp0 && sym <= keysym::kKp9) {
    digit = static_cast<int>(sym - keysym::kKp0);
  }
  if (digit >= 1 && digit <= 9) {
    command.code = KeyCode::kDigit;
    command.digit = static_cast<uint8_t>(digit);
    return command;
  }
  if (digit == 0) {
    command.code = KeyCode::kPunctuation;
    command.unicode = U'0';
    return command;
  }

  // 其余可打印 ASCII（标点）。控制字符与非 ASCII keysym 落为 kUnknown。
  if (sym >= 0x21 && sym <= 0x7e) {
    command.code = KeyCode::kPunctuation;
    command.unicode = sym;
    return command;
  }
  return command;
}

}  // namespace addon
}  // namespace abc
