#!/usr/bin/env bash
# install-user.sh — Linux 智能 ABC 用户级安装脚本（A5）。
#
# 把一个 staging 目录（相对路径树）按 1:1 镜像安装到用户级前缀
# （--prefix，默认 $HOME；也可用项目隔离前缀）。相对布局由 build-staging.sh
# 按 A4 的 install-manifest.md 组装，本脚本不预设 fcitx5 子路径。
#
# 安全约定：
#   - 拒绝 /、/usr、/usr/**、/opt、/opt/** 作为前缀（原始串与规范化后各校验一次）；
#   - 目标真实路径（realpath）落入 /usr 或 /opt 时拒绝写入（防符号链接绕过）；
#   - 每个被覆盖的既有文件先备份，失败回滚与卸载恢复均以此为依据；
#   - 卸载与回滚的唯一依据是清单 <prefix>/.abc-im/manifest.tsv；
#   - 任何失败自动回滚本次安装的全部变更（退出码 2）；
#   - 清单登记前缀与 --prefix 不一致时拒绝执行（防误备份/误覆盖）；
#   - 不含凭据；不输出敏感环境变量。
#
# 重复安装语义（可预测）：
#   - 与清单 sha 一致的文件（本项目已有）：原地更新；备份仅用于失败回滚，
#     成功后删除；
#   - 已存在但非本项目（或本项目文件被外部修改）的文件：先备份再覆盖，
#     备份保留至卸载（--restore-backups 可恢复用户原文件）；
#   - staging 根控制文件 .abc-im-no-overwrite.list 列出的路径：目标存在即跳过
#     （用于用户配置模板，绝不覆盖用户数据）；
#   - 旧清单登记但新 staging 缺失的文件：保留并继续登记（升级不删除）。
#
# 用法:
#   install-user.sh --staging DIR [--prefix PREFIX] [--dry-run]
#   install-user.sh --verify [--prefix PREFIX]
#
# 退出码: 0 成功；1 参数/校验错误（未做任何变更）；2 安装失败（已回滚）或校验不符。
set -u -o pipefail
set -f  # 关闭 glob：路径可含空格与通配符字符

readonly PROG="install-user.sh"
readonly MANIFEST_VERSION="1"
readonly ABC_IM_DIR=".abc-im"                      # prefix 相对
readonly NOOVERWRITE_FILE=".abc-im-no-overwrite.list"  # staging 根控制文件
readonly CONTROL_GLOB=".abc-im-*"                  # staging 根控制文件（不安装）

readonly EXIT_USAGE=1
readonly EXIT_ABORTED=2

# ---------------------------------------------------------------- 基础工具
err()  { printf '%s: error: %s\n' "$PROG" "$*" >&2; }
warn() { printf '%s: warning: %s\n' "$PROG" "$*" >&2; }
log()  { printf '%s: %s\n' "$PROG" "$*"; }

usage() {
  cat <<'USAGE'
用法: install-user.sh --staging DIR [--prefix PREFIX] [--dry-run]
      install-user.sh --verify    [--prefix PREFIX]
      install-user.sh --print-env [--prefix PREFIX]

  --staging DIR  待安装的相对路径树（必填；由 build-staging.sh 或 cmake 安装产出）
  --prefix DIR   安装前缀（默认 $HOME；禁止 /、/usr/**、/opt/**）
  --dry-run      只打印计划，不做任何变更
  --verify       只校验已安装状态（清单 vs 磁盘 sha），不写任何文件
  --print-env    只打印安全的 FCITX_ADDON_DIRS 建议值（不写文件、不改环境）
  --allow-prefix-mismatch
                 允许清单登记前缀与本次 --prefix 不一致（迁移场景，默认拒绝）
  --help         显示本帮助

行为要点：
  - 非本项目文件先备份到 <prefix>/.abc-im/backup/ 再覆盖（卸载可恢复）；
  - staging 根部 .abc-im-no-overwrite.list 登记的路径存在即跳过；
  - 失败自动回滚本次全部变更；卸载请使用 uninstall-user.sh。
USAGE
}

sha256_of() {  # 只取 sha256 十六进制串（避免多一个 awk 进程）
  local h
  h="$(sha256sum -- "$1")" || return 1
  printf '%s' "${h%% *}"
}

# 清单头部登记的安装前缀（用于检测"前缀与清单不一致"的误操作）
manifest_prefix() {
  sed -n 's/^# prefix: //p' "$1" 2>/dev/null | head -n 1
}

# ---------------------------------------------------------------- 环境变量建议
# 只读：打印安全的 FCITX_ADDON_DIRS 值，不写文件、不改环境。
# 语义：该变量会替换 fcitx5 内置 addon 目录列表，因此必须保留原有条目；
# 已设置时把本项目库目录前置，未设置时附带系统目录 /usr/lib/fcitx5。
print_addon_env() {
  local libdir="" rel
  if [ -f "$MANIFEST" ]; then
    while IFS=$'\t' read -r ftype rel _; do
      [ "$ftype" = "F" ] || continue
      case "$rel" in
        */fcitx5/libabc.so) libdir="$PREFIX/$(dirname -- "$rel")"; break ;;
      esac
    done < "$MANIFEST"
  fi
  [ -n "$libdir" ] || libdir="$PREFIX/.local/lib/fcitx5"

  local existing="${FCITX_ADDON_DIRS:-}" value
  if [ -z "$existing" ]; then
    value="$libdir:/usr/lib/fcitx5"
  else
    case ":$existing:" in
      *":$libdir:"*) value="$existing" ;;   # 已包含本项目目录，保持原值
      *) value="$libdir:$existing" ;;       # 前置本项目目录，保留用户其余设置
    esac
  fi
  printf 'export FCITX_ADDON_DIRS="%s"\n' "$value"
  printf '%s: 仅打印建议值，未修改任何文件或环境；已有条目的其余部分保持不变\n' "$PROG" >&2
  exit 0
}

# ---------------------------------------------------------------- 校验模式
# 只读：比对清单登记的 sha 与磁盘实际内容，不写任何文件。
verify_install() {
  [ -f "$MANIFEST" ] || {
    err "未找到安装清单（本项目未安装或已卸载）: $MANIFEST"; exit "$EXIT_USAGE"; }
  local mprefix
  mprefix="$(manifest_prefix "$MANIFEST")"
  if [ -n "$mprefix" ] && [ "$mprefix" != "$PREFIX" ] && [ "$ALLOW_PREFIX_MISMATCH" -eq 0 ]; then
    err "清单登记前缀与 --prefix 不一致（清单: $mprefix，本次: $PREFIX）"
    err "如确需在迁移后的前缀校验，请加 --allow-prefix-mismatch"
    exit "$EXIT_USAGE"
  fi
  local ftype rel fsha n_ok=0 n_bad=0
  while IFS=$'\t' read -r ftype rel fsha _; do
    case "$ftype" in
      F)
        if [ ! -e "$PREFIX/$rel" ]; then
          err "缺失文件: $rel"; n_bad=$((n_bad+1))
        elif [ -z "$fsha" ]; then
          warn "清单缺 sha，无法校验: $rel"; n_bad=$((n_bad+1))
        elif [ "$(sha256_of "$PREFIX/$rel")" != "$fsha" ]; then
          err "内容与清单不符（安装后被修改？）: $rel"; n_bad=$((n_bad+1))
        else
          n_ok=$((n_ok+1))
        fi ;;
      B)
        if [ ! -f "$BACKUP_DIR/$rel" ]; then
          warn "备份缺失: $rel"; n_bad=$((n_bad+1))
        elif [ -n "$fsha" ] && [ "$(sha256_of "$BACKUP_DIR/$rel")" != "$fsha" ]; then
          err "备份内容与清单不符: $rel"; n_bad=$((n_bad+1))
        else
          n_ok=$((n_ok+1))
        fi ;;
      D)
        [ -d "$PREFIX/$rel" ] || { warn "目录缺失: $rel"; n_bad=$((n_bad+1)); } ;;
      \#*|"") ;;
      *) warn "清单含未知条目类型 '$ftype'（已跳过）" ;;
    esac
  done < "$MANIFEST"
  if [ "$n_bad" -eq 0 ]; then
    log "校验通过: $n_ok 项一致（清单: $MANIFEST）"
    exit 0
  fi
  err "校验未通过: $n_bad 项异常，$n_ok 项一致"
  exit "$EXIT_ABORTED"
}

# ---------------------------------------------------------------- 参数解析
STAGING=""
PREFIX="${HOME:-}"
DRY_RUN=0
VERIFY=0
PRINT_ENV=0
ALLOW_PREFIX_MISMATCH=0

while [ $# -gt 0 ]; do
  case "$1" in
    --staging) [ $# -ge 2 ] || { err "--staging 需要参数"; exit "$EXIT_USAGE"; }
      STAGING=$2; shift 2 ;;
    --prefix) [ $# -ge 2 ] || { err "--prefix 需要参数"; exit "$EXIT_USAGE"; }
      PREFIX=$2; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --verify) VERIFY=1; shift ;;
    --print-env) PRINT_ENV=1; shift ;;
    --allow-prefix-mismatch) ALLOW_PREFIX_MISMATCH=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) err "未知参数: $1"; usage >&2; exit "$EXIT_USAGE" ;;
  esac
done

# ---------------------------------------------------------------- 前缀校验
[ -n "$PREFIX" ] || { err "--prefix 为空且 \$HOME 未设置"; exit "$EXIT_USAGE"; }
case "$PREFIX" in
  /*) ;;  # 必须绝对路径
  *) err "--prefix 必须是绝对路径: $PREFIX"; exit "$EXIT_USAGE" ;;
esac
# 安全校验先于任何创建动作（原始字符串与规范化后各一次）
case "$PREFIX" in
  /|/usr|/usr/*|/opt|/opt/*)
    err "禁止把前缀设为 /、/usr 或 /opt（项目安全边界，禁止覆盖系统文件）"
    exit "$EXIT_USAGE" ;;
esac
PREFIX="$(realpath -m -- "$PREFIX" 2>/dev/null)" || {
  err "--prefix 路径非法: 无法规范化"; exit "$EXIT_USAGE"; }
case "$PREFIX" in
  /|/usr|/usr/*|/opt|/opt/*)
    err "禁止把前缀设为 /、/usr 或 /opt（规范化后判定）"
    exit "$EXIT_USAGE" ;;
esac
ABSDIR="$PREFIX/$ABC_IM_DIR"
MANIFEST="$ABSDIR/manifest.tsv"
BACKUP_DIR="$ABSDIR/backup"        # 用户原文件备份（保留至卸载/恢复）
ROLLBACK_DIR="$ABSDIR/rollback"    # 本项目文件的临时回滚副本（成功后删除）

# 环境变量建议模式：只读，先于任何创建动作返回
if [ "$PRINT_ENV" -eq 1 ]; then
  [ -z "$STAGING" ] || { err "--print-env 不接受 --staging"; exit "$EXIT_USAGE"; }
  print_addon_env
fi

# 校验模式：只读，必须在任何创建动作之前返回
if [ "$VERIFY" -eq 1 ]; then
  [ -z "$STAGING" ] || { err "--verify 不接受 --staging"; exit "$EXIT_USAGE"; }
  [ -d "$PREFIX" ] || { err "--prefix 不存在，无安装可校验: $PREFIX"; exit "$EXIT_USAGE"; }
  for tool in sha256sum awk sed; do
    command -v "$tool" >/dev/null 2>&1 || { err "未找到 $tool"; exit "$EXIT_USAGE"; }
  done
  verify_install
fi

PREFIX_CREATED=0
if [ ! -d "$PREFIX" ]; then
  if [ "$DRY_RUN" -eq 1 ]; then
    :  # dry-run 不创建任何目录；计划把全部目标视为不存在
  else
    mkdir -p -- "$PREFIX" || { err "无法创建前缀目录: $PREFIX"; exit "$EXIT_USAGE"; }
    PREFIX_CREATED=1
  fi
fi

# ---------------------------------------------------------------- staging 校验
[ -n "$STAGING" ] || { err "--staging 为空"; usage >&2; exit "$EXIT_USAGE"; }
[ -d "$STAGING" ] || { err "staging 目录不存在: $STAGING"; exit "$EXIT_USAGE"; }
STAGING="$(cd -- "$STAGING" && pwd -P)"
if [ -z "$(ls -A -- "$STAGING" 2>/dev/null)" ]; then
  err "staging 目录为空: $STAGING"; exit "$EXIT_USAGE"
fi
for tool in sha256sum find awk stat cp mv dirname realpath; do
  command -v "$tool" >/dev/null 2>&1 || { err "未找到 $tool"; exit "$EXIT_USAGE"; }
done

if [ -e "$STAGING/$NOOVERWRITE_FILE" ] && [ ! -f "$STAGING/$NOOVERWRITE_FILE" ]; then
  err "staging 控制文件不是普通文件: $NOOVERWRITE_FILE"; exit "$EXIT_USAGE"
fi

# 符号链接不支持（镜像语义不明确，明确报错而非猜测）
if [ -n "$(find "$STAGING" -mindepth 1 -type l -print -quit 2>/dev/null)" ]; then
  err "staging 含符号链接，本工具不支持（请展开为真实文件）"; exit "$EXIT_USAGE"
fi

# 枚举 staging 条目（目录 + 文件，C 排序保证父目录先于子目录；
# find 的 %y 输出小写 d/f，统一大写为 D/F 与清单格式一致）
ENTRIES="$(find "$STAGING" -mindepth 1 \( -type f -o -type d \) -printf '%y\t%P\n' 2>/dev/null \
  | awk -F'\t' 'BEGIN{OFS="\t"} {$1=toupper($1); print}' \
  | LC_ALL=C sort)" || {
  err "枚举 staging 失败"; exit "$EXIT_USAGE"; }
[ -n "$ENTRIES" ] || { err "staging 无可安装条目: $STAGING"; exit "$EXIT_USAGE"; }
# 防御：清单是 TSV，相对路径不得含制表符/换行符
if ! printf '%s\n' "$ENTRIES" | awk -F'\t' 'NF != 2 { exit 1 }'; then
  err "staging 含非法路径（制表符/换行符）"; exit "$EXIT_USAGE"
fi

# 根部控制文件（.abc-im-*）不安装；此处 glob 匹配是有意的
is_control_file() {
  # shellcheck disable=SC2053  # 有意使用 glob 模式
  [[ "$1" == $CONTROL_GLOB ]]
}

# ---------------------------------------------------------------- 读旧清单
declare -A PREV_SHA=()   # F 条目: rel -> sha
declare -A PREV_DIR=()   # D 条目集合
declare -A PREV_B=()     # B 条目: rel -> sha（用户原始内容）
HAVE_MANIFEST=0
if [ ! -f "$MANIFEST" ] && [ -f "$ABSDIR/manifest.list" ]; then
  # A4 参考脚本（deploy/fcitx5-addon/install-user.sh）使用 manifest.list，
  # 格式与本脚本的 manifest.tsv 不兼容；混用会互相看不到对方安装的文件。
  err "检测到 A4 参考脚本的清单 .abc-im/manifest.list，与本脚本清单不兼容"
  err "请先运行 deploy/fcitx5-addon/uninstall-user.sh 卸载，再使用 scripts/install-user.sh"
  exit "$EXIT_USAGE"
fi
if [ -f "$MANIFEST" ]; then
  HAVE_MANIFEST=1
  while IFS=$'\t' read -r ftype rel fsha _; do
    case "$ftype" in
      F) PREV_SHA["$rel"]="$fsha" ;;
      D) PREV_DIR["$rel"]=1 ;;
      B) PREV_B["$rel"]="${fsha:-}" ;;
      \#*|"") ;;  # 注释行（无制表符时 ftype 为整行，按前缀匹配）
      *) warn "清单含未知条目类型 '$ftype'（已跳过）" ;;
    esac
  done < "$MANIFEST"
  # 清单前缀一致性：避免把 A 前缀的清单当成 B 前缀的状态（会误备份/误覆盖）
  prev_prefix="$(manifest_prefix "$MANIFEST")"
  if [ -n "$prev_prefix" ] && [ "$prev_prefix" != "$PREFIX" ] &&
     [ "$ALLOW_PREFIX_MISMATCH" -eq 0 ]; then
    err "清单登记前缀与本次 --prefix 不一致（清单: $prev_prefix，本次: $PREFIX）"
    err "如确需迁移，请加 --allow-prefix-mismatch"
    exit "$EXIT_USAGE"
  fi
fi

# ---------------------------------------------------------------- no-overwrite 清单
declare -A NOOVERWRITE=()
if [ -f "$STAGING/$NOOVERWRITE_FILE" ]; then
  while IFS= read -r rel || [ -n "$rel" ]; do
    rel="${rel#"${rel%%[![:space:]]*}"}"; rel="${rel%"${rel##*[![:space:]]}"}"
    case "$rel" in ""|\#*) continue ;; esac
    NOOVERWRITE["$rel"]=1
  done < "$STAGING/$NOOVERWRITE_FILE"
fi

# ---------------------------------------------------------------- 回滚
declare -a CREATED_DIRS=()   # 本次新建的目录（prefix 相对）
declare -a ROLLBACK_UP=()    # 覆盖本项目文件前的临时副本（成功后删除）
declare -a FOREIGN_UP=()     # 覆盖用户原文件前的备份（保留至卸载）
declare -a FOREIGN_NEW=()    # 本次新建的备份（回滚时删除，避免孤儿备份）
declare -a CREATED_FILES=()  # 本次新建（原目标不存在）的文件
MUTATED=0                    # 从第一个写操作起为 1

dirs_deepest_first() {  # 深度优先排序（深者先处理）
  printf '%s\n' "$@" | awk '{ n=gsub(/\//,"/"); print n "\t" $0 }' \
    | LC_ALL=C sort -t$'\t' -k1,1rn -k2,2 | cut -f2-
}

rollback_and_die() {
  err "$1"
  err "开始回滚本次安装的全部变更"
  local rel target bk
  [ -n "${NEW_MANIFEST:-}" ] && rm -f -- "$NEW_MANIFEST" 2>/dev/null || true
  if [ "$MUTATED" -eq 1 ]; then
    for rel in ${ROLLBACK_UP[@]+"${ROLLBACK_UP[@]}"}; do
      target="$PREFIX/$rel"; bk="$ROLLBACK_DIR/$rel"
      if [ -f "$bk" ]; then
        cp -p -- "$bk" "$target" || err "回滚：恢复失败: $rel"
      else
        err "回滚：临时副本缺失，无法恢复: $rel"
      fi
    done
    for rel in ${FOREIGN_UP[@]+"${FOREIGN_UP[@]}"}; do
      target="$PREFIX/$rel"; bk="$BACKUP_DIR/$rel"
      if [ -f "$bk" ]; then
        cp -p -- "$bk" "$target" || err "回滚：恢复失败（备份已保留）: $rel"
      else
        err "回滚：备份缺失，无法恢复: $rel"
      fi
    done
    for rel in ${CREATED_FILES[@]+"${CREATED_FILES[@]}"}; do
      rm -f -- "$PREFIX/$rel" || err "回滚：删除失败: $rel"
    done
    if [ "${#CREATED_DIRS[@]}" -gt 0 ]; then
      while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        rmdir -- "$PREFIX/$rel" 2>/dev/null || true
      done < <(dirs_deepest_first "${CREATED_DIRS[@]}")
    fi
    if [ -d "$ROLLBACK_DIR" ]; then
      find "$ROLLBACK_DIR" -type f -delete 2>/dev/null || true
      find "$ROLLBACK_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true
      rmdir -- "$ROLLBACK_DIR" 2>/dev/null || true
    fi
    # 本次新建的备份在回滚后没有意义（原文件已恢复），删除以免留下孤儿备份
    for rel in ${FOREIGN_NEW[@]+"${FOREIGN_NEW[@]}"}; do
      rm -f -- "$BACKUP_DIR/$rel" 2>/dev/null || true
    done
    if [ -d "$BACKUP_DIR" ]; then
      find "$BACKUP_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true
      rmdir -- "$BACKUP_DIR" 2>/dev/null || true
    fi
    rmdir -- "$ABSDIR" 2>/dev/null || true
    if [ "$PREFIX_CREATED" = "1" ]; then
      rmdir -- "$PREFIX" 2>/dev/null || true   # 仅当为空时移除本次创建的前缀
    fi
  fi
  err "回滚完成；旧清单（若有）保持原样。退出码 $EXIT_ABORTED"
  exit "$EXIT_ABORTED"
}

# 防御纵深：前缀本身已校验，但子目录可能是指向系统目录的符号链接。
# 解析真实路径后再次拒绝 /usr、/opt，避免经符号链接写入系统位置。
guard_no_system_path() {  # $1 = prefix 相对路径
  local resolved
  resolved="$(realpath -m -- "$PREFIX/$1" 2>/dev/null)" || return 0
  case "$resolved" in
    /usr|/usr/*|/opt|/opt/*)
      rollback_and_die "目标真实路径位于系统目录（疑似符号链接），拒绝写入: $1" ;;
  esac
}

mkdirp_tracked() {  # $1 = prefix 相对目录；逐级创建并登记新建目录
  # shellcheck disable=SC2206  # 有意按 / 分词以支持含空格路径
  local acc="" part
  local IFS='/'
  guard_no_system_path "$1"   # realpath 已解析全部中间组件，检查最终路径即可
  for part in $1; do
    [ -n "$part" ] || continue
    if [ -z "$acc" ]; then acc="$part"; else acc="$acc/$part"; fi
    if [ ! -d "$PREFIX/$acc" ]; then
      mkdir -- "$PREFIX/$acc" 2>/dev/null || { rollback_and_die "无法创建目录: $acc"; }
      CREATED_DIRS+=("$acc")
    elif [ ! -w "$PREFIX/$acc" ]; then
      rollback_and_die "目录存在但不可写: $acc"
    fi
  done
}

# ---------------------------------------------------------------- 计划
declare -A ACTION=()  # file rel -> create | update-ours | update-foreign | skip
N_CREATE=0; N_UPDATE=0; N_SKIP=0; N_BACKUP=0

while IFS=$'\t' read -r ftype rel; do
  if is_control_file "$rel"; then continue; fi  # 根部控制文件不安装
  if [ "$ftype" = "D" ]; then
    if [ -e "$PREFIX/$rel" ] && [ ! -d "$PREFIX/$rel" ]; then
      err "目标路径被非目录对象占用: $rel"; exit "$EXIT_USAGE"  # 计划期，无变更
    fi
    continue
  fi
  target="$PREFIX/$rel"
  if [ -d "$target" ]; then
    err "目标路径被目录占用（应为文件）: $rel"; exit "$EXIT_USAGE"
  fi
  if [ -n "${NOOVERWRITE[$rel]:-}" ] && [ -e "$target" ]; then
    ACTION["$rel"]="skip"; N_SKIP=$((N_SKIP+1)); continue
  fi
  if [ -e "$target" ]; then
    prev="${PREV_SHA[$rel]:-}"
    if [ -n "$prev" ] && [ "$(sha256_of "$target")" = "$prev" ]; then
      ACTION["$rel"]="update-ours"; N_UPDATE=$((N_UPDATE+1))
    else
      ACTION["$rel"]="update-foreign"; N_UPDATE=$((N_UPDATE+1)); N_BACKUP=$((N_BACKUP+1))
    fi
  else
    ACTION["$rel"]="create"; N_CREATE=$((N_CREATE+1))
  fi
done <<< "$ENTRIES"

if [ "$DRY_RUN" -eq 1 ]; then
  log "dry-run 计划: 新建 $N_CREATE，更新 $N_UPDATE（其中先备份 $N_BACKUP），跳过 $N_SKIP"
  log "前缀: $PREFIX"
  log "清单: $MANIFEST（当前 $([ "$HAVE_MANIFEST" -eq 1 ] && echo 存在 || echo 不存在)）"
  while IFS=$'\t' read -r ftype rel; do
    if is_control_file "$rel"; then continue; fi
    case "$ftype" in
      D) log "  [目录] $rel" ;;
      F)
        case "${ACTION[$rel]:-create}" in
          create)         log "  [新建] $rel" ;;
          update-ours)    log "  [更新-本项目] $rel" ;;
          update-foreign) log "  [更新-先备份] $rel" ;;
          skip)           log "  [跳过-已存在] $rel" ;;
        esac ;;
    esac
  done <<< "$ENTRIES"
  log "dry-run 结束（未做任何变更）"
  exit 0
fi

log "计划: 新建 $N_CREATE，更新 $N_UPDATE（其中先备份 $N_BACKUP），跳过 $N_SKIP"
log "前缀: $PREFIX"
MUTATED=1

# ---------------------------------------------------------------- 执行：目录
while IFS=$'\t' read -r ftype rel; do
  [ "$ftype" = "D" ] || continue
  if is_control_file "$rel"; then continue; fi
  [ -d "$PREFIX/$rel" ] || mkdirp_tracked "$rel"
done <<< "$ENTRIES"
mkdirp_tracked "$ABC_IM_DIR"

# ---------------------------------------------------------------- 执行：备份
# update-ours   → 复制到 .abc-im/rollback/（仅失败回滚用，成功后删除）
# update-foreign→ 复制到 .abc-im/backup/（用户原文件，保留至卸载；已有则不再覆盖）
while IFS=$'\t' read -r ftype rel; do
  [ "$ftype" = "F" ] || continue
  act="${ACTION[$rel]:-}"
  case "$act" in update-ours|update-foreign) ;; *) continue ;; esac
  if is_control_file "$rel"; then continue; fi
  target="$PREFIX/$rel"
  if [ "$act" = "update-ours" ]; then
    bk="$ROLLBACK_DIR/$rel"
    mkdir -p -- "$(dirname -- "$bk")" || rollback_and_die "创建回滚目录失败: $rel"
    cp -p -- "$target" "$bk" || rollback_and_die "回滚副本失败: $rel"
    [ "$(sha256_of "$bk")" = "$(sha256_of "$target")" ] || {
      rollback_and_die "回滚副本校验失败: $rel"; }
    ROLLBACK_UP+=("$rel")
  else
    bk="$BACKUP_DIR/$rel"
    if [ -f "$bk" ]; then
      log "已存在用户原始备份，保留不动: $rel"
    else
      mkdir -p -- "$(dirname -- "$bk")" || rollback_and_die "创建备份目录失败: $rel"
      cp -p -- "$target" "$bk" || rollback_and_die "备份失败: $rel"
      [ "$(sha256_of "$bk")" = "$(sha256_of "$target")" ] || {
        rollback_and_die "备份校验失败: $rel"; }
      log "已备份: $rel -> $ABC_IM_DIR/backup/$rel"
      FOREIGN_NEW+=("$rel")
    fi
    FOREIGN_UP+=("$rel")
  fi
done <<< "$ENTRIES"

# ---------------------------------------------------------------- 执行：安装
install_one() {  # $1 = rel；复制 staging 文件到目标（临时文件 + 原子改名）
  local rel=$1 target="$PREFIX/$1" src="$STAGING/$1" tmp
  tmp="$target.abc-tmp.$$"
  cp -p -- "$src" "$tmp" || { rm -f -- "$tmp"; rollback_and_die "复制失败: $rel"; }
  mv -f -- "$tmp" "$target" || { rm -f -- "$tmp"; rollback_and_die "落盘失败: $rel"; }
  local digest
  digest="$(sha256_of "$target")" || rollback_and_die "校验失败: $rel"
  [ -n "$digest" ] || rollback_and_die "校验失败: $rel"
  NEW_SHA["$rel"]="$digest"
  INSTALLED+=("$rel")
}

declare -A NEW_SHA=()
declare -a INSTALLED=()

while IFS=$'\t' read -r ftype rel; do
  [ "$ftype" = "F" ] || continue
  act="${ACTION[$rel]:-}"
  [ "$act" = "create" ] || [ "$act" = "update-ours" ] || continue
  if is_control_file "$rel"; then continue; fi
  mkdirp_tracked "$(dirname -- "$rel")" || rollback_and_die "创建目录失败"
  install_one "$rel"
  if [ "$act" = "create" ]; then
    CREATED_FILES+=("$rel"); log "新建: $rel"
  else
    log "更新: $rel"
  fi
done <<< "$ENTRIES"

while IFS=$'\t' read -r ftype rel; do
  [ "$ftype" = "F" ] || continue
  [ "${ACTION[$rel]:-}" = "update-foreign" ] || continue
  if is_control_file "$rel"; then continue; fi
  mkdirp_tracked "$(dirname -- "$rel")" || rollback_and_die "创建目录失败"
  install_one "$rel"
  log "更新（原文件已备份）: $rel"
done <<< "$ENTRIES"

# ---------------------------------------------------------------- 写清单
# F = 旧清单 ∪ 本次安装（更新者取新 sha）；D = 旧清单 ∪ 本次新建；
# B = 旧清单 ∪ 本次外部文件备份（同路径保留最早的原始备份）
NEW_MANIFEST="$MANIFEST.tmp.$$"
declare -A FINAL_SHA=()
declare -A FINAL_DIR=()
declare -A FINAL_B=()
declare -A B_SEEN=()
rel=""
for rel in "${!PREV_SHA[@]}"; do FINAL_SHA["$rel"]="${PREV_SHA[$rel]}"; done
for rel in ${INSTALLED[@]+"${INSTALLED[@]}"}; do FINAL_SHA["$rel"]="${NEW_SHA[$rel]}"; done
for rel in "${!PREV_DIR[@]}"; do FINAL_DIR["$rel"]=1; done
for rel in ${CREATED_DIRS[@]+"${CREATED_DIRS[@]}"}; do
  case "$rel" in "$ABC_IM_DIR"|"$ABC_IM_DIR"/*) continue ;; esac
  FINAL_DIR["$rel"]=1
done
for rel in "${!PREV_B[@]}"; do FINAL_B["$rel"]="${PREV_B[$rel]}"; B_SEEN["$rel"]=1; done
for rel in ${FOREIGN_UP[@]+"${FOREIGN_UP[@]}"}; do
  [ -n "${B_SEEN[$rel]:-}" ] && continue          # 同路径保留最早原始备份
  digest="$(sha256_of "$BACKUP_DIR/$rel")" || rollback_and_die "备份校验失败: $rel"
  [ -n "$digest" ] || rollback_and_die "备份校验失败: $rel"
  FINAL_B["$rel"]="$digest"
done

{
  printf '# abc-im install manifest v%s\n' "$MANIFEST_VERSION"
  printf '# tool: %s (project: linux-abc-im)\n' "$PROG"
  printf '# created: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# prefix: %s\n' "$PREFIX"
  printf '# fields: F<TAB>rel<TAB>sha256<TAB>mode | D<TAB>rel | B<TAB>rel<TAB>orig_sha256\n'
  # 注意：路径可含空格，遍历一律 while-read，禁止 for + 命令替换分词
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    printf 'F\t%s\t%s\t%s\n' "$rel" "${FINAL_SHA[$rel]}" \
      "$(stat -c '%a' -- "$PREFIX/$rel" 2>/dev/null || printf 644)"
  done < <(printf '%s\n' "${!FINAL_SHA[@]}" | LC_ALL=C sort)
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    printf 'D\t%s\n' "$rel"
  done < <(printf '%s\n' "${!FINAL_DIR[@]}" | LC_ALL=C sort)
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    printf 'B\t%s\t%s\n' "$rel" "${FINAL_B[$rel]}"
  done < <(printf '%s\n' "${!FINAL_B[@]}" | LC_ALL=C sort)
} > "$NEW_MANIFEST" || rollback_and_die "写清单失败"
mv -f -- "$NEW_MANIFEST" "$MANIFEST" || rollback_and_die "落盘清单失败"

# 清理本次的回滚副本（成功后不再需要；用户原文件备份保留）
if [ -d "$ROLLBACK_DIR" ]; then
  find "$ROLLBACK_DIR" -type f -delete 2>/dev/null || true
  find "$ROLLBACK_DIR" -mindepth 1 -type d -empty -delete 2>/dev/null || true
  rmdir -- "$ROLLBACK_DIR" 2>/dev/null || true
fi

log "安装完成: 新建 $N_CREATE，更新 $N_UPDATE，跳过 $N_SKIP，备份保留 $N_BACKUP"
log "清单: $MANIFEST"
if [ "$N_BACKUP" -gt 0 ]; then
  log "备份目录: $BACKUP_DIR（卸载时可用 --restore-backups 恢复用户原文件）"
fi
exit 0
