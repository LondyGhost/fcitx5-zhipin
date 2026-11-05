#!/usr/bin/env bash
# build-rime-index.sh — 把 Rime 词库（如 rime-ice）编译成 abc 引擎索引并做用户级安装。
#
# 流程：merge_rime_dict.py 合并 import_tables → abc_dict_import 编译索引 → 备份旧索引 → 安装。
# 只写用户目录；不碰系统路径，不覆盖未备份的文件。
#
# 用法:
#   build-rime-index.sh --import-tool PATH [--rime-dir DIR] [--main REL] [--prefix DIR] [--restart]
#
#   --import-tool PATH  abc_dict_import 可执行文件（构建产物）
#   --rime-dir DIR      Rime 用户目录（默认 ~/.local/share/fcitx5/rime）
#   --abbr-max-syllables N  为 2..N 音节的词生成简拼/混拼码（默认 4；0 = 关闭）
#   --main REL          入口词库，相对 --rime-dir（默认 ice/rime_ice.dict.yaml）
#   --prefix DIR        安装前缀（默认 $HOME），索引落到 <prefix>/.local/share/abc/index.abcdix
#   --restart           安装后重启 fcitx5 用户服务
#
# 退出码: 0 成功；1 参数/环境错误；2 合并或导入失败。

set -u -o pipefail

PROG="build-rime-index.sh"
err() { printf '%s: error: %s\n' "$PROG" "$*" >&2; }
log() { printf '%s: %s\n' "$PROG" "$*"; }

IMPORT_TOOL=""
RIME_DIR="$HOME/.local/share/fcitx5/rime"
MAIN="ice/rime_ice.dict.yaml"
PREFIX="$HOME"
RESTART=0
ABBR_MAX=4

while [ $# -gt 0 ]; do
  case "$1" in
    --import-tool) IMPORT_TOOL="${2:-}"; shift 2 ;;
    --rime-dir)    RIME_DIR="${2:-}"; shift 2 ;;
    --main)        MAIN="${2:-}"; shift 2 ;;
    --prefix)      PREFIX="${2:-}"; shift 2 ;;
    --abbr-max-syllables) ABBR_MAX="${2:-4}"; shift 2 ;;
    --restart)     RESTART=1; shift ;;
    -h|--help)     sed -n '2,20p' "$0"; exit 0 ;;
    *)             err "unknown option: $1"; exit 1 ;;
  esac
done

[ -n "$IMPORT_TOOL" ] || { err "missing --import-tool"; exit 1; }
[ -x "$IMPORT_TOOL" ] || { err "import tool not executable: $IMPORT_TOOL"; exit 1; }
[ -f "$RIME_DIR/$MAIN" ] || { err "main dict not found: $RIME_DIR/$MAIN"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MERGE="$SCRIPT_DIR/merge_rime_dict.py"
[ -f "$MERGE" ] || { err "merge script missing: $MERGE"; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/abc-rime-index.XXXXXX")" || exit 2
trap 'rm -rf "$WORK"' EXIT

log "merge: $RIME_DIR/$MAIN"
if ! python3 "$MERGE" --main "$RIME_DIR/$MAIN" --out "$WORK/merged.tsv" \
      --abbr-max-syllables "$ABBR_MAX"; then
  err "merge failed"; exit 2
fi

log "import: $(wc -l < "$WORK/merged.tsv") entries"
if ! "$IMPORT_TOOL" --input "$WORK/merged.tsv" --output "$WORK/index.abcdix" \
      --source rime-ice; then
  err "import failed"; exit 2
fi

DEST_DIR="$PREFIX/.local/share/abc"
DEST="$DEST_DIR/index.abcdix"
mkdir -p "$DEST_DIR"
if [ -f "$DEST" ]; then
  BACKUP="$DEST.$(date +%Y%m%d-%H%M%S).bak"
  cp -a "$DEST" "$BACKUP"
  log "backup: $BACKUP"
fi
install -m 644 "$WORK/index.abcdix" "$DEST"
log "installed: $DEST ($(stat -c %s "$DEST") bytes)"

if [ "$RESTART" = "1" ]; then
  UNIT="app-org.fcitx.Fcitx5@autostart.service"
  if systemctl --user is-active --quiet "$UNIT"; then
    systemctl --user restart "$UNIT" && log "restarted: $UNIT"
  else
    log "fcitx5 未由 systemd 用户服务托管，请手动重启 fcitx5 使词典生效"
  fi
else
  log "提示：词典在 addon 加载时读取，需重启 fcitx5 生效"
fi
