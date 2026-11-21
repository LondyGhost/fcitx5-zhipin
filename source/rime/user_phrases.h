// user_phrases.h — 用户词组记忆（进程级单例）
//
// 为什么需要它：
//   1. 随包的 abc.schema.yaml 里 `enable_user_dict: false`，librime 自己
//      不记新词；
//   2. 「分阶段逐字选字」（jinhai -> 金 + 海）是我们自己实现的，
//      pickStagedChar() 会 clearRime() 再用 commitTextEffect 上屏，
//      整个流程根本不经过 rime 的 commit，rime 无从学习。
//
// 所以这里维护一份「编码 -> 词组」的小表：上屏时记一次，下次同编码输入时
// 把这些词组插到候选最前面。文件格式是制表符分隔的纯文本，方便用户手改。
#ifndef ABC_RIME_USER_PHRASES_H_
#define ABC_RIME_USER_PHRASES_H_

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace abc {

class UserPhraseStore {
 public:
  static UserPhraseStore& instance();

  // 设置持久化路径并加载。重复调用只有第一次生效。
  // 路径为空时退化为纯内存（不落盘）。
  void configure(const std::string& path);

  // 记录一次上屏。code 里的音节分隔符会被去掉，保证 shan'an 与 shanan
  // 命中同一条记录。文本不足两个字符时忽略。
  void add(const std::string& code, const std::string& phrase);

  // 取该编码下学到的词组，按频次降序、同频次按最近使用靠前。
  std::vector<std::string> lookup(const std::string& code);

  // 用已学到的片段拼出整串编码对应的词组。
  // 例：学过 jinhai->金海、jiaxiao->驾校，则 compose("jinhaijiaxiao")
  // 返回「金海驾校」。必须能被切成至少两段，否则返回空串
  // （单段由 lookup 精确命中，不需要拼）。
  std::string compose(const std::string& code);

  // 仅测试用：清空内存表（不动文件）。
  void clearForTest();

 private:
  UserPhraseStore() = default;
  void loadLocked();
  void saveLocked();

  struct Entry {
    std::string text;
    int count = 1;
    unsigned long seq = 0;  // 最近使用序号，用于同频次排序
  };

  std::mutex mutex_;
  std::string path_;
  bool loaded_ = false;
  unsigned long seq_ = 0;
  std::map<std::string, std::vector<Entry>> table_;  // code -> entries
};

}  // namespace abc

#endif  // ABC_RIME_USER_PHRASES_H_
