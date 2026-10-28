// A1 测试用 fake Dictionary — 按冻结契约 docs/interfaces/dictionary.md 实现。
//
// 两种模式：
//   1. 词条表模式（默认）：add() 录入词条；lookup 按契约 4.1 冻结排序
//      （精确编码组在前 → code 升序 → weight 降序 → text 升序），
//      total 为精确总数，offset 回显，hasMore 按公式 offset+items<total。
//   2. 脚本模式：setScript() 注入回调，可模拟错误状态、空结果、重复候选等。
// 所有查询被记录（prefix/limit/offset）供测试断言核心的查询行为。

#ifndef ABC_CORE_TEST_FAKE_DICTIONARY_HPP
#define ABC_CORE_TEST_FAKE_DICTIONARY_HPP

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "abc/dictionary.h"

namespace abc_test {

struct QueryRecord {
  std::string prefix;
  uint32_t limit = 0;
  uint32_t offset = 0;
};

class FakeDictionary final : public abc::Dictionary {
 public:
  void add(const std::string& code, const std::string& text, double weight,
           const std::string& source = "fake") {
    abc::Candidate c;
    c.text = text;
    c.code = code;
    c.weight = weight;
    c.source = source;
    c.stableId = abc::computeStableId(code, text, source);
    entries_.push_back(std::move(c));
  }

  // 病理情形注入：设置后 lookup 完全交给回调（回调须自行满足契约返回形状）。
  void setScript(std::function<abc::LookupResult(std::string_view, uint32_t,
                                                 uint32_t)>
                     script) {
    script_ = std::move(script);
  }

  abc::LookupResult lookup(std::string_view prefix, uint32_t limit,
                           uint32_t offset) const override {
    calls_.push_back({std::string(prefix), limit, offset});
    if (script_) {
      return script_(prefix, limit, offset);
    }

    // 契约 4.2 参数校验（核心不应触发，此处仅为夹具保真）。
    if (prefix.empty() || limit > 256) {
      abc::LookupResult bad;
      bad.status = abc::DictionaryStatus::kInvalidArgument;
      return bad;
    }

    std::vector<const abc::Candidate*> matched;
    for (const abc::Candidate& c : entries_) {
      if (c.code.size() >= prefix.size() &&
          c.code.compare(0, prefix.size(), prefix) == 0) {
        matched.push_back(&c);
      }
    }
    std::stable_sort(matched.begin(), matched.end(),
                     [prefix](const abc::Candidate* a, const abc::Candidate* b) {
                       const bool exactA = a->code == prefix;
                       const bool exactB = b->code == prefix;
                       if (exactA != exactB) return exactA;  // 1. 精确编码优先
                       if (a->code != b->code) return a->code < b->code;  // 2.
                       if (a->weight != b->weight) return a->weight > b->weight;  // 3.
                       return a->text < b->text;  // 3. text 升序平局
                     });

    abc::LookupResult result;
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
    result.batch.hasMore = static_cast<uint64_t>(offset) +
                               result.batch.items.size() <
                           result.batch.total;
    return result;
  }

  const std::vector<QueryRecord>& calls() const { return calls_; }
  void clearCalls() { calls_.clear(); }

 private:
  std::vector<abc::Candidate> entries_;
  std::function<abc::LookupResult(std::string_view, uint32_t, uint32_t)>
      script_;
  mutable std::vector<QueryRecord> calls_;
};

}  // namespace abc_test

#endif  // ABC_CORE_TEST_FAKE_DICTIONARY_HPP
