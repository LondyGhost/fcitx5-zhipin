// 接口冒烟测试（A0）。
// 覆盖：契约版本常量、公共类型可用性、stableId 已知向量、FakeDictionary
// 演示契约可实现的分页/排序语义、公共头依赖方向（仅标准库 + abc/*，无 fcitx5）。
// 不测试实现行为（A1–A3 未交付）；实现期符号（工厂/加载器）在骨架期不得被引用。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "abc/dictionary.h"
#include "abc/engine.h"
#include "abc/index_format.h"
#include "abc/key.h"
#include "abc/version.h"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cerr << "[FAIL] " << what << "\n";
    ++g_failures;
  } else {
    std::cout << "[ok] " << what << "\n";
  }
}

// ---------------------------------------------------------------------------
// 头文件依赖方向守卫：公共头只允许包含 <标准库> 或 "abc/*"；
// 任何 include 指令出现 fcitx/x11/wayland 即失败（架构契约，见 build.md 4）。
// ---------------------------------------------------------------------------
void scanHeaderIncludes(const std::filesystem::path& file,
                        std::vector<std::string>* violations) {
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash == std::string::npos) continue;
    std::string rest = line.substr(hash + 1);
    // 允许 # 与 include 之间有空白
    const auto firstNonSpace = rest.find_first_not_of(" \t");
    if (firstNonSpace == std::string::npos) continue;
    rest = rest.substr(firstNonSpace);
    if (rest.rfind("include", 0) != 0) continue;

    std::string lowered;
    lowered.reserve(line.size());
    for (char c : line) {
      lowered.push_back(
          static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    if (lowered.find("fcitx") != std::string::npos ||
        lowered.find("xlib") != std::string::npos ||
        lowered.find("wayland") != std::string::npos) {
      violations->push_back(file.string() + ": " + line);
      continue;
    }
    const auto quoteOpen = line.find('"');
    if (quoteOpen == std::string::npos) continue;  // <...> 系统头：允许
    const auto quoteClose = line.find('"', quoteOpen + 1);
    const std::string target =
        line.substr(quoteOpen + 1, quoteClose - quoteOpen - 1);
    if (target.rfind("abc/", 0) != 0) {
      violations->push_back(file.string() + ": 非法本地包含 \"" + target +
                            "\"（只允许 \"abc/...\"）");
    }
  }
}

// ---------------------------------------------------------------------------
// FakeDictionary：契约可实现性的最小演示（分页 + 冻结排序）；A1 可复用此模式。
// lookup 时按契约 4.1 排序：精确编码组在前，其余按 code 升序，组内
// weight 降序、text 升序。
// ---------------------------------------------------------------------------
class FakeDictionary final : public abc::Dictionary {
 public:
  explicit FakeDictionary(std::vector<abc::Candidate> items)
      : items_(std::move(items)) {}

  abc::LookupResult lookup(std::string_view prefix, uint32_t limit,
                           uint32_t offset) const override {
    abc::LookupResult result;
    if (prefix.empty() || prefix.size() > abc::kIndexMaxCodeBytes ||
        limit > 256) {
      result.status = abc::DictionaryStatus::kInvalidArgument;
      return result;
    }
    std::vector<const abc::Candidate*> matched;
    for (const abc::Candidate& cand : items_) {
      const std::string& code = cand.code;
      if (code.size() >= prefix.size() &&
          code.compare(0, prefix.size(), prefix) == 0) {
        matched.push_back(&cand);
      }
    }
    std::sort(matched.begin(), matched.end(),
              [prefix](const abc::Candidate* a, const abc::Candidate* b) {
                const bool exactA = a->code == prefix;
                const bool exactB = b->code == prefix;
                if (exactA != exactB) return exactA;
                if (a->code != b->code) return a->code < b->code;
                if (a->weight != b->weight) return a->weight > b->weight;
                return a->text < b->text;
              });
    result.batch.total = matched.size();
    if (offset < matched.size() && limit > 0) {
      const std::size_t start = offset;
      const std::size_t end =
          std::min(matched.size(), static_cast<std::size_t>(offset) + limit);
      for (std::size_t i = start; i < end; ++i) {
        result.batch.items.push_back(*matched[i]);
      }
    }
    result.batch.offset = offset;
    result.batch.hasMore =
        static_cast<uint64_t>(offset) + result.batch.items.size() <
        result.batch.total;
    return result;
  }

 private:
  std::vector<abc::Candidate> items_;
};

}  // namespace

int main() {
  // 1. 版本常量
  check(ABC_INTERFACE_VERSION_MAJOR == 1 && ABC_INTERFACE_VERSION_MINOR == 0 &&
            ABC_INTERFACE_VERSION_PATCH == 0,
        "interface version macros are 1.0.0");
  check(abc::kIndexFormatVersionMajor == 1 && abc::kIndexFormatVersionMinor == 0,
        "index format version is 1.0");
  check(abc::kIndexFormatVersion == 0x00010000u,
        "index format version encoding is (1<<16)|0");
  check(std::string_view(reinterpret_cast<const char*>(abc::kIndexMagic), 8) ==
            std::string("ABCDIX1\0", 8),
        "index magic is \"ABCDIX1\\0\"");

  // 2. 公共类型可构造（值语义、默认值符合契约）
  abc::KeyCommand cmd;
  cmd.code = abc::KeyCode::kCharacter;
  cmd.unicode = U'n';
  check(cmd.code == abc::KeyCode::kCharacter && cmd.digit == 0,
        "KeyCommand is default-constructible and assignable");

  abc::Candidate c;
  c.text = "你";
  c.code = "ni";
  c.weight = 3.14;
  c.source = "base";
  c.stableId = abc::computeStableId(c.code, c.text, c.source);
  const abc::Candidate copied = c;  // 值语义，无隐藏共享
  check(copied.text == "你" && copied.stableId == c.stableId,
        "Candidate is copyable value type");

  abc::EngineConfig cfg;
  check(cfg.pageSize == 9 && cfg.maxBufferChars == 64 &&
            cfg.initialMode == abc::Mode::kChineseEmpty,
        "EngineConfig defaults match contract (pageSize=9, maxBuffer=64)");

  abc::EngineResult r;
  r.consumed = true;
  r.mode = abc::Mode::kComposing;
  r.preedit = "n";
  r.effects.push_back(abc::Effect{abc::Effect::Kind::kCommitText, "n"});
  check(r.consumed && r.highlight == -1 && r.effects.size() == 1 &&
            r.effects[0].kind == abc::Effect::Kind::kCommitText,
        "EngineResult defaults match contract (highlight=-1)");

  // 3. stableId 已知向量（FNV-1a 64，契约 dictionary.md 2.2）
  check(abc::computeStableId("ni", "你", "base") == 0xdab5c10fa400390eULL,
        "stableId vector (ni,你,base)");
  check(abc::computeStableId("nihao", "你好", "cn_dicts/base") ==
            0xe8f6edb01cdcb459ULL,
        "stableId vector (nihao,你好,cn_dicts/base)");
  check(abc::computeStableId("", "", "") == 0x0879e907b5281763ULL,
        "stableId of empty parts equals basis mixed with two separators");

  // 4. FakeDictionary：契约分页/排序语义可实现
  std::vector<abc::Candidate> fixture;
  auto add = [&fixture](const char* text, const char* code, double weight,
                        const char* source) {
    abc::Candidate cand;
    cand.text = text;
    cand.code = code;
    cand.weight = weight;
    cand.source = source;
    cand.stableId = abc::computeStableId(code, text, source);
    fixture.push_back(cand);
  };
  // 故意乱序放入，验证 lookup 输出遵循冻结排序。
  add("你好吗", "nihaoma", 20.0, "ext");
  add("泥", "ni", 10.0, "base");
  add("你好", "nihao", 90.0, "cn_dicts/base");
  add("你", "ni", 100.0, "base");
  const FakeDictionary fake(fixture);

  const abc::LookupResult page1 = fake.lookup("ni", 2, 0);
  check(page1.status == abc::DictionaryStatus::kOk &&
            page1.batch.total == 4 && page1.batch.items.size() == 2 &&
            page1.batch.items[0].text == "你" &&
            page1.batch.items[1].text == "泥" && page1.batch.hasMore &&
            page1.batch.offset == 0,
        "lookup page 1: exact-code group first, total/hasMore/offset correct");

  const abc::LookupResult page2 = fake.lookup("ni", 2, 2);
  check(page2.status == abc::DictionaryStatus::kOk &&
            page2.batch.items.size() == 2 &&
            page2.batch.items[0].text == "你好" &&
            page2.batch.items[1].text == "你好吗" && !page2.batch.hasMore &&
            page2.batch.offset == 2,
        "lookup page 2: prefix groups after exact group, code ascending");

  const abc::LookupResult countOnly = fake.lookup("ni", 0, 0);
  check(countOnly.status == abc::DictionaryStatus::kOk &&
            countOnly.batch.items.empty() && countOnly.batch.total == 4 &&
            countOnly.batch.hasMore,  // 公式：0 + 0 < total
        "lookup with limit=0 is count-only (hasMore per formula)");

  const abc::LookupResult beyond = fake.lookup("ni", 4, 9);
  check(beyond.status == abc::DictionaryStatus::kOk &&
            beyond.batch.items.empty() && beyond.batch.total == 4 &&
            !beyond.batch.hasMore,
        "lookup with offset beyond total returns empty batch");

  const abc::LookupResult bad = fake.lookup("", 4, 0);
  check(bad.status == abc::DictionaryStatus::kInvalidArgument &&
            bad.batch.items.empty() && bad.batch.total == 0,
        "empty prefix rejected with kInvalidArgument");

  // 5. 公共头依赖方向守卫（扫描 source/include）
  std::vector<std::string> violations;
  std::size_t scanned = 0;
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(ABC_SOURCE_INCLUDE_DIR,
                                                     ec)) {
    if (!entry.is_regular_file()) continue;
    ++scanned;
    scanHeaderIncludes(entry.path(), &violations);
  }
  check(!ec && scanned >= 5, "scanned public headers under source/include");
  check(violations.empty(),
        "public headers include only std headers and abc/* (no fcitx/x11/wayland)");
  for (const auto& v : violations) std::cerr << "       " << v << "\n";

  if (g_failures == 0) {
    std::cout << "abc_interface_smoke: ALL PASS\n";
    return 0;
  }
  std::cout << "abc_interface_smoke: " << g_failures << " failure(s)\n";
  return 1;
}
