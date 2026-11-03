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
| `arch/` | `PKGBUILD`（本地源码包构建） |
| `debian/` | `control`、`changelog`、`rules` |
| `rpm/` | `fcitx5-zhipin.spec` |
| `rime/` | `abc.schema.yaml`、`abc.theme.conf`（随包安装的运行数据） |

## 安装后

1. 重启 fcitx5（`fcitx5 -r` 或注销重登）。
2. fcitx5 配置工具里把「智拼」加入输入法列表。
3. 外观建议：外观设置 → 主题选「智能ABC」（可选主题名）。
