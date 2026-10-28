// A6 集成测试用契约保真 Dictionary 替身。
//
// 从消费方独立实现 docs/interfaces/dictionary.md 的冻结语义：
//   - 4.1 冻结排序（精确编码组 → code 升序 → weight 降序 → text 升序）；
//   - 第 3 节分页（offset 回显、total 精确、hasMore 公式）；
//   - 4.2 参数校验（空前缀/limit>256 → kInvalidArgument）；
//   - 脚本注入用于错误路径（kCorruptIndex 等）。
// 该替身同时是词典契约的可执行交叉核对：A3 交付后可用相同词条集合
// 对比 A3 实现与本研究实现的逐字节一致性。

#ifndef ABC_INTEGRATION_CONTRACT_DICTIONARY_HPP
#define ABC_INTEGRATION_CONTRACT_DICTIONARY_HPP

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "abc/dictionary.h"

namespace abc_itest {

struct ChainQuery {
  std::string prefix;
  uint32_t limit = 0;
  uint32_t offset = 0;
};

class ContractDictionary final : public abc::Dictionary {
 public:
  void add(const std::string& code, const std::string& text, double weight,
           const std::string& source = "integration") {
    abc::Candidate c;
    c.text = text;
    c.code = code;
    c.weight = weight;
    c.source = source;
    c.stableId = abc::computeStableId(code, text, source);
    entries_.push_back(std::move(c));
  }

  void setScript(std::function<abc::LookupResult(std::string_view, uint32_t,
                                                 uint32_t)>
                     script) {
    script_ = std::move(script);
  }

  abc::LookupResult lookup(std::string_view prefix, uint32_t limit,
                           uint32_t offset) const override {
    queries_.push_back({std::string(prefix), limit, offset});
    if (script_) return script_(prefix, limit, offset);

    if (prefix.empty() || prefix.size() > 128 || limit > 256) {
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
                       if (exactA != exactB) return exactA;
                       if (a->code != b->code) return a->code < b->code;
                       if (a->weight != b->weight) return a->weight > b->weight;
                       return a->text < b->text;
                     });

    abc::LookupResult r;
    r.batch.total = matched.size();
    if (offset < matched.size() && limit > 0) {
      const std::size_t end = std::min(
          matched.size(), static_cast<std::size_t>(offset) + limit);
      for (std::size_t i = offset; i < end; ++i) {
        r.batch.items.push_back(*matched[i]);
      }
    }
    r.batch.offset = offset;
    r.batch.hasMore =
        static_cast<uint64_t>(offset) + r.batch.items.size() < r.batch.total;
    return r;
  }

  const std::vector<ChainQuery>& queries() const { return queries_; }
  void clearQueries() { queries_.clear(); }

 private:
  std::vector<abc::Candidate> entries_;
  std::function<abc::LookupResult(std::string_view, uint32_t, uint32_t)>
      script_;
  mutable std::vector<ChainQuery> queries_;
};

}  // namespace abc_itest

#endif  // ABC_INTEGRATION_CONTRACT_DICTIONARY_HPP
