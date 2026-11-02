# 产物清单（manifest）

> 维护方：A6。只登记经审查、可交接且与提交号/环境关联的产物；临时文件不入库。

## 当前登记（2026-10-28，集成验收轮次 1，验收提交 `a6d95ac`）

| 产物 | 类型 | 来源提交 / 校验 | 位置 | 状态 |
|---|---|---|---|---|
| A0 契约归档 `abc-a0-f314103.tar.gz` | 提交导出 | A0 `f314103`，sha256 `b07b2012d7c3a13a08a3f5f1f0aa7f19afe87170f87cce408d6d1b1ba8fd2bf8` | `D:\workbuddy\abc-a0-f314103.tar.gz`（A0 登记，本表转录） | 复核引用 |
| A1 集成验收归档 `abc-a6-a6d95ac.tar.gz` | 提交导出（git archive，HEAD=a6d95ac） | sha256 `16244dff684b67bc3cabf447e4fdf40756471f47d18cf0d33c33b9cf4540af92` | `D:\workbuddy\tmp\abc-a6-a6d95ac.tar.gz`；目标机副本 `/home/londy/abc-a6-verify-a6d95ac/` | 已用于目标机独立复跑 |
| 目标机构建树 `abc-a6-verify-a6d95ac/src/build` | 构建目录（复核证据，非发布产物） | 提交 `a6d95ac`，GCC 16.2.1 / CMake 4.4.4 / Ninja 1.13.2 | 目标机 `$HOME`（`/home/londy/abc-a6-verify-a6d95ac/`） | 保留备查；可安全删除 |
| 目标机构建树（A0 历史） | 构建目录 | A0 `f314103` | `/home/londy/abc-a0-verify-f314103-082431/` | A0 证据，保留 |
| 脱敏日志 `logs/a6/2026-10-28-*.md` | 验收日志 | 见各文件头部提交号 | 仓库 `logs/a6/` | 已入库 |

## 尚无产物（缺口即缺陷，见 docs/验收记录.md）

| 预期产物 | 产生任务 | 状态 |
|---|---|---|
| A2 导入器可执行 `abc_dict_import` + 导入报告 | A2 | 已有实现提交 `f35d867`；轮次 2 未复跑归档构建 |
| A2 最小索引 fixture（运行时生成） | A2→A3 | 轮次 2 集成测试临时目录生成，不作为发布产物入库 |
| A3 词典库 `libabc_dictionary.a` | A3 | 已有实现提交 `76a83ee`；轮次 2 未复跑归档构建 |
| A4 addon 共享库与用户级安装布局 | A4 | 历史实现 `de63841`；新修复工作树未提交；本轮无原生 `.so` 产物 |
| A5 用户级安装清单与备份 | A5 | 脚本已有提交；轮次 2 未执行真实安装/回滚 |

## 归档规则重申

## 轮次 2（2026-10-29）状态

- 本轮未产生新的可发布构建产物、A2 索引归档或 Wayland 证据；不把目标机路径上未核验的 A4 日志声称登记为本轮独立复验产物。
- 本地临时 A4 测试可执行文件位于会话临时目录，非交付产物，不入库。
- `deploy/fcitx5-addon/logs/target-host-ssh-2026-10-29.log` 属于未跟踪 A4 所有权文件，包含主机/账号等连接元数据且叙述与后续文档矛盾；A6 不读取、不移动、不归档、不提交或引用该原文，待材料所有者审查并重制脱敏摘要。
- A4 构建/隔离探测日志仍位于 A4 所有权目录；其 hash 仅为既有 A4 文档登记，本轮没有独立复算或重新下载核验。隔离探测报告未发现 addon 注册，不能当作注册通过证据。
- 轮次 2 结论及复验要求见 [`docs/验收记录.md`](../docs/验收记录.md) §10。
- 轮次 2 本机脱敏命令/结果日志：[`logs/a6/2026-10-29-round2-local.md`](../logs/a6/2026-10-29-round2-local.md)。不含发布二进制或敏感目标机信息。

## 轮次 3（2026-10-29）目标机复验

目标机原始文件保存在 `/home/londy/abc-a6-clean/`；以下路径不含凭据及未脱敏 SSH 原始日志：

| 证据 | 用途 | 绝对路径 | 状态 |
|---|---|---|---|
| CMake configure | 干净目标机配置 | `/home/londy/abc-a6-clean/configure.log` | 通过 |
| Native build | GCC 16.2.1、fcitx5 5.1.23 addon（链接 A1/A3/A5 PIC archives） | `/home/londy/abc-a6-clean/build.log` | `libabc.so` 成功链接，符号核验 |
| CTest | 全量 14 tests | `/home/londy/abc-a6-clean/ctest.log` | 14/14 |
| A2 report/index | 自造最小 fixture；8 records、6 codes、无 skip | `/home/londy/abc-a6-clean/import-report.json`、`/home/londy/abc-a6-clean/user-home/.local/share/abc/index.abcdix` | 生成成功 |
| Staging/install/verify | 隔离 HOME 用户级部署 | `/home/londy/abc-a6-clean/staging.log`、`/home/londy/abc-a6-clean/install-user.log`、`/home/londy/abc-a6-clean/verify.log` | 通过 |
| Repeat/uninstall | 幂等与隔离卸载 | `/home/londy/abc-a6-clean/install-repeat.log`、`/home/londy/abc-a6-clean/verify-repeat.log`、`/home/londy/abc-a6-clean/uninstall.log` | 脚本通过；旧输入法文件留存 |
| Real HOME install | 用户前缀 `/home/londy` | `/home/londy/abc-a6-clean/install-real-home.log`、`/home/londy/abc-a6-clean/verify-real-home.log` | 首次安装使用错误文件名，不作激活验收 |
| Fixed addon install | 用户前缀 `/home/londy`，`libabc.so` | `/home/londy/abc-a6-clean/install-real-home.log`、`/home/londy/abc-a6-clean/verify-real-home.log`；库 SHA256 `183987b13472339f66b270a570d8d995fee959ad54cadc9a85a395412c8302b5` | 已安装并校验 5 项；旧未登记 `abc.so` 残留保留未删 |
| Fcitx diagnostics | addon/输入法激活与索引读取 | `/home/londy/abc-a6-clean/fcitx5-session-fixed.log` | `Loaded addon abc`、`abc dictionary loaded`；CurrentInputMethod=abc |
| Qt/GTK probes | 原生 Wayland toolkit 窗口 | `/home/londy/abc-a6-clean/gui-bin/qt-probe`、`/home/londy/abc-a6-clean/gui-bin/gtk-probe` | 可启动；真实按键用例未执行 |

本轮结论与绝对路径清单见 [`docs/验收记录.md`](../docs/验收记录.md) §11。真实输入截图/录屏不存在，不登记模拟证据。

1. 只有与提交号绑定、可复现的产物才登记；目标机数据先审查（无用户数据/凭据）再归档。
2. addon 交付后：把 `.so`、安装布局清单、构建环境三元组登记入本表，并附 sha256。
3. 索引产物仅登记**最小 fixture**（自造词条），完整第三方词库不入仓库、不入归档。
