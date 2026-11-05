#!/usr/bin/env bash
# build-deb-manual.sh — 无 debhelper 环境下的 .deb 组装（CMake install + dpkg-deb）。
#
# 产物与 packaging/debian/control 声明一致：纯运行时文件，无维护脚本。
# 用法: build-deb-manual.sh <源码tar.gz> <输出目录>
set -eu

TARBALL="$1"
OUT="${2:-.}"
NAME="fcitx5-zhipin"
VERSION="1.0.0"
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) ARCH=amd64 ;;
  aarch64) ARCH=arm64 ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# 源码解包。
mkdir -p "$WORK/src"
tar xzf "$TARBALL" -C "$WORK/src" --strip-components=1

mkdir -p "$WORK/pkg/DEBIAN"
cat > "$WORK/pkg/DEBIAN/control" <<EOF
Package: $NAME
Version: $VERSION-1
Architecture: $ARCH
Maintainer: abc-im maintainers <dev@example.com>
Depends: fcitx5, librime1 | librime, rime-data
Recommends: rime-ice-data | rime-ice
Description: Intelligent ABC style input method engine for fcitx5
 An fcitx5 input method engine that reproduces the classic Windows
 "Intelligent ABC" typing flow on top of an embedded librime session.
Section: utils
Priority: optional
EOF

# 构建 + 安装到包根。
cmake -S "$WORK/src" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr 2>&1 | tail -2
cmake --build "$WORK/build" 2>&1 | tail -2
DESTDIR="$WORK/pkg" cmake --install "$WORK/build" 2>&1 | tail -3

mkdir -p "$WORK/pkg/usr/share/doc/$NAME"
cp "$WORK/src/README.md" "$WORK/pkg/usr/share/doc/$NAME/"
cp "$WORK/src/LICENSE" "$WORK/pkg/usr/share/doc/$NAME/"

cd "$WORK"
# dpkg-deb 要求 control 在包根的 DEBIAN/；用 --root-owner-group 统一属主。
dpkg-deb --root-owner-group --build "$WORK/pkg" \
  "$OUT/${NAME}_${VERSION}-1_${ARCH}.deb"
echo "built: $OUT/${NAME}_${VERSION}-1_${ARCH}.deb"
