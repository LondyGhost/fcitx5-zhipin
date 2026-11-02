// keyconv_test.cpp — 验证智能ABC 翻页键映射
#include <cstdio>

#include "keyconv.h"

using namespace abc;
using namespace abc::addon;

static void check(uint32_t sym, const char* name, KeyCode expect) {
  KeyInput in;
  in.keysym = sym;
  const KeyCommand cmd = toKeyCommand(in, ModeSwitchKey::kShift);
  const bool ok = cmd.code == expect;
  std::printf("%-10s 0x%04x -> code=%d expect=%d  %s\n", name, sym,
              static_cast<int>(cmd.code), static_cast<int>(expect),
              ok ? "OK" : "FAIL");
}

int main() {
  // 主键盘 - / = 是翻页键
  check(keysym::kMinus, "- (main)", KeyCode::kPageUp);
  check(keysym::kEqual, "= (main)", KeyCode::kPageDown);
  // 物理 PageUp/PageDown 同样有效
  check(keysym::kPrior, "PgUp", KeyCode::kPageUp);
  check(keysym::kNext, "PgDn", KeyCode::kPageDown);
  // 小键盘的 - / = 不能当翻页键
  check(keysym::kKpSubtract, "KP -", KeyCode::kUnknown);
  check(keysym::kKpEqual, "KP =", KeyCode::kUnknown);
  // 其他键不受影响
  check(keysym::kSpace, "space", KeyCode::kSpace);
  check(keysym::kLowerA, "a", KeyCode::kCharacter);
  check(keysym::kDigit0 + 1, "1", KeyCode::kDigit);
  return 0;
}
