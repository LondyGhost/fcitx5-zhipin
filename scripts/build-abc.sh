#!/usr/bin/env bash
# build-abc.sh — Linux 智能 ABC 构建脚本（A5 拥有 scripts/build-*.sh）。
#
# 仅负责 configure/build/test/安装暂存（staging），不做任何真实安装；
# 用户级安装一律交给 scripts/install-user.sh。
# 安全约定：不含任何凭据；不输出敏感环境变量；不写 /usr、/opt。
#
# 退出码: 0 成功；1 参数/环境错误；2 configure/build/test/staging 失败。
set -u -o pipefail

readonly PROG="build-abc.sh"

err() { printf '%s: error: %s\n' "$PROG" "$*" >&2; }
log() { printf '%s: %s\n' "$PROG" "$*"; }

usage() {
  cat <<'USAGE'
用法: build-abc.sh [--source-dir DIR] [--build-dir DIR] [--build-type T]
                   [--prefix PREFIX] [--generator G] [--staging DIR]
                   [--staging-allow-missing-addon]
                   [--no-tests] [--clean] [-- cmake 参数...]

  --source-dir   源码目录（默认：脚本所在目录的上一级）
  --build-dir    构建目录（默认: <source-dir>/build）
  --build-type   CMAKE_BUILD_TYPE（默认 RelWithDebInfo）
  --prefix       仅写入 CMake 缓存作记录；真实安装前缀由
                 install-user.sh --prefix 决定（默认 $HOME）
  --generator    Ninja（默认，缺失时回退 Unix Makefiles）
  --staging DIR  构建后组装用户级 staging 树（HOME 相对路径），供
                 install-user.sh --staging 使用。步骤：
                   1) cmake --install --prefix <临时前缀>
                   2) build-staging.sh 把前缀树映射为 <DIR>/.local/** 并补配置模板
                 每次组装都会清空 DIR（构建产物，不是用户数据）。
  --staging-allow-missing-addon
                 无 fcitx5 开发包时允许只组装配置（仅开发/CI；不可用于部署）
  --no-tests     不运行 CTest
  --clean        先删除构建目录
  -- 之后的参数原样追加给 cmake configure
USAGE
}

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
SOURCE_DIR="$(cd -- "$SELF_DIR/.." && pwd -P)"
BUILD_DIR=""
BUILD_TYPE="RelWithDebInfo"
PREFIX="$HOME/.local"
GENERATOR=""
STAGING=""
STAGING_ALLOW_MISSING_ADDON=0
RUN_TESTS=1
CLEAN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --source-dir) [ $# -ge 2 ] || { err "--source-dir 需要参数"; exit 1; }
      SOURCE_DIR=$2; shift 2 ;;
    --build-dir) [ $# -ge 2 ] || { err "--build-dir 需要参数"; exit 1; }
      BUILD_DIR=$2; shift 2 ;;
    --build-type) [ $# -ge 2 ] || { err "--build-type 需要参数"; exit 1; }
      BUILD_TYPE=$2; shift 2 ;;
    --prefix) [ $# -ge 2 ] || { err "--prefix 需要参数"; exit 1; }
      PREFIX=$2; shift 2 ;;
    --generator) [ $# -ge 2 ] || { err "--generator 需要参数"; exit 1; }
      GENERATOR=$2; shift 2 ;;
    --staging) [ $# -ge 2 ] || { err "--staging 需要参数"; exit 1; }
      STAGING=$2; shift 2 ;;
    --staging-allow-missing-addon) STAGING_ALLOW_MISSING_ADDON=1; shift ;;
    --no-tests) RUN_TESTS=0; shift ;;
    --clean) CLEAN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    --) shift; break ;;
    *) err "未知参数: $1"; usage >&2; exit 1 ;;
  esac
done

[ -f "$SOURCE_DIR/CMakeLists.txt" ] || { err "源码目录缺少 CMakeLists.txt: $SOURCE_DIR"; exit 1; }
[ -n "$BUILD_DIR" ] || BUILD_DIR="$SOURCE_DIR/build"
# --clean 会删除构建目录，先挡住明显危险的目标
case "$BUILD_DIR" in
  /|/usr|/usr/*|/opt|/opt/*) err "禁止把 --build-dir 设为 /、/usr 或 /opt"; exit 1 ;;
esac
if [ "$CLEAN" -eq 1 ] && [ -n "${HOME:-}" ] && [ "$BUILD_DIR" = "$HOME" ]; then
  err "禁止把 --build-dir 设为 \$HOME（--clean 会删除该目录）"; exit 1
fi
case "$BUILD_TYPE" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) err "--build-type 仅支持 Debug/Release/RelWithDebInfo/MinSizeRel"; exit 1 ;;
esac
case "$PREFIX" in
  /|/usr|/usr/*|/opt|/opt/*) err "禁止把 --prefix 设为 /、/usr 或 /opt（项目安全边界）"; exit 1 ;;
esac

if [ -z "$GENERATOR" ]; then
  if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"
  elif command -v make >/dev/null 2>&1; then GENERATOR="Unix Makefiles"
  else err "未找到 ninja 或 make"; exit 1
  fi
fi

command -v cmake >/dev/null 2>&1 || { err "未找到 cmake"; exit 1; }

if [ "$CLEAN" -eq 1 ] && [ -e "$BUILD_DIR" ]; then
  log "删除旧构建目录"
  rm -rf -- "$BUILD_DIR" || { err "无法删除旧构建目录"; exit 2; }
fi

log "configure: generator=$GENERATOR build-type=$BUILD_TYPE"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  "$@" || { err "configure 失败"; exit 2; }

log "build"
cmake --build "$BUILD_DIR" || { err "build 失败"; exit 2; }

if [ "$RUN_TESTS" -eq 1 ]; then
  log "test"
  ctest --test-dir "$BUILD_DIR" --output-on-failure || { err "ctest 失败"; exit 2; }
fi

if [ -n "$STAGING" ]; then
  case "$STAGING" in
    /|/usr|/usr/*|/opt|/opt/*) err "禁止把 --staging 设为 /、/usr 或 /opt"; exit 1 ;;
  esac
  [ -f "$SELF_DIR/build-staging.sh" ] || { err "缺少 scripts/build-staging.sh"; exit 2; }
  install_prefix="$(mktemp -d "${TMPDIR:-/tmp}/abc-cmake-prefix.XXXXXX")" || {
    err "无法创建临时安装前缀"; exit 2; }
  # shellcheck disable=SC2064  # 有意在 trap 时展开（变量在脚本生命周期内不变）
  trap 'rm -rf -- "$install_prefix"' EXIT INT TERM
  log "install(临时前缀): $install_prefix"
  cmake --install "$BUILD_DIR" --prefix "$install_prefix" || {
    err "cmake --install 失败（无 install 规则或安装出错）"; exit 2; }

  staging_args=(--from "$install_prefix" --out "$STAGING" --clean)
  [ "$STAGING_ALLOW_MISSING_ADDON" -eq 1 ] && staging_args+=(--allow-missing-addon)
  log "assemble(staging): $STAGING"
  bash "$SELF_DIR/build-staging.sh" "${staging_args[@]}" || {
    err "staging 组装失败"; exit 2; }
  log "staging 完成（相对路径树，可直接交给 install-user.sh --staging）"
fi

log "完成"
exit 0
