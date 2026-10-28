# 接口契约（A0 维护）

本目录是 A1–A5 并行开发的**唯一**公共接口契约。公共头文件位于 `source/include/abc/`，与本目录文档一一对应；两者由任务 A0（或集成负责人）单点维护，其他任务不得修改。

## 契约文件索引

| 文件 | 内容 | 对应公共头文件 | 主要消费任务 |
|---|---|---|---|
| [`engine.md`](engine.md) | `KeyCommand`、`EngineConfig`、`EngineResult`、`Effect`、`EngineSession`、会话工厂、按键转移表、生命周期与线程约束 | `abc/key.h`、`abc/engine.h` | A1（实现）、A4（适配） |
| [`dictionary.md`](dictionary.md) | `Candidate`、`CandidateBatch`、`Dictionary`、`DictionaryStatus`、加载器、排序/去重/stable ID 规则 | `abc/dictionary.h` | A3（实现）、A1/A4（使用）、A2（索引生产方） |
| [`index-format.md`](index-format.md) | 索引二进制格式 v1.0、版本策略、校验与必须拒绝的错误 | 常量 `abc/index_format.h` | A2（写入）、A3（读取） |
| [`config.md`](config.md) | 配置边界：键名/类型/默认值/范围、解析规则、`EngineConfig` 流向 | 字段 `abc/engine.h` | A5（解析）、A4（装配） |
| [`build.md`](build.md) | 构建骨架、模块注册规则、依赖方向、目标命名 | — | A1–A5 |
| [`confirmations.md`](confirmations.md) | A1–A5 下游确认记录与冻结流程 | — | A1–A5、集成负责人 |
| [`delivery-A0.md`](delivery-A0.md) | A0 交付记录：文件清单、构建/测试命令与结果 | — | 集成负责人 |

## 版本与冻结状态

- **接口版本：`1.0.0`**（`abc/version.h`）。索引格式版本：`1.0`（`abc/index_format.h`），二者独立演进。
- **冻结状态：待确认（v1.0-rc1）。** 本契约已按整体方案第 5、8 节定稿；按任务书要求，A1–A5 负责人在 [`confirmations.md`](confirmations.md) 书面确认后方可宣布冻结。确认期间提出的变更由 A0 评估并更新版本号。
- 版本语义：
  - `MAJOR`：不兼容变更（删除/改名/语义反转），所有下游必须同步修订。
  - `MINOR`：向后兼容的追加（新增枚举值、新增带默认值的字段、新增接口）。
  - `PATCH`：纯文档或注释澄清，不改任何声明。
- 冻结后变更流程：由集成负责人审批 → A0 修改并提升版本 → 同步所有下游任务并记录在 [`confirmations.md`](confirmations.md)。

## 全局约束（对所有契约生效）

1. **纯核心不依赖 fcitx5。** `source/include/abc/` 下的公共头文件只允许包含 C++ 标准库头和 `abc/` 自身；禁止包含 fcitx5、X11、Wayland 或任何第三方头。核心（core/dictionary/config/dict-importer）不得反向依赖 addon。
2. **addon 依赖方向单向。** addon（A4）只允许使用 `abc_interfaces` 公共头与核心公开的工厂函数；不允许访问核心内部头。
3. **`EngineSession` 是输入缓冲与组合状态的唯一所有者。** addon 不得维护第二份缓冲/候选状态；每次按键后以 `EngineResult` 快照为准。
4. **字典运行时接口只读。** 查询不修改索引状态；写侧只存在于离线导入器（A2）。
5. **错误通过返回值表达，不使用异常跨越模块边界**；任何失败路径不得静默丢数据、不得终止进程。
6. **字符串编码一律 UTF-8**（`std::string`/`std::string_view` 载体），预编辑、候选、提交文本、索引内容均如此。
7. 禁止 XIM/XTest/xdotool/模拟按键注入（项目级硬性边界，此处重申）。

## 术语

- **缓冲（buffer）**：当前组合中的规范化小写字母序列，仅存在于 `EngineSession` 内部。
- **组合（composition）**：缓冲 + 由其产生的 preedit/候选页展示状态。
- **原文（raw buffer）**：当前缓冲字符串本身（已规范化为小写），区别于候选词。
- **快照（snapshot）**：`EngineResult` 中描述的按键处理后的完整状态。
