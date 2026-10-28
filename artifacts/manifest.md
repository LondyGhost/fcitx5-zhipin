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
| A2 导入器可执行 `abc_dict_import` + 导入报告 | A2 | 未交付（无任何实现文件） |
| 引擎索引 `index.abcdix`（最小 fixture 产物） | A2→A3 链路 | 未产生 |
| A3 词典库 `libabc_dictionary.a` | A3 | 未交付（仅草稿 `internal/bytes.hpp`） |
| A4 addon 共享库与用户级安装布局 | A4 | 未交付 |
| 用户级安装清单（`~/.abc-im/`）与备份 | A5 脚本 | 未交付（脚本尚缺 install/uninstall） |

## 归档规则重申

1. 只有与提交号绑定、可复现的产物才登记；目标机数据先审查（无用户数据/凭据）再归档。
2. addon 交付后：把 `.so`、安装布局清单、构建环境三元组登记入本表，并附 sha256。
3. 索引产物仅登记**最小 fixture**（自造词条），完整第三方词库不入仓库、不入归档。
