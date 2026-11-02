# 预编辑不上屏问题：目标机实测证据（2026-10-29 12:30–12:50）

目标机：`192.168.1.250:47672`（londy），CatOS / KDE Wayland / fcitx5 5.1.23。
对应结论：`docs/ABC预编辑不上屏问题审核报告.md` 第 9 节。

## 1. 环境核对

```text
$ pgrep -a fcitx5
1972708 /usr/bin/fcitx5
$ grep -i abc /proc/1972708/maps
/home/londy/.local/lib/fcitx5/libabc.so
$ sha256sum ~/.local/lib/fcitx5/libabc.so
183987b13472339f66b270a570d8d995fee959ad54cadc9a85a395412c8302b5
$ fcitx5-remote ; fcitx5-remote -n
1
keyboard-us
$ grep -n '^ActiveByDefault' ~/.config/fcitx5/config
48:ActiveByDefault=False
```

## 2. 故障现场（未激活状态）

自驱动 GTK 客户端（`tools/diagnostics/gtk_im_selftest.c`）发送 `a`/`b`：

```text
[info] im module=fcitx
[key 'a'] filter_keypress=1 preedit_count=0 commit_count=0
[key 'b'] filter_keypress=1 preedit_count=0 commit_count=0
[summary] preedit_updates=0 commits=0
```

fcitx5 侧 key_trace 显示按键到达，但 addon 无任何 `keyEvent` 日志：

```text
KeyEvent: Key(a states=0) rawKey: Key(a states=0) ... program: gtk_im_selftest
KeyEvent handling time: 0ms result:0
```

## 3. 切到 abc 后（同一进程内枚举输入法）

```text
[preedit #1] "a" cursor=1
[preedit #2] "ab" cursor=2
[commit #1] "ab"
```

```text
ABCTRACE activate entry=abc addon=abc program=gtk_im_selftest
ABCTRACE keyEvent entry=abc addon=abc
ABCTRACE key sym=0x61 states=0x0 release=0 modrel=0 code=1 uni=97 digit=0 consumed=1 mode=2 preedit='a' cands=0 effects=0
ABCTRACE action kind=2 text='a'          # kSetPreedit
ABCTRACE action kind=4 text=''           # kClearCandidates
```

## 4. 改为 ActiveByDefault=True 之后（不做任何切换）

```text
$ fcitx5-remote ; fcitx5-remote -n
2
abc
```

```text
ABCTRACE activate entry=abc addon=abc program=org.gnome.gedit
ABCTRACE activate entry=abc addon=abc program=gtk_im_selftest
```

输入 `ceshi` + 空格：

```text
[preedit #1] "c" ... [preedit #5] "ce shi"
[commit #1] "测试"
```

## 5. 词库现状

```text
$ ls -l ~/.local/share/abc/index.abcdix
416 字节
$ xxd -l 64 ~/.local/share/abc/index.abcdix
4142 4344 4958 3100 0000 0100 0800 0000   ABCDIX1.........
...
00000030: 0500 6365 7368 6906 00e6 b58b e8af 95  ..ceshi..测试
```

索引内仅一条记录 `ceshi → 测试`；输入 `a`/`ab` 时 `candidates=0`，空格按 `commit_raw` 提交原文。

## 6. 使用的诊断手段（可复现）

- 临时插桩构建：在 `abcengine.cpp` 的构造/`keyEvent`/`apply`/`activate`/`deactivate`/`reset`
  加 `FCITX_INFO() << "ABCTRACE ..."`，装到 `~/.local/lib/fcitx5/libabc.so`，跑完已还原原二进制。
- `systemctl --user` 临时 drop-in：`ExecStart=/usr/bin/fcitx5 --verbose default=4,key_trace=5`，跑完已删除。
- 自驱动客户端：合成 `GdkEventKey` → `gtk_im_context_filter_keypress()` → fcitx5-gtk IM 模块
  → fcitx5 D-Bus frontend → addon，无需物理按键即可复现与验收。
