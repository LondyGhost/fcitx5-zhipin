# 智拼（ZhiPin）— fcitx5 输入法引擎

一个 **fcitx5 原生输入法引擎**，在 Linux 上复刻经典 ABC 字母输入风格的手感。
基于内嵌 librime 会话（词库底座为 [rime-ice](https://github.com/iDvel/rime-ice) 雾凇拼音），
但不使用 fcitx5-rime 前端——全部键位语义由本项目的 C++ 引擎实现。

English: ZhiPin — an fcitx5 input method engine that reproduces the classic
ABC-style typing flow on Linux, powered by an embedded librime session
(dictionary base: [rime-ice](https://github.com/iDvel/rime-ice)).

## 关于名字

**智拼**是本项目的正式名称。它与「智能ABC」只有输入风格上的渊源，
不是同一产品，也没有任何商标关联——键位语义、引擎实现、词库链路全部为本项目原创。

## 特性

- **打字不出候选**：输入拼音时只显示字母缓冲，界面干净
- **空格出词，空格上屏**：第一次空格放出候选，第二次空格上屏高亮项（默认第一项）
- **候选出来后继续打字 = 首选项直接上屏**：`nihao` 空格出候选，接着打 `woshi`，
  「你好」立即上屏、预编辑变成 `woshi`（经典 ABC 手感）
- **词尽出字**：词组候选翻完后自动接单字页
- **分阶段逐字选字**：多音节输入中选一个单字，立即进入下一音节，
  如 `ceshi` → 选「测」→ 组字框变「测shi」→ 选「是」→ 上屏「测是」
- **主键盘 `-` / `=` 翻页**（不是小键盘）
- **回车 / Shift+字母 上屏字母**：反悔通道，拼音原样或字母直接进应用
- **中文标点全角**：`,.` → `，。`，双引号交替出 `“ ”`，英文模式原样
- **大字表**：启用 rime-ice 41448 全字表（含生僻字），单字候选自动加深采集
- **经典 ABC 风格主题**：白底黑框、蓝色高亮、竖排候选（fcitx5 classicui）
- **图形化设置界面**：在 fcitx5 配置工具里直接调候选数、初始状态、切换键等

## 键位

| 按键 | 行为 |
|---|---|
| 字母 | 进入缓冲（不显示候选） |
| 空格 | 第一次：放出候选；第二次：上屏高亮项 |
| 字母（候选已放出） | 高亮项立即上屏，字母开始新一轮组合 |
| 数字 1–9 | 候选选择（放出候选后） |
| `-` / `=` | 上翻 / 下翻页（主键盘） |
| ← → | 移动高亮 |
| ↑ ↓ | 翻页 |
| 回车 | 字母原样上屏（分阶段选字时：已选字 + 剩余字母） |
| Shift+字母 | 字母上屏（继续组合） |
| 退格 | 删字母；分阶段选字时先撤已选的字 |
| Esc | 清空组合 |
| Shift（单击） | 中英切换 |
| 标点 | 中文态出全角标点 |

## 安装

### Arch Linux

```sh
git clone https://github.com/REPLACE-ME/fcitx5-zhipin.git
cd fcitx5-zhipin
packaging/package.sh arch
sudo pacman -U fcitx5-zhipin-*.pkg.tar.zst
```

### Debian / Ubuntu

```sh
packaging/package.sh deb
sudo apt install ./fcitx5-zhipin_*.deb
```

### RPM（Fedora / openEuler 等）

```sh
packaging/package.sh rpm
sudo dnf install ./fcitx5-zhipin-*.rpm
```

### 依赖

- `fcitx5`、`librime`、`rime-data`
- 推荐 [rime-ice](https://github.com/iDvel/rime-ice)：把仓库内容放进
  `~/.local/share/fcitx5/rime/`（abc 方案的词库底座，含大字表 41448）

### 启用

1. 重启 fcitx5：`fcitx5 -r`（或注销重登）
2. 在 fcitx5 配置工具中添加输入法「智拼」
3. 外观主题选「智能ABC」（可选，经典白底黑框样式）
4. 设置：fcitx5 配置工具 → 附加组件 → 智拼 → 配置

## 设置项

| 项 | 说明 | 默认 |
|---|---|---|
| 候选词数量 | 一页最多 9 个 | 9 |
| 初始输入状态 | 中文 / 英文 | 中文 |
| 最大编码长度 | 超限字符直接转发应用 | 64 |
| 空格无候选时 | 提交原文 / 保留编码 | 提交原文 |
| 切换中英文时未提交编码 | 提交原文 / 清除编码 | 提交原文 |
| 词典索引路径 | 自研索引（librime 可用时不加载） | 空 |
| 中英文切换键 | Shift / Control / Alt / Super | Shift_L |
| 预编辑文本送入应用窗口 | 客户端内联绘制 | 否 |

配置保存在 `~/.config/fcitx5/conf/abc.conf`；首次启动会从旧的
`~/.config/abc/config.ini` 自动迁移。

## 从源码构建

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ~/.local   # 用户级安装
```

用户级安装后需让 fcitx5 找到 addon 库：

```sh
export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
```

依赖：CMake ≥ 3.16、C++17 编译器（addon 需 C++20）、fcitx5 开发头文件、librime。
缺 fcitx5 SDK 或 librime 时对应模块自动跳过（引擎有自研词典回退路径）。

## 架构

```
应用 ← fcitx5(框架/Wayland) ← abc addon(适配层) ← abc 引擎(librime 会话封装)
                                                    └─ librime + rime-ice 词库
```

- `source/fcitx5-addon/`：fcitx5 适配层（键转换、UI 映射、配置界面定义）
- `source/rime/`：librime 会话封装——ABC 键位语义（两段空格、候选上屏、
  分阶段选字、词尽出字、全角标点）全部在这里的 C++ 实现，不依赖 Lua 脚本
- `source/core|dictionary|config|dict-importer/`：自研引擎回退路径（librime 不可用时）
- `packaging/`：deb / rpm / Arch 打包、图标与随包 Rime 数据，见 [packaging/README.md](packaging/README.md)
- `docs/`：接口契约、模块文档与验收记录
- `tools/`：探针（`rime_probe_engine`，支持任意键序列回放）与音节表生成器

## 致谢

- [rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice)（Dvel）：词库与 schema 底座
- [librime](https://github.com/rime/librime)（RIME 开发者）：中州韵引擎
- [fcitx5](https://github.com/fcitx/fcitx5)（Weng Xuetian）：输入法框架

## License

GPL-3.0（见 [LICENSE](LICENSE)）。`packaging/rime/abc.schema.yaml` 派生自
rime-ice 项目，遵循其上游许可。
