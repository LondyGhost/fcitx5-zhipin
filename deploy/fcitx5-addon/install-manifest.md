# abc fcitx5 addon 安装路径清单

本清单是 A4 允许写入的全部路径。路径相对于安装前缀；前缀只能是用户目录
（默认 `$HOME/.local`）或项目隔离前缀。`install-user.sh` 拒绝 `/`、`/usr`、`/opt`。

| 相对路径 | 来源 | 作用 |
|---|---|---|
| `lib/fcitx5/abc.so` | `abc_fcitx5_addon` | addon 共享库。`Library=libabc`，fcitx5 加 `lib` 前缀和 `.so` |
| `share/fcitx5/addon/abc.conf` | `abc-addon.conf` | addon 注册描述 |
| `share/fcitx5/inputmethod/abc.conf` | `abc-im.conf` | 输入法条目 |
| `.abc-im/manifest.list` | 安装脚本生成 | 卸载依据，只含上面的相对路径 |
| `.abc-im/backup/<时间戳>/` | 安装脚本生成 | 覆盖前的原文件，卸载时恢复 |

不在本清单中的路径不会被安装或卸载脚本修改。

## 为什么不是默认用户路径就能被发现

目标机 fcitx5 5.1.23 实测：

- addon 描述目录：`$HOME/.local/share/fcitx5` 已在默认搜索路径中。
- addon **库**目录默认只有 `/usr/lib/fcitx5`，`userDirectory(Addon)` 为空。

因此用户级库要被加载，运行 fcitx5 的环境必须带：

```sh
export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
```

`FCITX_ADDON_DIRS` 会替换内置 addon 目录列表（已在目标机用
`StandardPathsType::Addon` 验证）。系统目录保留在后面，旧输入法不受影响。
该变量由用户会话设置，不修改 `/usr` 或 `/opt`。

## 注册与验证

```sh
export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
fcitx5 -r   # 或按发行版的用户级方式重启 fcitx5
fcitx5-remote -n abc   # 切到本引擎；不可用时先 fcitx5-diagnose
```

确认 `fcitx5-diagnose` 的 addon 列表含 `abc`，且加载路径是
`$HOME/.local/lib/fcitx5/abc.so`。Wayland 端到端由 A6 执行。
