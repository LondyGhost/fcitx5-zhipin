#!/usr/bin/env bash
# build-staging.sh — 把 CMake 安装前缀树组装成用户级 staging 树（A5）。
#
# 职责单一：把 `cmake --install --prefix DIR` 产出的前缀树（顶层 lib/、share/…）
# 映射到 staging 目录的 <addon-dir>/ 下，并补上 A5 拥有的用户配置模板与控制文件。
# 不做真实安装；安装一律交给 scripts/install-user.sh。
#
# 布局来源（不自行发明路径）：
#   - addon 侧：deploy/fcitx5-addon/install-manifest.md（A4 冻结）+
#     source/fcitx5-addon/CMakeLists.txt 的 install() 规则：
#       lib/fcitx5/abc.so
#       share/fcitx5/addon/abc.conf
#       share/fcitx5/inputmethod/abc.conf
#     以上路径相对于安装前缀，用户级前缀即 $HOME/.local → staging 落点 <addon-dir>。
#   - 配置侧：docs/interfaces/config.md 示例路径 ~/.config/abc/config.ini。
#
# 产物（--out 下，HOME 相对）：
#   <addon-dir>/lib/fcitx5/abc.so
#   <addon-dir>/share/fcitx5/addon/abc.conf
#   <addon-dir>/share/fcitx5/inputmethod/abc.conf
#   .config/abc/config.ini
#   .abc-im-no-overwrite.list        （控制文件，install-user.sh 不安装它）
#
# 安全约定：不含凭据；不输出环境变量；拒绝把 --out 设为 /、/usr、/opt 或 $HOME。
#
# 退出码: 0 成功；1 参数/校验错误（未做变更）；2 组装失败。
set -u -o pipefail
set -f  # 关闭 glob：路径可含空格与通配符字符

readonly PROG="build-staging.sh"

readonly EXIT_USAGE=1
readonly EXIT_FAIL=2

err()  { printf '%s: error: %s\n' "$PROG" "$*" >&2; }
warn() { printf '%s: warning: %s\n' "$PROG" "$*" >&2; }
log()  { printf '%s: %s\n' "$PROG" "$*"; }

usage() {
  cat <<'USAGE'
用法: build-staging.sh --from PREFIX --out DIR [选项]

  --from DIR                已安装的前缀树（通常来自 cmake --install --prefix）
  --out DIR                 staging 输出目录（HOME 相对路径树，交给 install-user.sh）
  --addon-dir NAME          --from 的内容在 staging 中的落点（默认 .local，即 $HOME/.local）
  --config-template FILE    用户配置模板（默认 <项目>/deploy/abc/config.ini）
  --config-rel PATH         模板在 staging 中的相对路径（默认 .config/abc/config.ini）
  --allow-missing-addon     A4 产物缺失时只警告（无 fcitx5 的开发/CI 环境）
  --clean                   先清空 --out（默认非空即报错，避免残留旧文件）
  --dry-run                 只打印计划，不做任何变更
  --help                    显示本帮助

说明:
  - addon 布局取自 A4 的 deploy/fcitx5-addon/install-manifest.md，本脚本不新增路径；
  - 输出可直接用于: install-user.sh --staging DIR --prefix "$HOME"。
USAGE
}

# ---------------------------------------------------------------- 参数解析
FROM=""
OUT=""
ADDON_DIR=".local"
CONFIG_TEMPLATE=""
CONFIG_REL=".config/abc/config.ini"
ALLOW_MISSING_ADDON=0
CLEAN=0
DRY_RUN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --from) [ $# -ge 2 ] || { err "--from 需要参数"; exit "$EXIT_USAGE"; }
      FROM=$2; shift 2 ;;
    --out) [ $# -ge 2 ] || { err "--out 需要参数"; exit "$EXIT_USAGE"; }
      OUT=$2; shift 2 ;;
    --addon-dir) [ $# -ge 2 ] || { err "--addon-dir 需要参数"; exit "$EXIT_USAGE"; }
      ADDON_DIR=$2; shift 2 ;;
    --config-template) [ $# -ge 2 ] || { err "--config-template 需要参数"; exit "$EXIT_USAGE"; }
      CONFIG_TEMPLATE=$2; shift 2 ;;
    --config-rel) [ $# -ge 2 ] || { err "--config-rel 需要参数"; exit "$EXIT_USAGE"; }
      CONFIG_REL=$2; shift 2 ;;
    --allow-missing-addon) ALLOW_MISSING_ADDON=1; shift ;;
    --clean) CLEAN=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) err "未知参数: $1"; usage >&2; exit "$EXIT_USAGE" ;;
  esac
done

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
PROJECT_DIR="$(cd -- "$SELF_DIR/.." && pwd -P)"
[ -n "$CONFIG_TEMPLATE" ] || CONFIG_TEMPLATE="$PROJECT_DIR/deploy/abc/config.ini"

[ -n "$FROM" ] || { err "--from 为空"; usage >&2; exit "$EXIT_USAGE"; }
[ -n "$OUT" ] || { err "--out 为空"; usage >&2; exit "$EXIT_USAGE"; }
[ -d "$FROM" ] || { err "--from 目录不存在: $FROM"; exit "$EXIT_USAGE"; }
FROM="$(cd -- "$FROM" && pwd -P)" || { err "--from 无法规范化: $FROM"; exit "$EXIT_USAGE"; }

case "$ADDON_DIR" in
  ""|/*|*..*|.|-) err "--addon-dir 必须是不含 .. 的相对路径: $ADDON_DIR"; exit "$EXIT_USAGE" ;;
esac
case "$CONFIG_REL" in
  ""|/*|*..*) err "--config-rel 必须是不含 .. 的相对路径: $CONFIG_REL"; exit "$EXIT_USAGE" ;;
esac

for tool in cp find mkdir rm dirname realpath; do
  command -v "$tool" >/dev/null 2>&1 || { err "未找到 $tool"; exit "$EXIT_USAGE"; }
done

# ---------------------------------------------------------------- 路径安全
case "$OUT" in
  /*) ;;
  *) OUT="$PWD/$OUT" ;;
esac
OUT="$(realpath -m -- "$OUT" 2>/dev/null)" || {
  err "--out 无法规范化: $OUT"; exit "$EXIT_USAGE"; }
case "$OUT" in
  /|/usr|/usr/*|/opt|/opt/*)
    err "禁止把 --out 设为 /、/usr 或 /opt（项目安全边界）"; exit "$EXIT_USAGE" ;;
esac
if [ -n "${HOME:-}" ] && [ "$OUT" = "$HOME" ]; then
  err "禁止把 --out 设为 \$HOME（会清空用户目录）"; exit "$EXIT_USAGE"
fi
case "$OUT/" in
  "$FROM"/*) err "--out 不能位于 --from 内部: $OUT"; exit "$EXIT_USAGE" ;;
esac
case "$FROM/" in
  "$OUT"/*) err "--from 不能位于 --out 内部: $FROM"; exit "$EXIT_USAGE" ;;
esac
case "$OUT/" in
  "$PROJECT_DIR"/*) warn "--out 位于项目源码目录内，注意不要提交 staging 产物: $OUT" ;;
esac

# ---------------------------------------------------------------- --from 校验
[ -n "$(find "$FROM" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ] || {
  err "--from 为空目录（没有可组装的安装产物）: $FROM"; exit "$EXIT_FAIL"; }
if [ -n "$(find "$FROM" -type l -print -quit 2>/dev/null)" ]; then
  err "--from 含符号链接，staging 不支持（请展开为真实文件）"; exit "$EXIT_FAIL"
fi

# A4 冻结的 addon 相对路径（deploy/fcitx5-addon/install-manifest.md）
ADDON_REQUIRED="lib/fcitx5/abc.so
share/fcitx5/addon/abc.conf
share/fcitx5/inputmethod/abc.conf"

missing=""
while IFS= read -r rel; do
  [ -n "$rel" ] || continue
  [ -f "$FROM/$rel" ] || missing="$missing $rel"
done <<< "$ADDON_REQUIRED"
if [ -n "$missing" ]; then
  if [ "$ALLOW_MISSING_ADDON" -eq 1 ]; then
    warn "A4 产物缺失（--allow-missing-addon 已启用，仅组装配置）:$missing"
  else
    err "A4 产物缺失:$missing"
    err "原因通常是 fcitx5 开发包缺失导致 addon 被跳过；请安装 Fcitx5Core 后重建，"
    err "或在无 fcitx5 的环境使用 --allow-missing-addon（仅用于开发/CI，不可用于部署）"
    err "若为 Debian 系多架构 libdir（lib/<triplet>/fcitx5），请用 --addon-dir 与 A4 确认布局"
    exit "$EXIT_FAIL"
  fi
fi

[ -f "$CONFIG_TEMPLATE" ] || { err "配置模板不存在: $CONFIG_TEMPLATE"; exit "$EXIT_USAGE"; }

# ---------------------------------------------------------------- 输出目录
if [ -e "$OUT" ] && [ ! -d "$OUT" ]; then
  err "--out 已存在且不是目录: $OUT"; exit "$EXIT_USAGE"
fi
OUT_NONEMPTY=0
if [ -d "$OUT" ] && [ -n "$(find "$OUT" -mindepth 1 -print -quit 2>/dev/null)" ]; then
  OUT_NONEMPTY=1
fi
if [ "$OUT_NONEMPTY" -eq 1 ] && [ "$CLEAN" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
  err "--out 非空（避免残留旧文件）: $OUT"
  err "如需重建请加 --clean（会删除该目录内容）"
  exit "$EXIT_USAGE"
fi

if [ "$DRY_RUN" -eq 1 ]; then
  log "dry-run 计划:"
  log "  --from: $FROM"
  log "  --out : $OUT（$([ "$OUT_NONEMPTY" -eq 1 ] && echo 非空-将被清空 || echo 空)）"
  log "  addon → $ADDON_DIR/（${missing:+缺失:$missing}${missing:-全部就绪}）"
  log "  配置  → $CONFIG_REL"
  log "  控制文件 → .abc-im-no-overwrite.list"
  log "dry-run 结束（未做任何变更）"
  exit 0
fi

if [ "$OUT_NONEMPTY" -eq 1 ]; then
  rm -rf -- "$OUT" || { err "清空 --out 失败: $OUT"; exit "$EXIT_FAIL"; }
fi
mkdir -p -- "$OUT" || { err "创建 --out 失败: $OUT"; exit "$EXIT_FAIL"; }

# ---------------------------------------------------------------- 组装
mkdir -p -- "$OUT/$ADDON_DIR" || { err "创建 addon 落点失败: $ADDON_DIR"; exit "$EXIT_FAIL"; }
cp -R -p -- "$FROM/." "$OUT/$ADDON_DIR/" || {
  err "复制安装产物失败: $FROM → $OUT/$ADDON_DIR"; exit "$EXIT_FAIL"; }

mkdir -p -- "$OUT/$(dirname -- "$CONFIG_REL")" || {
  err "创建配置目录失败: $(dirname -- "$CONFIG_REL")"; exit "$EXIT_FAIL"; }
cp -p -- "$CONFIG_TEMPLATE" "$OUT/$CONFIG_REL" || {
  err "复制配置模板失败: $CONFIG_REL"; exit "$EXIT_FAIL"; }
printf '%s\n' "$CONFIG_REL" > "$OUT/.abc-im-no-overwrite.list" || {
  err "写控制文件失败: .abc-im-no-overwrite.list"; exit "$EXIT_FAIL"; }

# 组装后复核（防止 cp 部分失败造成"看似成功"）
for rel in "$ADDON_DIR/lib/fcitx5/abc.so" \
           "$ADDON_DIR/share/fcitx5/addon/abc.conf" \
           "$ADDON_DIR/share/fcitx5/inputmethod/abc.conf" \
           "$CONFIG_REL"; do
  [ -f "$OUT/$rel" ] || {
    if [ "$ALLOW_MISSING_ADDON" -eq 1 ] && [ "$rel" != "$CONFIG_REL" ]; then
      warn "staging 缺少 A4 产物: $rel"
    else
      err "staging 组装结果缺少: $rel"; exit "$EXIT_FAIL"
    fi
  }
done

n_files="$(find "$OUT" -type f | wc -l | tr -d ' ')"
n_dirs="$(find "$OUT" -mindepth 1 -type d | wc -l | tr -d ' ')"
log "staging 组装完成: $OUT（$n_files 个文件，$n_dirs 个目录）"
log "下一步: scripts/install-user.sh --staging \"$OUT\" --prefix \"\$HOME\""
exit 0
