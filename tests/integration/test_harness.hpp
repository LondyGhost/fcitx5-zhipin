// A6 集成测试用极简断言框架（独立于 tests/core 的模块自测工具，
// 保证验收链路测试不依赖被验收模块自身的测试内部件）。

#ifndef ABC_INTEGRATION_TEST_HARNESS_HPP
#define ABC_INTEGRATION_TEST_HARNESS_HPP

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace abc_itest {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
inline int& failures() {
  static int n = 0;
  return n;
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

}  // namespace abc_itest

#define ITEST(name)                                                        \
  static void abc_itest_fn_##name();                                       \
  static ::abc_itest::Registrar abc_itest_reg_##name(#name,                \
                                                     &abc_itest_fn_##name); \
  static void abc_itest_fn_##name()

#define IEXPECT_TRUE(cond)                                                 \
  do {                                                                     \
    if (!(cond)) {                                                         \
      ::abc_itest::fail(__FILE__, __LINE__, "IEXPECT_TRUE(" #cond ")");    \
    }                                                                      \
  } while (0)

#define IEXPECT_FALSE(cond) IEXPECT_TRUE(!(cond))

#define IEXPECT_EQ(a, b)                                                   \
  do {                                                                     \
    const auto& va_ = (a);                                                 \
    const auto& vb_ = (b);                                                 \
    if (!(va_ == vb_)) {                                                   \
      ::abc_itest::fail(__FILE__, __LINE__,                                \
                        std::string("IEXPECT_EQ(" #a ", " #b "): got=") +  \
                            ::abc_itest::show(va_) + " want=" +            \
                            ::abc_itest::show(vb_));                       \
    }                                                                      \
  } while (0)

namespace abc_itest {

inline int runAll() {
  int ran = 0;
  for (const Case& c : registry()) {
    currentName() = c.name;
    ++ran;
    c.fn();
  }
  std::printf("integration: ran %d cases, %d failed assertions\n", ran,
              failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace abc_itest

#endif  // ABC_INTEGRATION_TEST_HARNESS_HPP
