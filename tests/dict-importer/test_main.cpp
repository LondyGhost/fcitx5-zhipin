// A2 测试入口。argv[1] = fixtures 目录（可选）；argv[2] = abc_dict_import
// 可执行路径（可选；e2e 用例在缺省时跳过）。

#include "test_harness.hpp"
#include "test_util.hpp"

int main(int argc, char** argv) {
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    abc_test::runConfig().fixture_dir = argv[1];
  }
  if (argc > 2 && argv[2] != nullptr && argv[2][0] != '\0') {
    abc_test::runConfig().importer_exe = argv[2];
  }
  return abc_test::runAll();
}
