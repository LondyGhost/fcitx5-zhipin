// A2↔A3 交叉验证：A2 生成的索引必须能被 A3 loader 完整加载并按契约 4.1
// 查询。同时用损坏/截断/版本篡改样本验证 A3 的防御校验与 A2 的输出字节
// 完全对齐（CRC/顺序/stableId 任一不一致都会被拒绝）。
//
// 本文件链接 abc_dictionary（A3 目标）——build.md 允许的“CMake 目标链接”
// 跨模块复用方式；不修改 A3 的任何文件。

#include <cstdint>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "dict_importer.hpp"
#include "abc/index_format.h"
#include "test_util.hpp"

namespace {

using abc::dict_import::ExitCode;
using abc::dict_import::ImportOptions;
using abc::dict_import::ImportResult;

// 把字节串写入临时文件（TempDir 由调用方持有以延长生命期）。
std::string writeBytesTo(const abc_test::TempDir& dir, const char* name,
                         const std::string& bytes) {
  const std::string path = dir.file(name);
  abc_test::writeFileBytes(path, bytes);
  return path;
}

}  // namespace

ABC_TEST(CrossMinimalLoadsAndQueries) {
  abc_test::TempDir dir;
  ImportOptions options;
  options.source = "cn_dicts/base";
  std::vector<unsigned char> buf;
  const ImportResult r = abc::dict_import::compileIndexToBuffer(
      abc_test::fixtureFile("minimal.dict.yaml"), options, &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  const std::string path =
      writeBytesTo(dir, "minimal.ix", std::string(buf.begin(), buf.end()));
  const abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(path);
  EXPECT_TRUE(d.status == abc::DictionaryStatus::kOk);
  EXPECT_TRUE(d.detail.empty());
  EXPECT_TRUE(d.dictionary != nullptr);
  if (d.status != abc::DictionaryStatus::kOk || d.dictionary == nullptr) return;

  // 精确编码查询。
  const abc::LookupResult ni = d.dictionary->lookup("nihao", 9, 0);
  EXPECT_TRUE(ni.status == abc::DictionaryStatus::kOk);
  EXPECT_EQ(ni.batch.total, 1u);
  EXPECT_EQ(ni.batch.items.size(), 1u);
  if (ni.batch.items.size() == 1) {
    EXPECT_EQ(ni.batch.items[0].text, "\xE4\xBD\xA0\xE5\xA5\xBD");  // 你好
    EXPECT_EQ(ni.batch.items[0].code, "nihao");
    EXPECT_TRUE(ni.batch.items[0].weight == 100.0);
    EXPECT_EQ(ni.batch.items[0].source, "cn_dicts/base");
    EXPECT_EQ(ni.batch.items[0].stableId,
              abc::computeStableId("nihao",
                                   "\xE4\xBD\xA0\xE5\xA5\xBD",
                                   "cn_dicts/base"));
  }
  EXPECT_FALSE(ni.batch.hasMore);

  // 前缀查询：精确编码组在前（契约 4.1）。
  const abc::LookupResult ma = d.dictionary->lookup("ma", 9, 0);
  EXPECT_TRUE(ma.status == abc::DictionaryStatus::kOk);
  EXPECT_EQ(ma.batch.total, 4u);
  EXPECT_EQ(ma.batch.items.size(), 4u);
  if (ma.batch.items.size() == 4) {
    EXPECT_EQ(ma.batch.items[0].code, "ma");
    EXPECT_EQ(ma.batch.items[0].text, "\xE9\xA9\xAC");  // 马（70）
    EXPECT_TRUE(ma.batch.items[0].weight == 70.0);
    EXPECT_EQ(ma.batch.items[1].text, "\xE5\xA6\x88");  // 妈（65）
    EXPECT_EQ(ma.batch.items[2].text, "\xE9\xAA\x82");  // 骂（60）
    EXPECT_EQ(ma.batch.items[3].code, "mama");          // 更长编码在后
    EXPECT_EQ(ma.batch.items[3].text, "\xE5\xA6\x88\xE5\xA6\x88");  // 妈妈
    EXPECT_TRUE(ma.batch.items[3].weight == 100.0);
  }

  // 分页语义（契约 3）。
  const abc::LookupResult p1 = d.dictionary->lookup("ma", 2, 0);
  EXPECT_EQ(p1.batch.items.size(), 2u);
  EXPECT_EQ(p1.batch.total, 4u);
  EXPECT_TRUE(p1.batch.hasMore);
  EXPECT_EQ(p1.batch.offset, 0u);
  const abc::LookupResult p2 = d.dictionary->lookup("ma", 2, 2);
  EXPECT_EQ(p2.batch.items.size(), 2u);
  EXPECT_EQ(p2.batch.total, 4u);
  EXPECT_FALSE(p2.batch.hasMore);
  EXPECT_EQ(p2.batch.offset, 2u);
  if (p2.batch.items.size() == 2) {
    EXPECT_EQ(p2.batch.items[0].text, "\xE9\xAA\x82");              // 骂
    EXPECT_EQ(p2.batch.items[1].text, "\xE5\xA6\x88\xE5\xA6\x88");  // 妈妈
  }
  const abc::LookupResult p3 = d.dictionary->lookup("ma", 2, 4);
  EXPECT_TRUE(p3.batch.items.empty());
  EXPECT_EQ(p3.batch.total, 4u);
  EXPECT_FALSE(p3.batch.hasMore);

  // limit == 0 只取计数。
  const abc::LookupResult count = d.dictionary->lookup("ma", 0, 0);
  EXPECT_TRUE(count.status == abc::DictionaryStatus::kOk);
  EXPECT_TRUE(count.batch.items.empty());
  EXPECT_EQ(count.batch.total, 4u);

  // 归一化编码可直接查询（音节空格已在导入时去除）。
  const abc::LookupResult cs = d.dictionary->lookup("ceshi", 9, 0);
  EXPECT_EQ(cs.batch.total, 1u);
  EXPECT_EQ(cs.batch.items[0].text, "\xE6\xB5\x8B\xE8\xAF\x95");  // 测试
}

ABC_TEST(CrossEmptyAndBoundsIndexes) {
  // 空索引可加载。
  {
    abc_test::TempDir dir;
    ImportOptions options;
    options.source = "e";
    std::vector<unsigned char> buf;
    const ImportResult r = abc::dict_import::compileIndexToBuffer(
        abc_test::fixtureFile("empty.dict.yaml"), options, &buf);
    EXPECT_TRUE(r.exit_code == ExitCode::kOk);
    const std::string path =
        writeBytesTo(dir, "empty.ix", std::string(buf.begin(), buf.end()));
    const abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(path);
    EXPECT_TRUE(d.status == abc::DictionaryStatus::kOk);
    if (d.status == abc::DictionaryStatus::kOk) {
      const abc::LookupResult q = d.dictionary->lookup("a", 9, 0);
      EXPECT_TRUE(q.status == abc::DictionaryStatus::kOk);
      EXPECT_TRUE(q.batch.items.empty());
      EXPECT_EQ(q.batch.total, 0u);
    }
  }
  // 恰好 128 字节编码可加载且支持前缀查询。
  {
    abc_test::TempDir dir;
    ImportOptions options;
    options.source = "b";
    std::vector<unsigned char> buf;
    const ImportResult r = abc::dict_import::compileIndexToBuffer(
        abc_test::fixtureFile("bounds_ok.dict.yaml"), options, &buf);
    EXPECT_TRUE(r.exit_code == ExitCode::kOk);
    const std::string path =
        writeBytesTo(dir, "bounds.ix", std::string(buf.begin(), buf.end()));
    const abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(path);
    EXPECT_TRUE(d.status == abc::DictionaryStatus::kOk);
    if (d.status == abc::DictionaryStatus::kOk) {
      const abc::LookupResult full =
          d.dictionary->lookup(std::string(128, 'a'), 9, 0);
      EXPECT_EQ(full.batch.total, 1u);
      const abc::LookupResult prefix =
          d.dictionary->lookup(std::string(127, 'a'), 9, 0);
      EXPECT_EQ(prefix.batch.total, 1u);  // 127 前缀命中 128 编码
    }
  }
}

ABC_TEST(CrossInvalidLookupArguments) {
  abc_test::TempDir dir;
  ImportOptions options;
  options.source = "x";
  std::vector<unsigned char> buf;
  const ImportResult r = abc::dict_import::compileIndexToBuffer(
      abc_test::fixtureFile("minimal.dict.yaml"), options, &buf);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  const std::string path =
      writeBytesTo(dir, "minimal.ix", std::string(buf.begin(), buf.end()));
  const abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(path);
  EXPECT_TRUE(d.status == abc::DictionaryStatus::kOk);
  if (d.status != abc::DictionaryStatus::kOk) return;
  // 契约 4.2：空前缀 / 超长前缀 → kInvalidArgument。
  EXPECT_TRUE(d.dictionary->lookup("", 9, 0).status ==
              abc::DictionaryStatus::kInvalidArgument);
  EXPECT_TRUE(d.dictionary->lookup(std::string(129, 'a'), 9, 0).status ==
              abc::DictionaryStatus::kInvalidArgument);
}

ABC_TEST(CrossCorruptionDetectedByLoader) {
  abc_test::TempDir dir;
  ImportOptions options;
  options.source = "x";
  std::vector<unsigned char> good;
  const ImportResult r = abc::dict_import::compileIndexToBuffer(
      abc_test::fixtureFile("minimal.dict.yaml"), options, &good);
  EXPECT_TRUE(r.exit_code == ExitCode::kOk);
  const std::string good_bytes(good.begin(), good.end());
  EXPECT_TRUE(good_bytes.size() > 100u);

  // 各类损坏样本必须被 A3 拒绝（证明 A2 输出与 A3 校验逐字节对齐）。
  struct Mutation {
    const char* name;
    std::string bytes;
    abc::DictionaryStatus expected;
  };
  std::vector<Mutation> mutations;

  {  // 记录区字节翻转 → CRC 失败。
    std::string m = good_bytes;
    m[100] = static_cast<char>(m[100] ^ 0x01);
    mutations.push_back({"record byte flip", m,
                         abc::DictionaryStatus::kCorruptIndex});
  }
  {  // magic 损坏。
    std::string m = good_bytes;
    m[0] = 'X';
    mutations.push_back(
        {"bad magic", m, abc::DictionaryStatus::kCorruptIndex});
  }
  {  // 截断。
    mutations.push_back({"truncated", good_bytes.substr(0, 20),
                         abc::DictionaryStatus::kCorruptIndex});
  }
  {  // 主版本不兼容（0x0002_0000）。
    std::string m = good_bytes;
    m[0x08] = static_cast<char>(0x02);
    mutations.push_back({"major version bump", m,
                         abc::DictionaryStatus::kUnsupportedVersion});
  }
  {  // 次版本更高（读取方 minor=0 → 拒绝）。version 小端：低 16 位在字节 0x08-0x09。
    std::string m = good_bytes;
    m[0x08] = static_cast<char>(0x01);
    mutations.push_back({"minor version bump", m,
                         abc::DictionaryStatus::kUnsupportedVersion});
  }
  {  // 保留字段非零。
    std::string m = good_bytes;
    m[0x20] = static_cast<char>(0x01);
    mutations.push_back(
        {"flags nonzero", m, abc::DictionaryStatus::kCorruptIndex});
  }
  {  // 尾随字节。
    std::string m = good_bytes;
    m += '\x00';
    mutations.push_back({"trailing byte", m,
                         abc::DictionaryStatus::kCorruptIndex});
  }

  for (std::size_t i = 0; i < mutations.size(); ++i) {
    const std::string path =
        writeBytesTo(dir, ("mut_" + std::to_string(i) + ".ix").c_str(),
                     mutations[i].bytes);
    const abc::DictionaryLoadResult d = abc::loadDictionaryFromFile(path);
    EXPECT_TRUE(d.status == mutations[i].expected);
    EXPECT_TRUE(d.dictionary == nullptr);
    EXPECT_FALSE(d.detail.empty());  // 诊断必须可定位
  }
}
