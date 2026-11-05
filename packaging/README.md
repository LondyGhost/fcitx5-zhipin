# packaging/ — 发行版打包

三种 Linux 安装包共用同一套 CMake install 规则（`source/fcitx5-addon/CMakeLists.txt`）。

## 安装内容

| 路径 | 来源 |
|---|---|
| `/usr/lib/fcitx5/libabc.so` | addon 共享库（静态链接 abc 核心 + librime） |
| `/usr/share/fcitx5/addon/abc.conf` | fcitx5 addon 注册 |
| `/usr/share/fcitx5/inputmethod/abc.conf` | 输入法条目 |
| `/usr/share/rime-data/abc.schema.yaml` | Rime 方案（基于 rime-ice） |
| `/usr/share/fcitx5/themes/abc/theme.conf` | classicui 主题（经典 ABC 风格） |

系统级安装不需要 `FCITX_ADDON_DIRS`（`/usr/lib/fcitx5` 在默认搜索路径）。
用户目录的 `~/.local/share/fcitx5/rime/` 里若有同名 `abc.schema.yaml` 会覆盖系统版。

## 依赖

- 硬依赖：fcitx5、librime、rime-data
- 推荐：[rime-ice](https://github.com/iDvel/rime-ice)（abc schema 的词库底座，
  需放进 Rime 用户目录；无它时只有基础字词）

## 构建方法（在仓库根目录）

```sh
# 源码 tarball（git archive 干净导出）
packaging/package.sh tarball

# Arch (.pkg.tar.zst)
packaging/package.sh arch

# Debian (.deb，需 debhelper >= 13)
packaging/package.sh deb

# RPM (.rpm，需 rpmbuild)
packaging/package.sh rpm
```

## 目录

| 目录 | 内容 |
|---|---|
| `arch/` | `PKGBUILD`（AUR 发布版）+ `.SRCINFO` |
| `archlinuxcn/` | 提交到 archlinuxcn 仓库的 `PKGBUILD` + `lilac.yaml`（见其 README） |
| `debian/` | `control`、`changelog`、`rules` |
| `rpm/` | `fcitx5-zhipin.spec` |
| `rime/` | `abc.schema.yaml`、`abc.theme.conf`（随包安装的运行数据） |

## 安装后

1. 重启 fcitx5（`fcitx5 -r` 或注销重登）。
2. fcitx5 配置工具里把「智拼」加入输入法列表。
3. 外观建议：外观设置 → 主题选「智拼」（可选主题名）。

## 发布到 AUR

`arch/PKGBUILD` 就是 AUR 上发布的那一份，`source` 指向 GitHub tag 源码包
（`codeload.github.com` 直连，避开 `github.com` 首跳被墙）。`.SRCINFO` 是同目录生成的索引。

发版流程：

```sh
# 1. 上游打 tag 并推送（AUR 包靠它拉源码）
git tag -a v1.0.0 -m "v1.0.0" && git push origin v1.0.0

# 2. 算 tag 源码包的校验和，填进 PKGBUILD
curl -sL -o /tmp/src.tar.gz \
  "https://codeload.github.com/LondyGhost/fcitx5-zhipin/tar.gz/refs/tags/v1.0.0"
sha256sum /tmp/src.tar.gz

# 3. 重新生成 .SRCINFO（每次改 PKGBUILD 都要做，否则 yay 报错）
makepkg --printsrcinfo > .SRCINFO

# 4. 本地验证
namcap PKGBUILD
makepkg -f && namcap fcitx5-zhipin-*.pkg.tar.zst

# 5. 推 AUR（首次需要 AUR 账号 + 上传 SSH 公钥）
git clone ssh://aur@aur.archlinux.org/fcitx5-zhipin.git /tmp/aur-push
cp PKGBUILD .SRCINFO /tmp/aur-push/
cd /tmp/aur-push && git add -A && git commit -m "upgpkg: fcitx5-zhipin 1.0.0-1" && git push
```

AUR 仓库里只放 `PKGBUILD`、`.SRCINFO` 和补丁，**不要**提交源码 tarball。
用户侧安装：`yay -S fcitx5-zhipin`。

### 版本升级

改 `pkgver` / `pkgrel` → 重算 `sha256sums` → 重新 `makepkg --printsrcinfo > .SRCINFO`
→ 提交信息用 `upgpkg: fcitx5-zhipin <ver>-<rel>`。
