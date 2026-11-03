#!/usr/bin/env bash
# package.sh — 在源码树旁生成发布 tarball，并调用目标发行版的打包工具。
#
# 用法（在仓库根目录）:
#   packaging/package.sh arch    # makepkg → .pkg.tar.zst
#   packaging/package.sh deb     # dpkg-buildpackage → .deb（需 debhelper）
#   packaging/package.sh rpm     # rpmbuild → .rpm
#   packaging/package.sh tarball # 仅生成源码 tar.gz
#
# 流程：git archive 导出干净源码（排除凭据文件）→ 打包。
set -eu

PROG=$(basename "$0")
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# 项目版本取 project( ... VERSION x.y.z )；cmake_minimum_required 的 3.16 必须避开。
VERSION=$(sed -n 's/^\s*VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "$PROG: cannot read project version" >&2; exit 1; }
NAME="fcitx5-abc"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "$PROG: version $VERSION"

# 1) 干净源码导出（git archive 自动遵守 .gitignore / 归档排除）。
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$ROOT" archive --format=tar.gz \
    --prefix="$NAME-$VERSION/" -o "$WORK/$NAME-$VERSION.tar.gz" HEAD
else
  tar czf "$WORK/$NAME-$VERSION.tar.gz" --transform "s,^,$NAME-$VERSION/," .
fi
echo "$PROG: source tarball: $WORK/$NAME-$VERSION.tar.gz"

case "${1:-}" in
  tarball)
    cp "$WORK/$NAME-$VERSION.tar.gz" .
    echo "$PROG: kept ./$NAME-$VERSION.tar.gz"
    ;;
  arch)
    BUILDROOT="$WORK/arch"
    mkdir -p "$BUILDROOT"
    cp "$WORK/$NAME-$VERSION.tar.gz" "$BUILDROOT/$NAME-$VERSION.tar.gz"
    cp "$ROOT/packaging/arch/PKGBUILD" "$BUILDROOT/"
    # 本地构建：source 指向刚导出的 tarball。
    sed -i "s|source=(\".*\")|source=(\"$NAME-$VERSION.tar.gz\")|" \
      "$BUILDROOT/PKGBUILD"
    (cd "$BUILDROOT" && updpkgsums >/dev/null 2>&1 || true; makepkg -f)
    cp "$BUILDROOT"/*.pkg.tar.* .
    ;;
  deb)
    BUILDROOT="$WORK/deb/$NAME-$VERSION"
    mkdir -p "$(dirname "$BUILDROOT")"
    tar xzf "$WORK/$NAME-$VERSION.tar.gz" -C "$WORK/deb"
    cp -r "$ROOT/packaging/debian" "$BUILDROOT/debian"
    (cd "$BUILDROOT" && dpkg-buildpackage -us -uc -b)
    cp "$WORK/deb/${NAME}_"*.deb .
    ;;
  rpm)
    RPMBUILD="$WORK/rpmbuild"
    mkdir -p "$RPMBUILD"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    cp "$WORK/$NAME-$VERSION.tar.gz" "$RPMBUILD/SOURCES/"
    rpmbuild -ba --define "_topdir $RPMBUILD" \
      --define "_sourcedir $RPMBUILD/SOURCES" \
      "$ROOT/packaging/rpm/fcitx5-abc.spec"
    find "$RPMBUILD/RPMS" -name "*.rpm" -exec cp {} . \;
    ;;
  *)
    echo "usage: $PROG {arch|deb|rpm|tarball}" >&2
    exit 1
    ;;
esac

echo "$PROG: done"
