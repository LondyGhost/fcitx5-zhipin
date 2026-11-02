#!/bin/sh
# install-user.sh — 把 abc fcitx5 addon 安装到用户目录或项目隔离前缀。
#
# 只镜像 staging 目录里的相对路径。拒绝安装到 /、/usr、/opt 及其子目录。
# 覆盖已有文件前先备份到 <prefix>/.abc-im/backup/<时间戳>/，
# 安装清单写入 <prefix>/.abc-im/manifest.list，供 uninstall-user.sh 使用。
#
# 用法：
#   install-user.sh --staging <dir> [--prefix <dir>] [--dry-run]
# 默认 prefix 是 $HOME/.local。
set -eu

prefix="${HOME:-}/.local"
staging=""
dry_run=0

die() {
  echo "abc-install: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix)
      prefix="${2:-}"
      shift 2
      ;;
    --staging)
      staging="${2:-}"
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[ -n "$prefix" ] || die "prefix is empty; pass --prefix or set HOME"
[ -n "$staging" ] || die "missing --staging <dir>"
[ -d "$staging" ] || die "staging directory does not exist: $staging"

case "/$prefix/" in
  */../*) die "prefix must not contain a parent-directory component: $prefix" ;;
esac

# 解析已有父目录的物理路径，再接回尚不存在的后缀，防止经符号链接
# 创建 /usr 或 /opt 下的新目录时绕过路径拒绝。
probe=$prefix
suffix=""
while [ ! -e "$probe" ]; do
  parent=${probe%/*}
  component=${probe##*/}
  [ "$parent" != "$probe" ] || parent=.
  [ -n "$component" ] || die "cannot resolve prefix: $prefix"
  suffix="/$component$suffix"
  probe=$parent
done
normalized=$(CDPATH= cd -P -- "$probe" && pwd) || die "cannot resolve prefix: $prefix"
normalized=${normalized%/}$suffix
normalized=${normalized%/}
case "$normalized" in
  ""|/|/usr|/usr/*|/opt|/opt/*)
    die "refusing to install into system path: $normalized"
    ;;
esac

meta="$normalized/.abc-im"
stamp=$(date +%Y%m%d%H%M%S)
backup="$meta/backup/$stamp"
manifest="$meta/manifest.list"

if [ "$dry_run" -eq 1 ]; then
  echo "dry-run: prefix=$normalized"
fi

list_file="$normalized/.abc-install.list"
source_file="$normalized/.abc-install.sources"
mkdir -p "$normalized" "$meta"
: >"$list_file"
: >"$source_file"

find "$staging" -type f >"$source_file"
while IFS= read -r source; do
  [ -n "$source" ] || continue
  rel=${source#"$staging"/}
  case "$rel" in
    /*|../*|*/../*)
      echo "abc-install: staging entry escapes the staging directory: $rel" >&2
      exit 1
      ;;
  esac
  target="$normalized/$rel"
  if [ "$dry_run" -eq 1 ]; then
    echo "install $rel -> $target"
    continue
  fi
  mkdir -p "$(dirname -- "$target")"
  if [ -e "$target" ]; then
    mkdir -p "$(dirname -- "$backup/$rel")"
    cp -p -- "$target" "$backup/$rel"
  fi
  cp -p -- "$source" "$target"
  printf '%s\n' "$rel" >>"$list_file"
  echo "installed $rel"
done <"$source_file"

if [ "$dry_run" -eq 0 ]; then
  if [ ! -s "$list_file" ]; then
    echo "abc-install: staging produced no files" >&2
    exit 1
  fi
  cp "$list_file" "$manifest"
  echo "manifest: $manifest"
fi
rm -f "$source_file" "$list_file" 2>/dev/null || :
