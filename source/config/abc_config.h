// abc_config.h — 用户配置解析（A5 拥有，source/config/**）
// 契约：docs/interfaces/config.md（键表、默认值、范围、解析与错误规则）。
// 输出 abc::EngineConfig（abc/engine.h，A0 冻结）+ AddonSettings（本模块定义）。
// 解析结果已按契约范围校验，可直接交给 abc::createEngineSession，
// 无需二次校验。任何解析异常不失败：警告 + 回退默认值（契约第 3/6 节）。
#ifndef ABC_CONFIG_H_
#define ABC_CONFIG_H_

#include <string>
#include <vector>

#include "abc/engine.h"

namespace abc {
namespace config {

// addon 侧设置（契约 config.md 第 2 节；语义与默认值以契约为准）。
// 结构体与字段由 A5 定义与文档化，供 A4 消费。
struct AddonSettings {
  // 编译后的引擎索引路径（abc::loadDictionaryFromFile 入参）。
  // 默认：$XDG_DATA_HOME/abc/index.abcdix，否则 $HOME/.local/share/abc/index.abcdix。
  std::string indexPath;
  // 中英文切换热键（fcitx5 键名，A4 据此映射为 kModeSwitch）。默认 "Shift"。
  std::string modeSwitchKey = "Shift";
  // 预编辑是否送入应用窗口（fcitx5 客户端 preedit）。默认 true。
  bool preeditInClient = true;
};

// 解析结果。warnings 为脱敏警告（"warning: line N: key 'k': 原因"，
// 键名 + 原因，按契约绝不包含值内容）；由调用方决定输出渠道。
struct ParseResult {
  abc::EngineConfig engine;  // 永远处于合法范围，可直接使用
  AddonSettings addon;
  std::vector<std::string> warnings;
};

// 解析显式路径的配置文件（UTF-8，允许 BOM）。
// 文件不存在：全默认值 + 一条警告（契约第 3 节，不失败）。
ParseResult parseFile(const std::string& path);

// 从内存解析（测试/嵌入式用途；语义与 parseFile 完全一致）。
ParseResult parseString(const std::string& content);

// 默认 indexPath（契约第 4 节"用户数据目录"的实现口径）：
// XDG_DATA_HOME 非空 → $XDG_DATA_HOME/abc/index.abcdix；
// 否则 HOME 非空 → $HOME/.local/share/abc/index.abcdix；
// 两者皆缺 → "abc/index.abcdix" 并产生一条警告。
std::string defaultIndexPath(std::vector<std::string>* warnings);

}  // namespace config
}  // namespace abc

#endif  // ABC_CONFIG_H_
