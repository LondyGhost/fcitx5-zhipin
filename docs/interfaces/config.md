# 配置边界契约（config.md）

关联公共头文件：`source/include/abc/engine.h`（`EngineConfig`）。
配置解析实现方：A5（`source/config/**`）。装配消费方：A4（addon）。
本契约冻结于接口版本 1.0.0（状态见 [README.md](README.md)）。

## 1. 配置流向（冻结）

```text
用户配置文件（本契约第 3 节键表）
  │ A5 source/config 解析与校验（键名/类型/范围）
  ▼
abc::EngineConfig ──────────────► abc::createEngineSession(cfg, dictionary)   // A1 核心
AddonSettings（第 2 节）────────► A4 addon（热键识别、索引路径装载）
```

- **核心不读配置文件**：`EngineSession` 只接受构造时注入的 `EngineConfig` 值。
- **addon 不解析配置文件**：A4 只消费 A5 解析产物（`EngineConfig` + `AddonSettings`）。
- A5 是唯一允许读取用户配置文件的位置；文件格式在第 3 节冻结，避免 A5 自行发明语法。

## 2. AddonSettings（addon 侧设置，A4 消费）

`EngineConfig` 之外、由 A4 需要的设置项。A5 以等价的简单结构体传递
（结构体由 A5 在 `source/config/` 内定义并文档化，字段语义以本契约为准）：

| 设置项 | 语义 | 默认 |
|---|---|---|
| `indexPath` | 编译后的引擎索引文件路径（`loadDictionaryFromFile` 入参） | 用户数据目录下 `abc/index.abcdix` |
| `modeSwitchKey` | 中英文切换热键的 fcitx5 键名（A4 据此把按键映射为 `kModeSwitch`） | `Shift` |
| `preeditInClient` | 预编辑是否送入应用窗口（fcitx5 客户端 preedit） | `true` |

说明：

- `modeSwitchKey` 只影响**本引擎会话内**的切换键，不替代 fcitx5 全局触发键；
  A4 必须在按键适配时优先匹配该键再走常规转换表。
- v1 不含用户词条开关；`indexPath` 是唯一索引来源（多输入文件在 A2 导入期合并，
  见 [`dictionary.md`](dictionary.md) 总览）。用户词频属于后续 minor 演进项。

## 3. 用户配置文件格式（冻结）

- 编码 UTF-8（允许 UTF-8 BOM）；每行 `key = value`；`#` 起始为注释；空行忽略。
- `value` 为行内 `=` 后去除首尾空白的内容，**支持含空格路径**（无需引号）；
  值内不可包含换行。
- 解析规则（A5 义务）：
  - 未知键：警告并忽略（记入解析报告，不失败）。
  - 值非法/越界：警告并回退默认值，不失败（输入法必须总能启动）。
  - 文件不存在：使用全默认值（警告），不失败。
  - 重复键：以最后一次为准（警告）。
- 该文件**禁止**存放凭据；A5 解析器与文档必须如此声明。

## 4. 冻结键表

`EngineConfig` 映射键（字段语义见 [`engine.md`](engine.md) 第 3 节）：

| 键 | 类型 | 合法值 | 默认 | 目标 |
|---|---|---|---|---|
| `initialMode` | 枚举 | `chinese` \| `english` | `chinese` | `EngineConfig.initialMode` |
| `pageSize` | 整数 | 1..9 | `9` | `EngineConfig.pageSize` |
| `maxBufferChars` | 整数 | 1..256 | `64` | `EngineConfig.maxBufferChars` |
| `spaceNoCandidatePolicy` | 枚举 | `commit_raw` \| `keep_buffer` | `commit_raw` | `EngineConfig.spaceNoCandidatePolicy` |
| `modeSwitchCompositionPolicy` | 枚举 | `commit_raw` \| `clear` | `commit_raw` | `EngineConfig.modeSwitchCompositionPolicy` |

`AddonSettings` 映射键（语义见第 2 节）：

| 键 | 类型 | 默认 |
|---|---|---|
| `indexPath` | 路径（可含空格） | 用户数据目录下 `abc/index.abcdix` |
| `modeSwitchKey` | fcitx5 键名字符串 | `Shift` |
| `preeditInClient` | `true` \| `false` | `true` |

- 键名区分大小写；整数接受十进制 ASCII，拒绝空串与溢出。
- 新增键属于 minor 演进：由 A0/集成负责人审批后追加到本表，A5 同步实现，
  旧文件（缺键）必须仍可解析。

## 5. 示例

```ini
# ~/.config/abc/config.ini
initialMode = chinese
pageSize    = 9
indexPath   = ~/.local/share/abc/index.abcdix
modeSwitchKey = Shift
```

## 6. 错误处理汇总（A5）

1. 任何解析失败不产生异常、不终止进程；总是产出可用的默认或回退配置。
2. 全部异常情况必须生成一行脱敏警告（键名 + 原因；不含值内容——值可能含路径隐私）。
3. 解析结果必须可直接用于 `createEngineSession` 而无需二次校验（范围已由 A5 保证）。
