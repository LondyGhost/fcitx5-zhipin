# dist/ — 发布产物（v1.0.0）

构建时间：2026-11-03（源码树 c271943）
构建环境：目标机 Arch Linux（192.168.1.250）via `packaging/package.sh`

## 文件

| 文件 | 适用发行版 | 安装命令 |
|---|---|---|
| `fcitx5-zhipin_1.0.0-1_amd64.deb` | Debian / Ubuntu | `sudo apt install ./fcitx5-zhipin_*.deb` |
| `fcitx5-zhipin-1.0.0-1.x86_64.rpm` | Fedora / openEuler | `sudo dnf install ./fcitx5-zhipin-*.rpm` |
| `fcitx5-zhipin-1.0.0-1-x86_64.pkg.tar.zst` | Arch / Manjaro | `sudo pacman -U fcitx5-zhipin-*.pkg.tar.zst` |
| `fcitx5-zhipin-1.0.0.tar.gz` | 源码（git archive 干净导出） | 见 README「从源码构建」 |
| `SHA256SUMS` | 全部产物的校验和 | `sha256sum -c SHA256SUMS` |

## 内容一致性

三种安装包的 payload 一致（安装路径见 [packaging/README.md](../packaging/README.md)）：

- `/usr/lib/fcitx5/libabc.so` — addon 库
- `/usr/share/fcitx5/addon/abc.conf` + `inputmethod/abc.conf` — 注册项
- `/usr/share/rime-data/abc.schema.yaml` — Rime 方案
- `/usr/share/fcitx5/themes/abc/theme.conf` — 智能ABC 主题
- README / LICENSE（deb、rpm、Arch 各自按发行版惯例归档）

## 重新构建

```sh
packaging/package.sh arch     # 或 deb / rpm / tarball
```

产物会输出到仓库根目录，确认后移入本目录。
本目录中的安装包与 tarball 不提交 git（见 `.gitignore`），通过 GitHub Release 分发；
仅本 README 与 `SHA256SUMS` 入库，用于核对发布物。
