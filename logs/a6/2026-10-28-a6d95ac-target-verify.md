# A6 目标机独立复跑日志（轮次 1）

- 日期：2026-10-28
- 验收提交：`a6d95ac`（git archive 导出，sha256 `16244dff684b67bc3cabf447e4fdf40756471f47d18cf0d33c33b9cf4540af92`）
- 目标机：`192.168.1.250:47672`（CatOS x86_64，内核 7.2.11-arch1-1，GCC 16.2.1，
  CMake 4.4.4，Ninja 1.13.2）——环境详情见 `docs/目标机环境.md`
- 执行方式：全新目录 `/home/londy/abc-a6-verify-a6d95ac/`，仅构建与只读审计，
  未安装任何文件
- 脱敏声明：无凭据、无 token、无真实用户输入；主机/用户名口径与
  `docs/目标机环境.md` 一致；执行脚本 `D:\workbuddy\a6_verify_commit.py`
  （凭据机制复用既有 ssh_run.py，脚本与输出均不含凭据）

## 1. 校验与配置

```
sha256sum 归档 = 16244dff…af92        （与本地一致）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
→ GNU 16.2.1 识别，Build files written …/src/build   CONFIGURE_EXIT=0
```

## 2. 构建（Ninja，11 目标）

```
[11/11] Linking CXX executable tests/core/abc_test_core
BUILD_EXIT=0（无告警输出）
产物：source/core/libabc_core.a、tests/smoke/abc_interface_smoke、
      tests/core/abc_test_core
```

## 3. CTest

```
1/2 abc_interface_smoke   Passed   0.00s
2/2 abc_core_tests        Passed   0.00s
100% tests passed out of 2              CTEST_EXIT=0
```

测试二进制明细：
- `abc_interface_smoke: ALL PASS`（18 项检查：版本常量、类型语义、stableId 向量、
  契约排序/分页语义、公共头依赖方向扫描）SMOKE_EXIT=0
- `ran 67 test cases, 0 failed assertions`                    CORE_EXIT=0

## 4. /usr、/opt 审计（只读）

```
find /usr /opt -maxdepth 6 \( -name 'libabc_*' -o -name 'abc_engine*'
  -o -name '*abcdix*' -o -name 'abc-test_*' -o -name 'abc_test_*'
  -o -name 'fcitx5-abc*' \)
→ 无匹配（AUDIT_FIND_EXIT=1，find 因部分目录无权限返回非 0，属预期）
/opt 内容：QQ、ariang、autologin、awesun、card、containerd、drhealth
  （均与本项目无关；属既有系统/用户数据，未触碰）
```

## 5. 用户级目录审计（只读）

```
find ~/.local ~/.config -maxdepth 4 -iname '*abc*'（排除本次验证目录）
→ 命中均为【历史 abcabbr 试验残留】（非本项目 A1–A6 产物）：
    ~/.local/share/fcitx5/rime/abcabbr.schema.yaml
    ~/.local/share/fcitx5/rime/abcabbr.userdb
    ~/.local/share/fcitx5/rime/abcabbr.dict.yaml
    ~/.config/environment.d/90-rime-abcabbr.conf
  其余命中为无关应用缓存中的 'abc' 子串（QQ/opencode），非文件名匹配。
→ 结论：本项目（新引擎）无任何用户级安装残留（与“从未执行安装”一致）；
  旧试验文件在位，回滚演练（UC18）时必须保持其完好。
```

## 6. Wayland 会话与输入法栈（只读）

```
loginctl：session 1 → Name=londy, Desktop=KDE, Type=wayland   （真实 Wayland 在位）
fcitx5 --version → 5.1.23
pacman -Q fcitx5 fcitx5-chinese-addons fcitx5-configtool
→ fcitx5 5.1.23-1 / fcitx5-chinese-addons 5.1.15-2 / fcitx5-configtool 5.1.16-3
  （旧/系统输入法在位，可作回滚后的可用性基线）
```

## 7. 历史证据链

```
/home/londy/abc-a0-verify-f314103-082431/   （A0 归档级验证目录，保留）
/home/londy/abc-a6-verify-a6d95ac/          （本次 A6 验证目录，保留备查）
```

## 8. 未执行项（阻塞，见 docs/验收记录.md）

- A2 索引导入 → A3 加载查询 → A4 addon 的真实链路：模块未交付。
- Wayland 端到端手工用例（tests/manual 清单 UC01–UC20）：前置条件未满足。
- 用户级安装/卸载/回滚演练：A5 安装脚本未交付。
