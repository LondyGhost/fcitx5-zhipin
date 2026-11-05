# archlinuxcn 仓库提交材料

`fcitx5-zhipin/` 目录就是 `archlinuxcn/repo` 仓库里要新增的包目录，
原样复制到 `archlinuxcn/fcitx5-zhipin/` 即可。

```
archlinuxcn/fcitx5-zhipin/
├── PKGBUILD      # 从 GitHub tag 源码构建
└── lilac.yaml    # 自动更新配置（nvchecker + lilac）
```

archlinuxcn 仓库**不需要** `.SRCINFO`，只放 `PKGBUILD` 和 `lilac.yaml`（可选 `LICENSE`）。

## 已验证

| 检查项 | 结果 |
|---|---|
| `namcap PKGBUILD` | 无告警 |
| `makepkg -f` 完整构建 | 成功，139 KB，无 debug 包（`options=(!debug)`） |
| 包内文件 | `libabc.so`、`abc.conf` ×2、主题、图标、schema、LICENSE、README |
| `lilac.yaml` 对 `lilac-yaml-schema.yaml` | 通过 |

## 提交步骤

```sh
# 1. fork archlinuxcn/repo（网页上点 Fork）

# 2. 克隆自己的 fork
git clone git@github.com:<你的用户名>/repo.git
cd repo

# 3. 复制包目录
mkdir -p archlinuxcn/fcitx5-zhipin
cp /path/to/packaging/archlinuxcn/fcitx5-zhipin/{PKGBUILD,lilac.yaml} \
   archlinuxcn/fcitx5-zhipin/

# 4. 提交
git add archlinuxcn/fcitx5-zhipin
git commit -m "fcitx5-zhipin: new package"
git push origin master

# 5. 在网页上开 PR 到 archlinuxcn/repo:master
```

PR 会被 `Packaging consistency check` 跑一遍（就是 `./pre-commit --all`，
校验 lilac.yaml schema）。通过后由 archlinuxcn 的维护者 review + 合并，
他们的 bot 会自动构建并推到 `repo.archlinuxcn.org`。

## PR 标题与正文

标题：

```
fcitx5-zhipin: new package
```

正文：

```markdown
模仿 Windows 智能ABC 拼音输入法的输入习惯的 fcitx5 输入法引擎。

- 上游：https://github.com/LondyGhost/fcitx5-zhipin
- 许可：GPL-3.0-only
- 依赖：fcitx5 / librime / rime-prelude / hicolor-icon-theme
- 构建：CMake + Ninja，librime 内嵌会话，不依赖 fcitx5-rime 前端

键位语义（两段空格选词、候选放出后继续打字直接上屏、分阶段逐字选字、
词尽出字、主键盘 -/= 翻页、全角标点）全部由本项目的 C++ 引擎实现。

说明：`abc.schema.yaml` 安装到 `/usr/share/rime-data/`（librime 固定扫描路径），
该目录与 `fcitx5-rime`、`rime-prelude` 共享，不冲突。

上游已提供 AUR 包与 GitHub Releases（deb/rpm/Arch 预编译包）。
```

## 合并后用户怎么装

```sh
# /etc/pacman.conf 加：
# [archlinuxcn]
# Server = https://repo.archlinuxcn.org/$arch

sudo pacman -Sy && sudo pacman -S archlinuxcn-keyring
sudo pacman -S fcitx5-zhipin
```

国内建议换镜像，见 https://github.com/archlinuxcn/mirrorlist-repo 。

## 版本升级

`lilac.yaml` 里配了 `update_on: github ... use_latest_release: true`，
上游打 `v*` tag 并生成 Release 后（GitHub Actions 会自动生成），
bot 会自动 `update_pkgver_and_pkgrel` 并 `git_pkgbuild_commit()` 回仓库，**不需要手动提 PR**。

`pre_build_script` 里的 `.lstrip('v')` 是必需的——上游 tag 带 `v` 前缀，pkgver 不能带。

## 待办

`post_build_script` 目前只有 `git_pkgbuild_commit()`，没有 `update_aur_repo()`。
等 AUR 上的 `fcitx5-zhipin` 包建好之后，可以补上这一行，让 archlinuxcn 的
构建顺带同步 AUR 的 PKGBUILD。
