// test_keyconv.cpp — engine.md 2.1 转换表（不依赖 fcitx5）。
#include <cstdlib>
#include <iostream>
#include <string>

#include "keyconv.h"

namespace {

int g_failed = 0;

void expect(bool cond, const std::string& message) {
  if (!cond) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failed;
  }
}

abc::addon::KeyInput key(uint32_t sym, uint32_t states = 0, bool release = false,
                         bool modifierRelease = false) {
  abc::addon::KeyInput input;
  input.keysym = sym;
  input.states = states;
  input.isRelease = release;
  input.isModifierRelease = modifierRelease;
  return input;
}

using abc::KeyCode;
using abc::addon::ModeSwitchKey;
using abc::addon::isDispatchableKey;
using abc::addon::toKeyCommand;
namespace keysym = abc::addon::keysym;
namespace state = abc::addon::state;

void testLetters() {
  auto lower = toKeyCommand(key(keysym::kLowerA), ModeSwitchKey::kShift);
  expect(lower.code == KeyCode::kCharacter && lower.unicode == U'a', "a -> a");

  auto upper = toKeyCommand(key(keysym::kUpperZ, state::kShift),
                            ModeSwitchKey::kShift);
  expect(upper.code == KeyCode::kCharacter && upper.unicode == U'z',
         "Shift+Z normalizes to z");

  auto ctrl = toKeyCommand(key(keysym::kLowerA, state::kCtrl),
                           ModeSwitchKey::kShift);
  expect(ctrl.code == KeyCode::kUnknown, "Ctrl+a is unknown, not buffered");

  auto alt = toKeyCommand(key(keysym::kLowerA + 1, state::kAlt),
                          ModeSwitchKey::kShift);
  expect(alt.code == KeyCode::kUnknown, "Alt+b is unknown");

  auto superKey = toKeyCommand(key(keysym::kLowerA + 2, state::kSuper),
                               ModeSwitchKey::kShift);
  expect(superKey.code == KeyCode::kUnknown, "Super+c is unknown");
}

void testDigits() {
  auto one = toKeyCommand(key(keysym::kDigit0 + 1), ModeSwitchKey::kShift);
  expect(one.code == KeyCode::kDigit && one.digit == 1, "1 -> digit 1");

  auto nine = toKeyCommand(key(keysym::kKp9), ModeSwitchKey::kShift);
  expect(nine.code == KeyCode::kDigit && nine.digit == 9, "KP_9 -> digit 9");

  auto zero = toKeyCommand(key(keysym::kDigit0), ModeSwitchKey::kShift);
  expect(zero.code == KeyCode::kPunctuation && zero.unicode == U'0',
         "0 -> punctuation");

  auto kpZero = toKeyCommand(key(keysym::kKp0), ModeSwitchKey::kShift);
  expect(kpZero.code == KeyCode::kPunctuation && kpZero.unicode == U'0',
         "KP_0 -> punctuation 0");

  auto altOne = toKeyCommand(key(keysym::kDigit0 + 1, state::kAlt),
                             ModeSwitchKey::kShift);
  expect(altOne.code == KeyCode::kUnknown, "Alt+1 is unknown");
}

void testNamedKeys() {
  expect(toKeyCommand(key(keysym::kBackSpace), ModeSwitchKey::kNone).code ==
             KeyCode::kBackspace,
         "BackSpace");
  expect(toKeyCommand(key(keysym::kReturn), ModeSwitchKey::kNone).code ==
             KeyCode::kEnter,
         "Return");
  expect(toKeyCommand(key(keysym::kKpEnter), ModeSwitchKey::kNone).code ==
             KeyCode::kEnter,
         "KP_Enter");
  expect(toKeyCommand(key(keysym::kSpace), ModeSwitchKey::kNone).code ==
             KeyCode::kSpace,
         "space");
  expect(toKeyCommand(key(keysym::kEscape), ModeSwitchKey::kNone).code ==
             KeyCode::kEscape,
         "Escape");
  expect(toKeyCommand(key(keysym::kPrior), ModeSwitchKey::kNone).code ==
             KeyCode::kPageUp,
         "Prior");
  expect(toKeyCommand(key(keysym::kKpPrior), ModeSwitchKey::kNone).code ==
             KeyCode::kPageUp,
         "KP_Prior");
  expect(toKeyCommand(key(keysym::kNext), ModeSwitchKey::kNone).code ==
             KeyCode::kPageDown,
         "Next");
  expect(toKeyCommand(key(keysym::kKpNext), ModeSwitchKey::kNone).code ==
             KeyCode::kPageDown,
         "KP_Next");
}

void testPunctuationAndUnknown() {
  auto comma = toKeyCommand(key(0x002c), ModeSwitchKey::kShift);
  expect(comma.code == KeyCode::kPunctuation && comma.unicode == U',',
         "comma -> punctuation");

  auto tab = toKeyCommand(key(0xff09), ModeSwitchKey::kShift);
  expect(tab.code == KeyCode::kUnknown, "Tab -> unknown");

  auto left = toKeyCommand(key(keysym::kLeft), ModeSwitchKey::kShift);
  expect(left.code == KeyCode::kLeft, "Left -> kLeft");
  auto right = toKeyCommand(key(keysym::kRight), ModeSwitchKey::kShift);
  expect(right.code == KeyCode::kRight, "Right -> kRight");
  auto up = toKeyCommand(key(keysym::kUp), ModeSwitchKey::kShift);
  expect(up.code == KeyCode::kUp, "Up -> kUp");
  auto down = toKeyCommand(key(keysym::kDown), ModeSwitchKey::kShift);
  expect(down.code == KeyCode::kDown, "Down -> kDown");

  auto release = toKeyCommand(key(keysym::kLowerA, 0, true),
                              ModeSwitchKey::kShift);
  expect(release.code == KeyCode::kUnknown, "key release is unknown");
}

void testModeSwitch() {
  expect(abc::addon::parseModeSwitchKey("Shift") == ModeSwitchKey::kShift,
         "parse Shift");
  expect(abc::addon::parseModeSwitchKey("Control_L") == ModeSwitchKey::kControl,
         "parse Control_L");
  expect(abc::addon::parseModeSwitchKey("F9") == ModeSwitchKey::kNone,
         "unknown hotkey parses to none");
  expect(abc::addon::parseModeSwitchKey(nullptr) == ModeSwitchKey::kNone,
         "null hotkey parses to none");

  auto matched = toKeyCommand(key(keysym::kShiftL, state::kShift, true, true),
                              ModeSwitchKey::kShift);
  expect(matched.code == KeyCode::kModeSwitch,
         "left Shift release matching the hotkey -> mode switch");

  auto rightMatched = toKeyCommand(
      key(keysym::kShiftR, state::kShift, true, true), ModeSwitchKey::kShift);
  expect(rightMatched.code == KeyCode::kModeSwitch,
         "right Shift release matching the default hotkey -> mode switch");

  auto other = toKeyCommand(key(keysym::kShiftL, state::kShift, true, false),
                            ModeSwitchKey::kShift);
  expect(other.code == KeyCode::kUnknown,
         "release that is not the configured modifier -> unknown");

  auto press = toKeyCommand(key(keysym::kShiftL), ModeSwitchKey::kShift);
  expect(press.code == KeyCode::kUnknown, "modifier press is unknown");

  auto disabled = toKeyCommand(key(keysym::kShiftL, state::kShift, true, true),
                               ModeSwitchKey::kNone);
  expect(disabled.code == KeyCode::kUnknown,
         "no configured hotkey means no mode switch");
}

void testReleaseDispatch() {
  // 释放事件不得进入核心状态机：核心把 kUnknown 当未处理键，会提交并清空缓冲。
  const auto letterPress = toKeyCommand(key(keysym::kLowerA), ModeSwitchKey::kShift);
  expect(isDispatchableKey(letterPress, false), "letter press is dispatched");

  const auto letterRelease =
      toKeyCommand(key(keysym::kLowerA, 0, true, false), ModeSwitchKey::kShift);
  expect(letterRelease.code == KeyCode::kUnknown, "letter release maps to unknown");
  expect(!isDispatchableKey(letterRelease, true),
         "letter release is NOT dispatched to the core");

  const auto modeSwitch =
      toKeyCommand(key(keysym::kShiftL, state::kShift, true, true),
                   ModeSwitchKey::kShift);
  expect(modeSwitch.code == KeyCode::kModeSwitch, "hotkey release -> mode switch");
  expect(isDispatchableKey(modeSwitch, true),
         "mode switch release is still dispatched");
}

}  // namespace

int main() {
  testLetters();
  testDigits();
  testNamedKeys();
  testPunctuationAndUnknown();
  testModeSwitch();
  testReleaseDispatch();
  if (g_failed != 0) {
    std::cerr << g_failed << " keyconv assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "abc_addon_keyconv: ok\n";
  return EXIT_SUCCESS;
}
