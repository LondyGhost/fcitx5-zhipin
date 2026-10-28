// A1 测试用极简断言框架（无外部依赖）。
// 用法：
//   ABC_TEST(name) { ... }          // 注册用例
//   EXPECT_TRUE(cond); EXPECT_EQ(a, b); EXPECT_NE(a, b);
// main() 调用 abc_test::runAll(argc, argv) 返回进程退出码。

#ifndef ABC_CORE_TEST_HARNESS_HPP
#define ABC_CORE_TEST_HARNESS_HPP

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace abc_test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failures() {
  static int count = 0;
  return count;
}

inline const char*& currentName() {
  static const char* name = "<unknown>";
  return name;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
  ++failures();
  std::fprintf(stderr, "  [FAIL] %s:%d (%s) %s\n", file, line, currentName(),
               msg.c_str());
}

template <typename T>
inline std::string show(const T& v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

inline std::string show(bool v) { return v ? "true" : "false"; }

}  // namespace abc_test

#define ABC_TEST(name)                                                     \
  static void abc_test_fn_##name();                                       \
  static ::abc_test::Registrar abc_test_reg_##name(#name,                 \
                                                   &abc_test_fn_##name);  \
  static void abc_test_fn_##name()

#define EXPECT_TRUE(cond)                                                  \
  do {                                                                     \
    if (!(cond)) {                                                         \
      ::abc_test::fail(__FILE__, __LINE__, "EXPECT_TRUE(" #cond ")");      \
    }                                                                      \
  } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                    \
  do {                                                                     \
    const auto& va_ = (a);                                                 \
    const auto& vb_ = (b);                                                 \
    if (!(va_ == vb_)) {                                                   \
      ::abc_test::fail(__FILE__, __LINE__,                                 \
                       std::string("EXPECT_EQ(" #a ", " #b "): actual=") + \
                           ::abc_test::show(va_) + " expected=" +          \
                           ::abc_test::show(vb_));                         \
    }                                                                      \
  } while (0)

#define EXPECT_NE(a, b) EXPECT_TRUE(!((a) == (b)))

namespace abc_test {

inline int runAll() {
  int ran = 0;
  for (const Case& c : registry()) {
    currentName() = c.name;
    ++ran;
    c.fn();
  }
  std::printf("ran %d test cases, %d failed assertions\n", ran, failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace abc_test

#endif  // ABC_CORE_TEST_HARNESS_HPP
