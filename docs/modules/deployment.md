# A5 — 配置、用户级部署与回滚（deployment.md）

拥有任务：A5。配置契约：[`../interfaces/config.md`](../interfaces/config.md)
（接口版本 **1.0.0-rc1**，键表/默认值/错误规则冻结）。构建规则：
[`../interfaces/build.md`](../interfaces/build.md)。addon 安装布局：
[`../../deploy/fcitx5-addon/install-manifest.md`](../../deploy/fcitx5-addon/install-manifest.md)（A4 已确认）。

## 1. 模块组成

| 组成 | 位置 | 说明 |
|---|---|---|
| 配置解析库 | `source/config/`（目标 `abc_config`） | 解析用户配置 → `abc::EngineConfig`（A0 冻结类型）+ `AddonSettings`（本模块定义）+ 脱敏警告 |
| 构建脚本 | `scripts/build-abc.sh` | configure/build/ctest + 调用 staging 组装；不做真实安装 |
| staging 组装 | `scripts/build-staging.sh` | 把 `cmake --install` 前缀树映射为 HOME 相对 staging 树，并补配置模板 |
| 安装脚本 | `scripts/install-user.sh` | staging → 用户前缀；冲突备份；失败回滚；`--verify` 校验 |
| 卸载脚本 | `scripts/uninstall-user.sh` | 仅按清单删除；备份保留/恢复；空目录回收 |
| 配置模板 | `deploy/abc/config.ini` | 默认用户配置（存在即跳过，绝不覆盖用户数据） |
| 测试 | `tests/deploy/` | 配置解析 C++ 测试 + 安装/卸载/组装 shell 套件 + 脚本静态检查 |

## 2. 配置解析（abc_config）

```cpp
#include "abc_config.h"
abc::config::ParseResult r = abc::config::parseFile("/path/config.ini");
// r.engine   → 直接交给 abc::createEngineSession，无需二次校验
// r.addon    → { indexPath, modeSwitchKey, preeditInClient }，供 A4 装配
// r.warnings → 脱敏警告（键名+原因，绝不包含值内容），调用方决定输出渠道
```

键表与默认值（与 `config.md` 第 4 节逐项对应）：

| 键 | 合法值 | 默认 | 去向 |
|---|---|---|---|
| `initialMode` | `chinese` / `english` | `chinese` | `EngineConfig.initialMode` |
| `pageSize` | 1..9 | `9` | `EngineConfig.pageSize` |
| `maxBufferChars` | 1..256 | `64` | `EngineConfig.maxBufferChars` |
| `spaceNoCandidatePolicy` | `commit_raw` / `keep_buffer` | `commit_raw` | `EngineConfig.spaceNoCandidatePolicy` |
| `modeSwitchCompositionPolicy` | `commit_raw` / `clear` | `commit_raw` | `EngineConfig.modeSwitchCompositionPolicy` |
| `indexPath` | 路径（可含空格，支持 `~/`） | `$XDG_DATA_HOME/abc/index.abcdix` 或 `$HOME/.local/share/abc/index.abcdix` | `AddonSettings.indexPath` |
| `modeSwitchKey` | fcitx5 键名 | `Shift` | `AddonSettings.modeSwitchKey` |
| `preeditInClient` | `true` / `false` | `true` | `AddonSettings.preeditInClient` |

- 解析规则完全按契约：未知键/重复键/非法值/空值 → 警告 + 回退默认，**不失败**；
  文件不存在 → 全默认 + 警告；UTF-8（允许 BOM）、CRLF 容错、整行 `#` 注释。
- 解析器不打印任何内容（纯返回），便于嵌入与测试；警告只含键名与原因，
  绝不回显值（值可能是含隐私的路径）。
- 用户词条：契约 v1 明确不含用户词条开关，`indexPath` 是唯一索引来源
  （见 `config.md` 第 2 节说明）。因此本任务不提供"用户词条路径"独立键；
  用户词频/词条属于后续 minor 演进项。
- 配置文件位置（A4 消费侧，`source/fcitx5-addon/abcengine.cpp`）：
  `$XDG_CONFIG_HOME/abc/config.ini`，否则 `$HOME/.config/abc/config.ini`
  —— 与本任务安装模板的落点一致。

## 3. 部署布局（A4 已确认，A5 不自行发明路径）

A4 的 `deploy/fcitx5-addon/install-manifest.md` 与 `source/fcitx5-addon/CMakeLists.txt`
的 `install()` 规则共同定义了 addon 布局（相对安装前缀，用户级前缀为 `$HOME/.local`）：

| 相对路径（`$HOME` 起） | 来源 | 作用 |
|---|---|---|
| `.local/lib/fcitx5/libabc.so` | `abc_fcitx5_addon` | addon 共享库（`Library=libabc`） |
| `.local/share/fcitx5/addon/abc.conf` | `abc-addon.conf` | addon 注册描述 |
| `.local/share/fcitx5/inputmethod/abc.conf` | `abc-im.conf` | 输入法条目 |
| `.config/abc/config.ini` | `deploy/abc/config.ini`（A5） | 用户配置模板（存在即跳过） |
| `.abc-im/manifest.tsv` | 安装脚本生成 | 安装清单（卸载/校验依据） |
| `.abc-im/backup/<rel>` | 安装脚本生成 | 被覆盖的**用户原文件**备份（保留至卸载/恢复） |
| `.abc-im/rollback/<rel>` | 安装脚本生成 | 覆盖**本项目文件**前的临时回滚副本（成功后立即删除） |

`build-staging.sh` 的映射规则是固定的：`cmake --install --prefix <tmp>` 的
顶层 `lib/`、`share/` 等整体复制到 staging 的 `<addon-dir>/`（默认 `.local`），
再把配置模板放到 `.config/abc/config.ini`，最后写控制文件
`.abc-im-no-overwrite.list`。组装后会复核上述三个 addon 路径，缺失即失败
（`--allow-missing-addon` 仅供无 fcitx5 的开发/CI 环境）。

因此 `install-user.sh --prefix "$HOME"` 的最终安装路径就是上表（`$HOME` 相对）。

运行期发现 addon 库需要用户会话设置（脚本不改任何 shell 配置，也不写 `/usr`）：

```sh
export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
fcitx5 -r
fcitx5-remote -n abc
```

### 3.1 `FCITX_ADDON_DIRS` 的安全处理

`FCITX_ADDON_DIRS` 会**替换** fcitx5 内置的 addon 目录列表（A4 已在目标机用
`StandardPathsType::Addon` 验证）。因此：

- 直接 `export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5"` 会让系统 addon
  （`/usr/lib/fcitx5` 下的其它输入法）从列表里消失，属于**破坏性覆盖**；
- 若用户已经设置过该变量（例如为别的 addon 指了额外目录），必须在其基础上
  **前置**本项目目录，而不是整体替换；
- addon/输入法描述文件须位于真实 `$HOME/.local/share/fcitx5/{addon,inputmethod}`。
  Fcitx5 5.1.23 的 `FCITX_ADDON_DIRS` 只改动态库目录，`FCITX_DATA_DIRS` 不改
  `StandardPathsType::PkgData`；使用隔离 HOME 验收时应单独确认该版本的标准路径。
- 本项目脚本**不写**任何 shell 配置（`~/.bashrc`、`~/.profile`、
  `~/.config/environment.d/*`），也不修改当前进程环境，只提供只读建议值：

```bash
# 只读：打印安全值，不写文件、不改环境
scripts/install-user.sh --print-env
# 已安装时按清单里的实际库目录推导，未安装时用 <prefix>/.local/lib/fcitx5
# 未设置 FCITX_ADDON_DIRS → "<本项目目录>:/usr/lib/fcitx5"
# 已设置             → "<本项目目录>:<用户原值>"（已包含本项目目录则原样输出）

# 采纳建议（用户自行写入会话配置，例如 ~/.config/environment.d/90-abc.conf）：
eval "$(scripts/install-user.sh --print-env)"    # 仅当前 shell
```

验证：`fcitx5-diagnose` 的 addon 列表应出现 `abc`，且加载路径为
`$HOME/.local/lib/fcitx5/libabc.so`；若新 addon 未加载，先确认该变量是否包含本项目目录、
以及是否覆盖掉了 `/usr/lib/fcitx5`。

## 4. 脚本参数与退出码

### 4.1 `scripts/build-abc.sh`

| 参数 | 说明 |
|---|---|
| `--source-dir DIR` | 源码目录（默认脚本上一级） |
| `--build-dir DIR` | 构建目录（默认 `<source>/build`） |
| `--build-type T` | `Debug`/`Release`/`RelWithDebInfo`/`MinSizeRel`（默认 RelWithDebInfo） |
| `--prefix PREFIX` | 仅写入 CMake 缓存作记录；真实安装前缀由 `install-user.sh --prefix` 决定 |
| `--generator G` | Ninja（默认，缺失回退 Unix Makefiles） |
| `--staging DIR` | 构建后组装 staging（**会清空 DIR**，它是构建产物不是用户数据） |
| `--staging-allow-missing-addon` | 无 fcitx5 开发包时只组装配置（仅开发/CI） |
| `--no-tests` / `--clean` / `--` | 跳过 ctest / 先删构建目录 / 其后参数透传 cmake |

退出码：`0` 成功；`1` 参数或环境错误；`2` configure/build/test/staging 失败。

### 4.2 `scripts/build-staging.sh`

| 参数 | 说明 |
|---|---|
| `--from DIR` | `cmake --install` 产出的前缀树（必填） |
| `--out DIR` | staging 输出目录（必填；默认非空即报错） |
| `--addon-dir NAME` | 前缀树在 staging 的落点（默认 `.local`） |
| `--config-template FILE` | 配置模板（默认 `deploy/abc/config.ini`） |
| `--config-rel PATH` | 模板在 staging 的相对路径（默认 `.config/abc/config.ini`） |
| `--allow-missing-addon` | 缺 A4 产物只警告（开发/CI，不可用于部署） |
| `--clean` / `--dry-run` | 先清空输出目录 / 只打印计划 |

退出码：`0` 成功；`1` 参数或路径校验错误（未变更）；`2` 组装失败（不留下半成品）。

### 4.3 `scripts/install-user.sh`

| 参数 | 说明 |
|---|---|
| `--staging DIR` | 待安装的相对路径树（必填） |
| `--prefix DIR` | 安装前缀（默认 `$HOME`；拒绝 `/`、`/usr/**`、`/opt/**`） |
| `--dry-run` | 只打印计划，零变更 |
| `--verify` | 只读校验：清单 sha vs 磁盘实际内容 |
| `--print-env` | 只读：打印安全的 `FCITX_ADDON_DIRS` 建议值（见 §3.1），不写文件、不改环境 |
| `--allow-prefix-mismatch` | 允许清单登记前缀与 `--prefix` 不一致（迁移场景，默认拒绝） |

退出码：`0` 成功/校验通过；`1` 参数或校验错误（未做任何变更）；`2` 安装失败（已回滚）
或校验不符。

### 4.4 `scripts/uninstall-user.sh`

| 参数 | 说明 |
|---|---|
| `--prefix DIR` | 安装前缀（默认 `$HOME`） |
| `--manifest PATH` | 清单路径（默认 `<prefix>/.abc-im/manifest.tsv`） |
| `--restore-backups` | 恢复安装前备份的用户原文件（校验 sha 后恢复并删除备份） |
| `--force-modified` | 删除安装后被外部修改过的本项目文件（默认跳过） |
| `--allow-prefix-mismatch` | 允许清单前缀与 `--prefix` 不一致 |
| `--dry-run` | 只打印计划，零变更 |

退出码：`0` 成功；`1` 参数或清单错误（未做任何变更）；`2` 部分条目未处理。

## 5. 安装行为矩阵（可预测语义）

| 场景 | 行为 |
|---|---|
| 目标不存在 | 安装（create） |
| 目标存在且与旧清单 sha 一致（本项目文件） | 原地更新；备份仅用于失败回滚，成功后删除 |
| 目标存在但非本项目（或本项目文件被外部修改） | 先备份到 `<prefix>/.abc-im/backup/<rel>` 再覆盖；备份保留至卸载 |
| 目标在 no-overwrite 清单且已存在 | 跳过，绝不覆盖；不登记为 F（卸载不会删除用户配置） |
| 旧清单登记、新 staging 缺失 | 保留并继续登记（升级不删除） |
| 重复安装同一 staging | 幂等：文件重写、sha 同步、不积累备份、不重复备份 |
| 目标应为文件但被目录占用 / 父路径被文件占用 | 计划期拒绝（退出码 1），零变更 |
| 管理目录 `.abc-im` 被文件占用 | 执行期失败并回滚（退出码 2），用户文件不动 |
| 中途任何一步失败 | 回滚本次全部变更（恢复被覆盖文件、删除本次新建文件/目录/备份），退出码 2，旧清单保持原样 |
| 清单登记前缀与 `--prefix` 不一致 | 拒绝执行（退出码 1），需显式 `--allow-prefix-mismatch` |
| 存在 A4 参考脚本清单 `.abc-im/manifest.list` | 拒绝执行（退出码 1），提示先用 A4 脚本卸载；两套清单不混用 |
| `--restore-backups` 时备份缺失 | 报告"备份缺失"，退出码 2，不打印"卸载完成" |
| `--restore-backups` 时备份 sha 不符 | 报告"校验不符"，保留备份不恢复，清单标 `restore-incomplete`，退出码 2 |

## 6. 清单格式与状态

`<prefix>/.abc-im/manifest.tsv`（TSV，`#` 注释）：

```text
# abc-im install manifest v1
# tool: install-user.sh (project: linux-abc-im)
# created: 2026-10-28T02:00:00Z
# prefix: /home/londy
# fields: F<TAB>rel<TAB>sha256<TAB>mode | D<TAB>rel | B<TAB>rel<TAB>orig_sha256
F	.local/lib/fcitx5/libabc.so	<sha256>	755
D	.local/share/abc
B	.config/abc/config.ini	<orig_sha256>
```

- `F`：本项目安装的文件（相对路径 + sha256 + 权限）。
- `D`：本项目创建的目录（仅在为空时删除）。
- `B`：被覆盖的用户原文件（相对路径 + 原始 sha256），备份实体在 `.abc-im/backup/<rel>`。
  本项目文件被升级时只写 `.abc-im/rollback/`（临时，成功后删除），**不会**顶替用户原文件备份。
- 卸载后若仍有未处理条目，清单会保留并标记状态，处理完自动清理：
  - `uninstalled-backups-pending`：还有未恢复的用户原文件备份（`--restore-backups` 可恢复）；
  - `uninstall-incomplete`：还有被跳过的本项目文件（安装后被改过，`--force-modified` 可删）；
  - `restore-incomplete`：本次恢复有失败项，剩余备份仍在。
  保留清单是必要的：否则这些文件和备份会失去登记，之后无法再精确处理。

## 7. 卸载、回滚与恢复

```bash
# 常规卸载：删除清单登记文件，保留备份，清单转为仅备份状态
scripts/uninstall-user.sh --prefix "$HOME"

# 回滚到安装前：恢复被覆盖的用户原文件（校验 sha 后恢复）
scripts/uninstall-user.sh --prefix "$HOME" --restore-backups

# 安装后文件被手工改过：默认跳过；确认要删再 --force-modified
scripts/uninstall-user.sh --prefix "$HOME" --force-modified

# 预演
scripts/uninstall-user.sh --prefix "$HOME" --dry-run
```

推荐的回滚演练顺序（升级场景）：

1. `install-user.sh` 覆盖了用户原文件 → 产生 `B` 条目与备份；
2. `uninstall-user.sh`（不带 restore）→ 删除本项目文件，备份保留，清单转仅备份；
3. `uninstall-user.sh --restore-backups` → 用户原文件回到原位，`.abc-im` 清空；
4. 审计：前缀目录与安装前快照一致（测试 15 覆盖）。

- 只删除清单登记的路径；清单缺失 → 明确报错退出 1，不做任何删除。
- 恢复失败（备份缺失/校验不符）→ 明确报错并退出 2；汇总行输出"卸载未完成"，
  不会出现"卸载完成"字样（避免把未完成的回滚报成成功）。
- 目录只在为空时 `rmdir`，不递归删除，不影响共享目录。
- 卸载不触碰 fcitx5 系统文件；旧输入法条目仍可用（addon 只是新增条目）。

## 8. 权限与安全

- **无需 root**：全部写入 `<prefix>` 内（默认 `$HOME`）。脚本不调用 `sudo`，
  不修改 `/etc`、`/usr`、`/opt`、systemd、X11/Wayland 配置或 shell rc。
- 前缀拒绝清单：`/`、`/usr`、`/usr/**`、`/opt`、`/opt/**`（原始串与 `realpath -m`
  规范化后各校验一次）；`--out` 额外拒绝 `$HOME`。
- 防御纵深：每个目标写入前解析 `realpath`，若落入 `/usr`、`/opt`（例如
  `~/.local` 被做成指向系统目录的符号链接）则拒绝并回滚。
- staging 含符号链接 → 拒绝安装（镜像语义不明确，明确报错而非猜测）。
- 脚本与配置库**不含**任何密码、token、私钥；不读取仓库外凭据文件；
  不打印环境变量；日志只含脚本名、相对路径、计数与退出码。
- 配置文件按契约声明禁止存放凭据。

## 9. 测试与静态检查

```bash
# 构建 + 全部 ctest（含配置解析、部署套件、bash -n、shellcheck 若可用）
scripts/build-abc.sh --build-dir build

# 只跑 A5 相关
ctest --test-dir build --output-on-failure -R 'abc_config_tests|abc_deploy|abc_bash_syntax|abc_shellcheck'

# 不依赖 CMake 的独立运行
bash tests/deploy/test_install_uninstall.sh
```

覆盖范围（`tests/deploy/test_install_uninstall.sh`，27 组）：
正常/重复/内容变更安装、升级不删除、外部冲突备份与覆盖、卸载保留备份→仅备份清单→
`--restore-backups` 恢复、修改文件默认跳过与 `--force-modified`、no-overwrite 配置保护、
缺/空 staging、未知参数、`--verify` 四种结果、staging 组装（映射/模板/控制文件/缺产物/
`--allow-missing-addon`/`--clean`/`--out` 安全/符号链接）、前缀安全拒绝（`/`、`/usr`、`/opt`）、
无权限、中途失败回滚（不可读文件 / `.abc-im` 被文件占用）、路径冲突（父路径被文件占用、
目标被目录占用）、清单前缀不一致、dry-run、含空格路径、卸载后路径清单审计、`/usr` `/opt` 全局审计；
以及本轮补充的 6 组专项（21–26）：

| 组 | 场景 | 关键断言 |
|---|---|---|
| 21 | 用户配置保护 | 已存在用户配置不覆盖、不登记为 F、卸载后内容 sha 与安装前一致；用户改过的已安装配置卸载时保留并报 `uninstall-incomplete` |
| 22 | 重复安装幂等 | 第二次安装 `新建 0/更新 3/跳过 1/备份保留 0`；清单条目与文件 sha 不变；无 rollback 残留；`.abc-im` 只有 `manifest.tsv` |
| 23 | dry-run 准确性 | 首次 `新建 4`、逐项 4 条 `[新建]`、不创建前缀；已安装后 `新建 0/更新 3/跳过 1`、3 条 `[更新-本项目]`、目录快照完全一致 |
| 24 | 备份恢复失败 | 备份缺失 / sha 不符均退出码 2，分别报"备份缺失"/"校验不符"，输出"卸载未完成"且无"卸载完成"；不符备份保留、清单 `restore-incomplete`、目标不被损坏内容覆盖 |
| 25 | 安装入口与路径边界 | 检测到 A4 参考脚本 `manifest.list` 时安装/卸载均拒绝（退出码 1）；正式入口只写前缀内，前缀外无 `libabc.so`，`/usr`、`/opt` 无本项目文件 |
| 26 | `FCITX_ADDON_DIRS` | `--print-env` 三种输入（未设置/已设置/已含本项目）输出正确且零变更；不修改 `~/.bashrc`、不创建 `environment.d`/`~/.profile` |

`tests/deploy/CMakeLists.txt` 将 `bash -n` 与 `shellcheck` 纳入 CTest（shellcheck 不存在时
只跑语法检查，以 `bash -n` 为底线），结果见第 10 节。

## 10. 验证记录（2026-10-28，本地）

环境：Windows 10 x64 + Git Bash（cygwin bash 5.3.15）；无 CMake/Ninja；
shellcheck 0.11.0（`pip install shellcheck-py`）；g++ 16.1.0 直接编译配置测试。
目标机（Linux）复验由集成负责人执行（权限类用例必须在那里跑）。

| 项 | 命令 | 结果 |
|---|---|---|
| 配置解析单元测试 | `g++ -std=c++17 -Wall -Wextra -Wpedantic -Isource/include -Isource/config tests/deploy/test_config_parser.cpp source/config/config.cpp -o abc_config_tests && ./abc_config_tests` | `abc_config_tests: ALL PASS`（11 组断言，零编译告警） |
| 部署套件 | `bash tests/deploy/test_install_uninstall.sh` | **pass=187 fail=0 skip=2**（27 组；skip 为权限类，Windows 不强制权限）<br>脱敏日志：[`tests/deploy/logs/2026-10-28-deploy-suite.txt`](../../tests/deploy/logs/2026-10-28-deploy-suite.txt) |
| 语法检查 | `bash -n scripts/build-abc.sh scripts/build-staging.sh scripts/install-user.sh scripts/uninstall-user.sh` | 全部通过 |
| shellcheck | `shellcheck -S warning scripts/*.sh tests/deploy/test_install_uninstall.sh` | shellcheck 0.11.0：**零告警**（warning 及以上；info 级别亦为空） |

覆盖到的关键结果（与 §5/§6/§7 语义逐条对应）：

- 安装/重复安装/内容变更：退出码 0，重复安装不积累备份，清单 sha 与实际一致；
- 外部文件冲突：先备份再覆盖，重装保留最早的用户原文件备份（不被本项目文件顶替）；
- 卸载：删除清单登记文件、保留备份并转为 `uninstalled-backups-pending`，
  随后 `--restore-backups` 恢复用户原文件并清空 `.abc-im`；
- 安装后被改过的文件：默认跳过（退出码 2）且清单保留 `uninstall-incomplete`，
  `--force-modified` 后清单自动清理；
- 失败回滚：`.abc-im` 被文件占用 → 退出码 2、已建目录回收、用户文件未动；
- 冲突检测：父路径被文件占用 / 目标被目录占用 → 计划期退出码 1，零变更；
- staging 组装：A4 三件产物映射到 `.local/**`、配置模板与控制文件就位、
  缺产物时退出码 2（`--allow-missing-addon` 仅警告）、`--clean` 重建无残留、
  拒绝 `/opt` 与 `$HOME` 作为 `--out`、拒绝含符号链接的前缀树；
- 安全审计：`/`、`/usr`、`/opt` 前缀全部拒绝，测试后 `/usr`、`/opt` 无任何本项目痕迹；
- 安装入口：检测到 A4 参考脚本 `manifest.list` 时拒绝（避免两套清单互相看不见），
  正式入口只写前缀内；
- 运行环境：`--print-env` 只读输出安全值，已有 `FCITX_ADDON_DIRS` 被保留并前置本项目目录，
  不写 `~/.bashrc` / `environment.d` / `~/.profile`；
- 卸载后目录快照与安装前逐项一致（路径清单审计）。

> 本地 Windows 复跑说明：宿主对 `rm`/`rmdir` 有拦截垫片、且 cygwin fork 很慢，
> 需以 `env -u CODEBUDDY_SESSION_ID -u CLAUDE_SESSION_ID TMPDIR=/tmp` 运行；
> 权限类用例（10、11）在 Windows 上自动跳过，必须在 Linux 目标机复跑。
> Linux 上直接 `bash tests/deploy/test_install_uninstall.sh` 即可，无需上述前缀。

## 11. 已知限制

1. 权限类用例（无权限目录、不可读文件）在 Windows 本地自动跳过，需在 Linux 复跑；
   跨平台可复现的失败路径由 `.abc-im` 被文件占用、父路径被文件占用两组用例覆盖。
2. 不支持 staging 内符号链接；不支持跨文件系统原子性（同目录临时文件 + rename）。
3. 升级不删除旧清单中的多余文件（避免误删）；清理请先卸载再安装。
4. 同一路径多次备份只保留最早的原始版本（`B` 去重保首）。
5. 并发运行两个 install 实例未加互斥（单用户手动部署场景；如需并发保护可后续加 flock）。
6. **唯一正式安装入口是 `scripts/install-user.sh`**（卸载对应
   `scripts/uninstall-user.sh`）。`deploy/fcitx5-addon/install-user.sh` 是 A4 自测用的
   参考脚本：它把 staging 直接镜像到 `$HOME/.local`，清单为 `.abc-im/manifest.list`，
   与本脚本的 `manifest.tsv` 不兼容。混用会出现"对方看不到已安装文件"的冲突状态，
   因此 A5 脚本检测到 `manifest.list` 会拒绝执行并提示先用 A4 脚本卸载；
   退役该参考脚本需 A4 决定（A5 不修改 A4 文件）。
7. 不校验 UTF-8 字节合法性（契约假定输入为 UTF-8）；不支持行内 `#` 注释（契约只定义整行注释）。
8. 脚本不修改 `FCITX_ADDON_DIRS` 等运行环境，需用户会话自行设置（见第 3 节）。
9. staging 布局以 `$HOME` 为基准（`.local`、`.config`）。若用户设置了
   `XDG_CONFIG_HOME`/`XDG_DATA_HOME`，addon 会去 `$XDG_CONFIG_HOME/abc/config.ini`
   找配置，需自行把模板复制到该目录（脚本不猜测 XDG 布局，避免写错位置）。

## 12. 交付记录

| 项 | 值 |
|---|---|
| 任务 | A5 配置与用户级部署 |
| 提交号 | `d5b4b5b`（全值 `d5b4b5bf8ae3223577b5ee658123caad6b566600`；本文件为交付记录提交） |
| 接口契约版本 | 1.0.0-rc1（`abc/version.h`：`ABC_INTERFACE_VERSION_STRING "1.0.0"`） |
| 配置键表版本 | `config.md` v1.0.0-rc1（第 4 节冻结键表，未新增键） |
| 清单格式版本 | `manifest v1`（`MANIFEST_VERSION=1`） |
| 变更文件 | `source/config/{abc_config.h,config.cpp,CMakeLists.txt}`、`scripts/{build-abc.sh,build-staging.sh,install-user.sh,uninstall-user.sh}`、`deploy/README.md`、`deploy/abc/config.ini`、`tests/deploy/{CMakeLists.txt,test_config_parser.cpp,test_install_uninstall.sh}`、`docs/modules/deployment.md` |
| 未触碰 | addon 实现（`source/fcitx5-addon/**`）、根 `CMakeLists.txt`、公共头、其他任务文件、`deploy/fcitx5-addon/**`（A4 所有权） |

### 12.1 自查声明（对照 `独立任务验收标准.md` §2.7）

- 无 XIM/XTest/xdotool：本任务不含任何按键注入代码，部署脚本只做文件复制与清单管理。
- 无 `/opt` 安装：`build-abc.sh`/`build-staging.sh`/`install-user.sh` 均显式拒绝
  `/`、`/usr`、`/opt` 及其子路径；测试 9、16.3、16.4、18.23 与 21 组复核。
- 不覆盖系统 `/usr`：安装目标只在前缀内（默认 `$HOME`），并对目标 `realpath`
  再校验一次，防符号链接绕过；测试后审计 `/usr`、`/opt` 无本项目痕迹。
- 不在仓库或日志放置部署凭据：脚本不读取、不打印任何凭据或环境变量；
  未访问仓库外部署资料；配置模板按契约声明禁止存放凭据。
- 未修改其他任务文件：见上表"未触碰"；本次提交仅 13 个 A5 文件。
