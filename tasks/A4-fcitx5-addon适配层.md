# A4 — fcitx5 addon 适配层

> 本任务完成后由统一集成验收负责人复核。请先阅读整体方案 fcitx5/Wayland 部分、A0 addon 契约、目标机版本和独立任务验收标准。

## 目标与输入

实现 fcitx5 原生输入法引擎 addon 薄适配层。输入为 A0 冻结的 addon 接口契约、目标机 fcitx5 API/ABI 信息以及 A1 核心接口。

## 输出与文件所有权

- `source/fcitx5-addon/**`
- `tests/fcitx5-addon/**`
- `docs/modules/fcitx5-addon.md`
- `deploy/fcitx5-addon/**`

不得修改 core、dictionary、公共头文件、根构建文件或其他任务文件。

## 接口边界

将 fcitx5 KeyEvent 转换为 `KeyCommand`，调用 session，并把结果映射为 fcitx5 preedit、candidate list、commit 和清理操作。不得实现输入缓冲、词库解析或候选排序。

## 硬性要求

- 使用 fcitx5 原生 addon/API；Wayland 通路由 fcitx5 负责。
- 禁止 XIM、XTest、xdotool 和任何模拟按键注入。
- 按键消费语义正确；焦点、停用、输入上下文销毁时生命周期安全。
- 不创建第二份缓冲状态；API/ABI 版本差异局限在适配层。
- 安装文件只进入用户级目录或项目隔离前缀。

## 验收标准

- 目标版本编译并可被 fcitx5 发现/注册。
- 自动化或集成测试覆盖 key 转换、preedit 更新、commit、清理和未消费按键。
- 在目标机真实 Wayland 桌面中的最终端到端用例由 A6 执行；单独 X11 测试不构成通过。
- 安装清单证明不写 `/usr` 和 `/opt`；由统一验收负责人复跑验收标准。

## 依赖与交付

A0 后可建立适配框架，与 A1–A3、A5 并行；端到端联调需核心可用。交付版本信息、构建/测试命令和结果、addon 注册步骤、安装清单及已知限制。
