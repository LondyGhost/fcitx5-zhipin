#!/usr/bin/env bash
# uninstall-user.sh — Linux 智能 ABC 用户级卸载脚本（A5）。
#
# 仅删除安装清单（<prefix>/.abc-im/manifest.tsv）登记的文件/目录，
# 可选恢复安装前的用户原文件备份。绝不触碰清单之外的任何路径。
#
# 安全约定：
#   - 内容与清单 sha 不符的文件视为"安装后被修改"，默认跳过并警告，
#     仅 --force-modified 才删除；
#   - 备份默认保留：若仍有未恢复备份，清单会改写为"仅备份"状态，
#     之后 `--restore-backups` 仍可恢复（避免备份成为无法追溯的孤儿文件）；
#   - --restore-backups 校验 sha 通过后恢复并删除备份；
#   - 目录仅在为空时删除（不影响其他工具的共享目录）；
#   - 清单头部登记的前缀与 --prefix 不一致时拒绝执行（防误删）；
#   - 不含凭据；不输出敏感环境变量。
#
# 用法:
#   uninstall-user.sh [--prefix PREFIX] [--manifest PATH]
#                     [--restore-backups] [--force-modified] [--dry-run]
#
# 退出码: 0 成功；1 参数/清单错误（未做任何变更）；2 部分条目未处理。
set -u -o pipefail
set -f  # 关闭 glob：路径可含空格与通配符字符

readonly PROG="uninstall-user.sh"
readonly MANIFEST_VERSION="1"

err()  { printf '%s: error: %s\n' "$PROG" "$*" >&2; }
warn() { printf '%s: warning: %s\n' "$PROG" "$*" >&2; }
log()  { printf '%s: %s\n' "$PROG" "$*"; }

usage() {
  cat <<'USAGE'
用法: uninstall-user.sh [--prefix PREFIX] [--manifest PATH]
                        [--restore-backups] [--force-modified]
                        [--allow-prefix-mismatch] [--dry-run]

  --prefix PREFIX   安装前缀（默认 $HOME；须与安装时一致）
  --manifest PATH   清单路径（默认 <prefix>/.abc-im/manifest.tsv）
  --restore-backups 恢复安装前备份的用户原文件（校验 sha 后恢复并删除备份）
  --force-modified  删除安装后被外部修改过的本项目文件（默认跳过）
  --allow-prefix-mismatch
                    允许清单登记前缀与 --prefix 不一致（迁移场景，默认拒绝）
  --dry-run         只打印计划，不做任何变更
  --help            显示本帮助

行为要点：
  - 只删除清单登记的文件；未登记路径永不触碰；
  - 不带 --restore-backups 时备份保留，清单改写为"仅备份"状态，之后仍可恢复；
  - 备份全部恢复或本就没有备份时，清单与 .abc-im 目录一并清理。
USAGE
}

sha256_of() {  # 只取 sha256 十六进制串（避免多一个 awk 进程）
  local h
  h="$(sha256sum -- "$1")" || return 1
  printf '%s' "${h%% *}"
}

manifest_prefix() {
  sed -n 's/^# prefix: //p' "$1" 2>/dev/null | head -n 1
}

PREFIX="${HOME:-}"
MANIFEST=""
RESTORE=0
FORCE=0
DRY_RUN=0
ALLOW_PREFIX_MISMATCH=0

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) [ $# -ge 2 ] || { err "--prefix 需要参数"; exit 1; }
      PREFIX=$2; shift 2 ;;
    --manifest) [ $# -ge 2 ] || { err "--manifest 需要参数"; exit 1; }
      MANIFEST=$2; shift 2 ;;
    --restore-backups) RESTORE=1; shift ;;
    --force-modified) FORCE=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --allow-prefix-mismatch) ALLOW_PREFIX_MISMATCH=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) err "未知参数: $1"; usage >&2; exit 1 ;;
  esac
done

[ -n "$PREFIX" ] || { err "--prefix 为空且 \$HOME 未设置"; exit 1; }
case "$PREFIX" in
  /*) ;;
  *) err "--prefix 必须是绝对路径"; exit 1 ;;
esac
PREFIX="$(cd -- "$PREFIX" 2>/dev/null && pwd -P)" || {
  err "--prefix 不存在或无法访问: $PREFIX"; exit 1; }
case "$PREFIX" in
  /|/usr|/usr/*|/opt|/opt/*)
    err "禁止把前缀设为 /、/usr 或 /opt"; exit 1 ;;
esac
[ -n "$MANIFEST" ] || MANIFEST="$PREFIX/.abc-im/manifest.tsv"
[ -f "$MANIFEST" ] || { err "未找到安装清单（本项目未安装或已卸载）: $MANIFEST"; exit 1; }
for tool in sha256sum awk sort sed mktemp; do
  command -v "$tool" >/dev/null 2>&1 || { err "未找到 $tool"; exit 1; }
done

ABSDIR="$PREFIX/.abc-im"
BACKUP_DIR="$ABSDIR/backup"

mprefix="$(manifest_prefix "$MANIFEST")"
if [ -n "$mprefix" ] && [ "$mprefix" != "$PREFIX" ] && [ "$ALLOW_PREFIX_MISMATCH" -eq 0 ]; then
  err "清单登记前缀与 --prefix 不一致（清单: $mprefix，本次: $PREFIX）"
  err "如确需在迁移后的前缀卸载，请加 --allow-prefix-mismatch"
  exit 1
fi

# 计数器
N_REMOVED=0; N_MISSING=0; N_MODIFIED_SKIPPED=0; N_RESTORED=0; N_DIRS=0
FAILED=0
TMP_PLAN="$(mktemp "${TMPDIR:-/tmp}/abc-uninstall-plan.XXXXXX")" || {
  err "无法创建临时计划文件"; exit 2; }
TMP_PENDING="$(mktemp "${TMPDIR:-/tmp}/abc-uninstall-pending.XXXXXX")" || {
  err "无法创建临时计划文件"; exit 2; }
TMP_REMAIN="$(mktemp "${TMPDIR:-/tmp}/abc-uninstall-remain.XXXXXX")" || {
  err "无法创建临时计划文件"; exit 2; }
trap 'rm -f -- "$TMP_PLAN" "$TMP_PENDING" "$TMP_REMAIN"' EXIT

# 解析清单 → 计划文件（F/B/D 分区）
while IFS=$'\t' read -r ftype rel fsha _; do
  case "$ftype" in
    F) printf 'F\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PLAN" ;;
    B) printf 'B\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PLAN" ;;
    D) printf 'D\t%s\n' "$rel" >> "$TMP_PLAN" ;;
    \#*|"") ;;  # 注释行（无制表符时 ftype 为整行，按前缀匹配）
    *) warn "清单含未知条目类型 '$ftype'（已跳过）" ;;
  esac
done < "$MANIFEST"

if [ "$DRY_RUN" -eq 1 ]; then
  log "dry-run 计划（清单: $MANIFEST）"
  while IFS=$'\t' read -r ftype rel fsha; do
    case "$ftype" in
      F)
        target="$PREFIX/$rel"
        if [ ! -e "$target" ]; then log "  [缺失-跳过] $rel"
        elif [ -z "$fsha" ]; then
          log "  [清单缺sha-跳过] $rel（--force-modified 可强制删除）"
        elif [ "$(sha256_of "$target")" != "$fsha" ] && [ "$FORCE" -eq 0 ]; then
          log "  [已修改-默认跳过] $rel（--force-modified 可强制删除）"
        else
          log "  [删除] $rel"
        fi ;;
      B)
        if [ "$RESTORE" -eq 1 ]; then log "  [恢复备份] $rel"
        else log "  [备份保留（清单转为仅备份状态）] $rel" ; fi ;;
      D) log "  [空目录删除] $rel" ;;
    esac
  done < "$TMP_PLAN"
  log "dry-run 结束（未做任何变更）"
  exit 0
fi

log "开始卸载（清单: $MANIFEST）"

# ---- F：删除本项目安装的文件
declare -A IN_BACKUP=()
while IFS=$'\t' read -r ftype rel fsha; do
  [ "$ftype" = "B" ] || continue
  IN_BACKUP["$rel"]=1
done < "$TMP_PLAN"

while IFS=$'\t' read -r ftype rel fsha; do
  [ "$ftype" = "F" ] || continue
  target="$PREFIX/$rel"
  if [ ! -e "$target" ]; then
    N_MISSING=$((N_MISSING+1)); continue
  fi
  if [ -z "$fsha" ]; then
    warn "清单缺 sha，保守跳过（可用 --force-modified 强制删除）: $rel"
    N_MODIFIED_SKIPPED=$((N_MODIFIED_SKIPPED+1)); FAILED=1
    printf 'F\t%s\t%s\n' "$rel" "" >> "$TMP_REMAIN"; continue
  fi
  if [ "$(sha256_of "$target")" != "$fsha" ] && [ "$FORCE" -eq 0 ]; then
    warn "跳过（安装后被修改；--force-modified 可强制删除）: $rel"
    N_MODIFIED_SKIPPED=$((N_MODIFIED_SKIPPED+1)); FAILED=1
    printf 'F\t%s\t%s\n' "$rel" "$fsha" >> "$TMP_REMAIN"; continue
  fi
  # 同路径存在备份且本次将恢复：交给恢复步骤处理（避免删了又被覆盖）
  if [ "$RESTORE" -eq 1 ] && [ -n "${IN_BACKUP[$rel]:-}" ]; then
    continue
  fi
  rm -f -- "$target" || {
    warn "删除失败: $rel"; FAILED=1
    printf 'F\t%s\t%s\n' "$rel" "$fsha" >> "$TMP_REMAIN"; continue; }
  N_REMOVED=$((N_REMOVED+1))
done < "$TMP_PLAN"

# ---- B：恢复备份（可选）；未恢复的备份登记到 TMP_PENDING
while IFS=$'\t' read -r ftype rel fsha; do
  [ "$ftype" = "B" ] || continue
  bk="$BACKUP_DIR/$rel"
  if [ "$RESTORE" -eq 0 ]; then
    if [ -f "$bk" ]; then
      printf 'B\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PENDING"
    fi
    continue
  fi
  if [ ! -f "$bk" ]; then
    warn "备份缺失，无法恢复: $rel"; FAILED=1; continue
  fi
  if [ -n "$fsha" ] && [ "$(sha256_of "$bk")" != "$fsha" ]; then
    warn "备份内容校验不符，保留备份不恢复: $rel"; FAILED=1
    printf 'B\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PENDING"
    continue
  fi
  parent="$(dirname -- "$rel")"
  [ -d "$PREFIX/$parent" ] || mkdir -p -- "$PREFIX/$parent" || {
    warn "恢复失败（无法创建目录）: $rel"; FAILED=1
    printf 'B\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PENDING"
    continue; }
  if mv -f -- "$bk" "$PREFIX/$rel"; then
    N_RESTORED=$((N_RESTORED+1)); log "已恢复用户原文件: $rel"
  else
    warn "恢复失败: $rel"; FAILED=1
    printf 'B\t%s\t%s\n' "$rel" "${fsha:-}" >> "$TMP_PENDING"
  fi
done < "$TMP_PLAN"

# ---- D：删除空目录（深度降序；仅空目录，绝不递归）
dirs=$(awk -F'\t' '$1 == "D" { print $2 }' "$TMP_PLAN" \
  | awk '{ n=gsub(/\//,"/"); print n "\t" $0 }' \
  | LC_ALL=C sort -t$'\t' -k1,1rn -k2,2 | cut -f2-)
if [ -n "$dirs" ]; then
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    if [ -d "$PREFIX/$rel" ]; then
      if rmdir -- "$PREFIX/$rel" 2>/dev/null; then
        N_DIRS=$((N_DIRS+1))
      else
        log "目录非空，保留: $rel"
      fi
    fi
  done <<< "$dirs"
fi

# ---- 清单状态收尾
# 只要还有未处理条目（跳过的 F 或未恢复的 B），就保留清单：
# 否则这些文件/备份将失去登记，之后无法再 --force-modified / --restore-backups。
if [ -s "$TMP_REMAIN" ] || [ -s "$TMP_PENDING" ]; then
  if [ -s "$TMP_PENDING" ] && [ "$RESTORE" -eq 1 ]; then
    state="restore-incomplete"
  elif [ -s "$TMP_PENDING" ]; then
    state="uninstalled-backups-pending"
  else
    state="uninstall-incomplete"
  fi
  new_manifest="$MANIFEST.tmp.$$"
  {
    printf '# abc-im install manifest v%s\n' "$MANIFEST_VERSION"
    printf '# tool: %s (project: linux-abc-im)\n' "$PROG"
    printf '# state: %s\n' "$state"
    printf '# prefix: %s\n' "$PREFIX"
    printf '# fields: F<TAB>rel<TAB>sha256<TAB>mode | D<TAB>rel | B<TAB>rel<TAB>orig_sha256\n'
    printf '# note: 本清单仅保留未处理条目；--force-modified 删除 F，--restore-backups 恢复 B\n'
    while IFS=$'\t' read -r t rel fsha; do
      [ "$t" = "F" ] || continue
      printf 'F\t%s\t%s\t%s\n' "$rel" "$fsha" 644
    done < "$TMP_REMAIN"
    while IFS=$'\t' read -r t rel fsha; do
      [ "$t" = "B" ] || continue
      printf 'B\t%s\t%s\n' "$rel" "$fsha"
    done < "$TMP_PENDING"
    # 仍然存在的本项目目录继续登记，下一次卸载才能回收
    while IFS=$'\t' read -r t rel _; do
      [ "$t" = "D" ] || continue
      [ -d "$PREFIX/$rel" ] || continue
      printf 'D\t%s\n' "$rel"
    done < "$TMP_PLAN"
  } > "$new_manifest" && mv -f -- "$new_manifest" "$MANIFEST" || {
    warn "无法改写清单（原清单保留）: $MANIFEST"; FAILED=1; }
  n_pending="$(awk -F'\t' '$1=="B" {n++} END{print n+0}' "$TMP_PENDING")"
  n_remain="$(awk -F'\t' '$1=="F" {n++} END{print n+0}' "$TMP_REMAIN")"
  [ "$n_remain" -gt 0 ] && log "未删除文件保留登记 $n_remain 个（--force-modified 可强制删除）"
  [ "$n_pending" -gt 0 ] && log "备份保留 $n_pending 个于 $BACKUP_DIR（--restore-backups 可恢复）"
  log "清单已保留为 state=$state；处理完剩余条目后清单自动清理"
else
  rm -f -- "$MANIFEST" || warn "无法删除清单文件: $MANIFEST"
  find "$BACKUP_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true
  if [ -d "$BACKUP_DIR" ] && [ -z "$(ls -A -- "$BACKUP_DIR" 2>/dev/null)" ]; then
    rmdir -- "$BACKUP_DIR" 2>/dev/null || true
  fi
  if [ -d "$ABSDIR" ] && [ -z "$(ls -A -- "$ABSDIR" 2>/dev/null)" ]; then
    rmdir -- "$ABSDIR" 2>/dev/null || true
  fi
fi

log "卸载完成: 删除 $N_REMOVED，恢复备份 $N_RESTORED，缺失 $N_MISSING，跳过修改 $N_MODIFIED_SKIPPED，目录 $N_DIRS"
if [ "$FAILED" -eq 1 ]; then
  err "存在未完全处理的条目（见上方警告），退出码 2"
  exit 2
fi
exit 0
