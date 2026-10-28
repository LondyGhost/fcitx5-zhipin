# A0 交付记录（delivery-A0.md）

按 `docs/独立任务验收标准.md` 第 2 节门槛编制，供统一集成验收负责人复核。

## 1. 任务标识

| 项 | 值 |
|---|---|
| 任务编号 | A0 — 接口契约与构建骨架 |
| 负责人 | A0 代理（本任务书指定 A0 先行，由集成负责人角色承担） |
| 分支 | `master` |
| **A0 提交号** | **`f314103`**（全值 `f3141031cb66043744cf1a66b252687cdf44293b`） |
| 基线提交号 | `d585dd4`（项目既有文档入库，A0 差异的对照基线） |
| 接口契约版本 | **1.0.0-rc1**（`abc/version.h`；待 A1–A5 确认后冻结） |
| 索引格式版本 | **1.0**（`abc/index_format.h`） |
| 交付记录提交号 | 见 git log 中本文件的提交（由集成负责人对照 `f314103` 复核） |

## 2. 变更文件清单（相对基线 `d585dd4`，共 26 个文件）

契约文档（7）：

- `docs/interfaces/README.md` — 索引、版本与冻结流程、全局约束
- `docs/interfaces/engine.md` — KeyCommand/EngineConfig/EngineResult/EngineSession、冻结按键转移表、生命周期与线程约束、错误策略、调用示例
- `docs/interfaces/dictionary.md` — Candidate/CandidateBatch/Dictionary/加载器、排序/去重/stableId/分页语义、线程约束
- `docs/interfaces/index-format.md` — 索引二进制格式 1.0、版本策略、生产者/加载器义务
- `docs/interfaces/config.md` — 配置边界、冻结键表、文件格式与解析规则
- `docs/interfaces/build.md` — 构建骨架规则、依赖方向、目标命名、标准命令
- `docs/interfaces/confirmations.md` — A1–A5 确认记录（当前状态：**待确认**）

公共头文件（5）：

- `source/include/abc/version.h`、`key.h`、`dictionary.h`、`index_format.h`、`engine.h`

构建骨架（6）：

- `CMakeLists.txt`（根）
- `source/{core,dict-importer,dictionary,fcitx5-addon,config}/CMakeLists.txt`（空模块占位清单，注明拥有者）

冒烟测试（2）：

- `tests/smoke/CMakeLists.txt`、`tests/smoke/test_interfaces.cpp`

占位与文档（6）：

- `tests/{core,dict-importer,dictionary,fcitx5-addon,deploy}/.gitkeep`
- `docs/目标机环境.md`（实测信息更新）

**所有权自查**：以上全部位于任务书规定的 A0 文件范围（`docs/interfaces/**`、
`docs/目标机环境.md`、根 `CMakeLists.txt`、`source/include/abc/**`、接口冒烟测试
与空模块构建清单）。A0 未创建/修改 A1–A5 模块实现文件。
工作树中存在其他任务的**未跟踪原型文件**（`source/core/*.hpp,*.cpp`、
`tests/core/*`），不属于本交付，未纳入提交；其中 `source/dictionary/` 的原型
文件在交付期间被其作者移除时连带删除了 A0 占位清单，A0 已重建该占位文件
（内容与提交 `f314103` 一致）。见第 7 节待决事项第 5 条。

## 3. 可复现构建与测试（干净 Linux 环境）

环境（目标机实测，详见 `docs/目标机环境.md`）：
CatOS（Arch 系滚动）x86_64，内核 7.2.11-arch1-1，GCC/G++ 16.2.1，CMake 4.4.4，
Ninja 1.13.2。

两级验证，均通过：

**a) 提交归档级（验收推荐复现方式）** — 以 `git archive` 导出提交 `f314103`
（47 个文件，tar.gz sha256 `b07b2012d7c3a13a08a3f5f1f0aa7f19afe87170f87cce408d6d1b1ba8fd2bf8`），
上传至目标机全新目录 `/home/londy/abc-a0-verify-f314103-082431/`（构建目录已保留备查）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # exit 0
cmake --build build                                              # exit 0，无告警
ctest --test-dir build --output-on-failure                       # 100% tests passed
```

**b) 工作树级** — 同工具链，目录 `/home/londy/abc-a0-smoke-20261028-081853/`，
configure/build/test 全部退出码 0（失败的首轮目录已清理）。

冒烟测试覆盖（`tests/smoke/test_interfaces.cpp`）：

1. 接口/索引版本常量与魔数；
2. 公共类型默认值与值语义（KeyCommand/Candidate/EngineConfig/EngineResult）；
3. `computeStableId` 已知向量（含空串、UTF-8 中文向量）；
4. 契约排序与分页语义可用测试替身实现（精确编码组优先、code 升序、weight 降序、
   limit=0 只计数、offset 越界、空前缀拒绝）；
5. **头文件依赖方向守卫**：扫描 `source/include/**` 的 include 指令，仅允许
   `<标准库>` 与 `"abc/..."`，出现 fcitx/x11/wayland 即失败（纯核心不依赖
   fcitx5 的架构约束由此测试强制）。

注：冒烟测试有意不引用 `createEngineSession`/`loadDictionaryFromFile`（A1/A3
未交付）；实现期由相应任务的 CMake 目标提供符号。

## 4. 模块说明（交付物职责摘要）

- **接口契约文档** = A1–A5 的唯一公共接口来源；头文件与文档一一对应，字段语义、
  生命周期、线程约束、错误路径、调用示例齐备（各文件内详列）。
- **公共头文件**：纯核心（仅标准库）；`EngineSession` 为输入缓冲唯一所有者；
  `Dictionary` 只读；错误经 `EngineError`/`DictionaryStatus` 返回值表达，
  无异常跨界、无全局状态。
- **根构建骨架**：模块在自己的目录提供 `CMakeLists.txt` 即自动纳入（存在性检查），
  A1–A5 无需编辑根清单；新增模块须集成负责人登记（`build.md` 第 2 节）。
- **下游集成方式**：A1 链接 `abc_interfaces` 实现 `createEngineSession`；
  A2 按 `index-format.md` 写索引；A3 实现 `loadDictionaryFromFile`/`Dictionary`；
  A4 按 `engine.md` 2.1 转换表与 4.4 effects 顺序适配 fcitx5，链 `abc_core`；
  A5 按 `config.md` 解析配置产出 `EngineConfig`。

## 5. 依赖 / 许可证 / 测试数据

- 第三方依赖：**无**（核心与冒烟测试仅 C++17 标准库；未引入任何外部库）。
- 许可证：仓库级许可尚未确定（见待决事项第 1 条）；本交付不含第三方代码。
- 测试数据：全部为本任务自造的最小向量（stableId 的 FNV-1a 64 值、4 条虚构候选），
  无真实词库、无用户输入、无未授权数据。

## 6. 禁止事项自查声明

- [x] 无 XIM/XTest/xdotool 引用；接口文档明确禁止（`README.md` 全局约束 7）。
- [x] 无 `/opt` 安装；构建仅在本机目录，未运行安装步骤。
- [x] 未覆盖系统 `/usr`；目标机操作全部只读采集 + `$HOME` 下新建目录。
- [x] 仓库、日志、提交中无部署凭据（凭据仅存于被 gitignore 的本机文件；
      提交前已检查 `git status`/`git diff --cached`）。
- [x] 日志无真实用户输入（冒烟测试输出仅含虚构候选）。

## 7. 待决事项

1. **仓库许可证未定**：需要在项目层面确定（影响 A2 对 rime-ice 样本的引用说明）。
2. **A1–A5 确认未完成**：`confirmations.md` 当前全部“待确认”；冻结前各任务只能
   做私有原型。其中已观察到某下游任务的未跟踪原型文件与本骨架共存于同一工作树
   （见第 2 节），请在确认环节一并对齐工作方式。
3. **原文大小写保留**：v1 冻结为提交规范化小写缓冲（`engine.md` 4.2 已知限制），
   若下游要求保留原始大小写需走 minor 变更。
4. **同码同文多来源**：去重保留权重最高者，其余进导入报告重复数；
   若 A2 实测 rime-ice 存在大量跨文件重复且需要来源合并策略，属 minor 演进。
5. **并行工作树协调**：交付期间有下游任务在共享工作树写原型并删除过 A0 占位文件
   （已重建）。建议集成负责人重申：原型请放各自任务空间，公共树以提交为准。
6. **性能承诺**：`dictionary.md` 第 6 节为 v1 基线要求（A3 交付时实测报告），
   mmap/压缩等由 A3 决定，不改格式。
7. **纯核心的线程模型**：已按 fcitx5 单事件循环假设冻结（会话单线程、词典只读
   并发安全）。若 A4 实测发现 fcitx5 版本存在多线程派发，须回报集成负责人评审
   （可能需要 minor 变更）。

## 8. 下游确认记录

见 [`confirmations.md`](confirmations.md)。确认完成后由 A0 在该文件标注冻结版本
与日期；确认过程中提出的契约修订将升版本号并记录变更。

## 9. 生成产物说明

- 目标机验证目录（非交付物，供复核）：
  `/home/londy/abc-a0-verify-f314103-082431/`（提交归档构建，推荐）。
- 本地归档：`D:\workbuddy\abc-a0-f314103.tar.gz`（提交 `f314103` 导出，
  sha256 见第 3 节）；上传/构建辅助脚本 `D:\workbuddy\a0_build_smoke.py`、
  `a0_verify_commit.py`（不含凭据，凭据由其引用的既有 `ssh_run.py` 从本地私密
  文件读取，未打印、未入库）。
- 无其他构建产物需要归档至 `artifacts/`（无 addon/词库产物）。
