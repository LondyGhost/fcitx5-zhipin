# Deploy

用户级安装模板、addon 元数据和安装清单说明。禁止放置凭据；不得覆盖系统 `/usr` 文件或安装到 `/opt`。

## 目录内容

| 路径 | 拥有者 | 说明 |
|---|---|---|
| `abc/config.ini` | A5 | 用户配置模板（键表符合 [`../docs/interfaces/config.md`](../docs/interfaces/config.md)），安装到 `~/.config/abc/config.ini`，**存在即跳过**，绝不覆盖用户配置 |
| `fcitx5-addon/install-manifest.md` | A4 | addon 安装布局（**已确认**）：`lib/fcitx5/abc.so`、`share/fcitx5/addon/abc.conf`、`share/fcitx5/inputmethod/abc.conf`（相对 `$HOME/.local`） |
| `fcitx5-addon/install-user.sh`、`fcitx5-addon/uninstall-user.sh` | A4 | A4 自测用的独立参考脚本（直接复制三个 addon 文件，清单为 `.abc-im/manifest.list`）。**不是部署入口** |

## 正式部署入口（A5）

```bash
# 1) 构建 + 组装 staging（HOME 相对路径树）
scripts/build-abc.sh --build-dir build --staging build/staging

# 2) 安装到用户目录（默认 --prefix $HOME）
scripts/install-user.sh --staging build/staging

# 3) 校验 / 卸载 / 回滚
scripts/install-user.sh --verify
scripts/uninstall-user.sh                 # 删除清单登记文件，保留备份
scripts/uninstall-user.sh --restore-backups  # 并恢复安装前的用户原文件
```

- 安装路径清单、清单格式、备份与回滚语义见
  [`../docs/modules/deployment.md`](../docs/modules/deployment.md)。
- `deploy/fcitx5-addon/*.sh` 与 `scripts/*.sh` **不要混用**：两者清单文件名与备份布局不同，
  混用会导致对方无法识别已安装状态。若曾用前者安装，请先用其 `uninstall-user.sh` 卸载。
- 脚本不写 `/opt`、不覆盖系统 `/usr`；前缀只能是用户目录或项目隔离前缀。
- 运行期需让 fcitx5 找到用户级 addon 库（由用户会话设置，脚本不改任何 shell 配置）：

  ```sh
  export FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/fcitx5"
  ```
