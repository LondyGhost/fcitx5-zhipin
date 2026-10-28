#!/usr/bin/env bash
# test_install_uninstall.sh — A5 部署测试套件（临时 HOME/DESTDIR，A5 拥有）。
#
# 覆盖：正常安装、重复安装、内容变更升级、外部文件冲突与备份恢复、
# 卸载（含 --restore-backups / --force-modified / 仅备份清单状态）、
# no-overwrite 配置模板、--verify 校验、staging 组装（build-staging.sh）、
# 缺 staging、空 staging、前缀安全拒绝（/、/usr、/opt）、无权限、
# 中途失败回滚（不可读文件 / 父路径被文件占用）、路径冲突、清单前缀不一致、
# dry-run、路径含空格、升级不删除旧文件、最终路径清单审计。
# 全部操作只发生在 mktemp 沙箱内；不读凭据；不写 /usr、/opt。
set -u -o pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT="${ABC_PROJECT_SOURCE_DIR:-$(cd -- "$HERE/../.." && pwd -P)}"
INSTALL="$PROJECT/scripts/install-user.sh"
UNINSTALL="$PROJECT/scripts/uninstall-user.sh"
BUILD="$PROJECT/scripts/build-abc.sh"
STAGING_SH="$PROJECT/scripts/build-staging.sh"
CONFIG_TEMPLATE="$PROJECT/deploy/abc/config.ini"

PASS=0
FAIL=0
SKIP=0
ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }
skip() { SKIP=$((SKIP+1)); printf '  skip - %s\n' "$1"; }
assert_eq() { [ "$1" = "$2" ] && ok "$3" || fail "$3（期望 [$2]，实际 [$1]）"; }
assert_file()    { [ -f "$1" ] && ok "$2" || fail "$2"; }
assert_no_file() { [ ! -e "$1" ] && ok "$2" || fail "$2"; }
assert_dir()     { [ -d "$1" ] && ok "$2" || fail "$2"; }
assert_no_dir()  { [ ! -d "$1" ] && ok "$2" || fail "$2"; }
assert_contains() { printf '%s' "$1" | grep -qF -- "$2" && ok "$3" || fail "$3"; }
assert_not_contains() { printf '%s' "$1" | grep -qF -- "$2" && fail "$3" || ok "$3"; }
assert_contains_grep() { printf '%s' "$1" | grep -qE -- "$2" && ok "$3" || fail "$3"; }
sha_of() { sha256sum -- "$1" | cut -d' ' -f1; }

SBOX="$(mktemp -d "${TMPDIR:-/tmp}/abc-deploy-test.XXXXXX")" || exit 1
# Windows/MSYS 下 mktemp 可能返回混合路径；脚本要求 POSIX 绝对路径
case "$SBOX" in
  /*) ;;
  *) if command -v cygpath >/dev/null 2>&1; then SBOX="$(cygpath -u -- "$SBOX")"
     else echo "需要 POSIX 路径的 TMPDIR（当前: $SBOX）" >&2; exit 1; fi ;;
esac
trap 'rm -rf -- "$SBOX"' EXIT

# 权限类测试在 root 或不强制权限的文件系统上无效 → 自动跳过
IS_ROOT=0
[ "$(id -u)" = "0" ] && IS_ROOT=1
PERMISSIONS_WORK=1
probe="$SBOX/.permprobe"; mkdir -p -- "$probe"
chmod 555 -- "$probe" 2>/dev/null
if [ -w "$probe" ]; then PERMISSIONS_WORK=0; fi
chmod 755 -- "$probe" 2>/dev/null

make_staging() {  # $1 = staging 目录；标准布局 fixture（含空格路径与模板）
  local d=$1
  mkdir -p -- "$d/.local/lib/fcitx5" "$d/.local/share/abc" \
             "$d/.config/abc" "$d/.local/share/app dir with space"
  printf 'lib-v1\n'           > "$d/.local/lib/fcitx5/abc-addon.so"
  printf 'index-v1\n'         > "$d/.local/share/abc/index.abcdix"
  printf 'template\n'         > "$d/.config/abc/config.ini"
  printf 'entry\n'            > "$d/.local/share/app dir with space/desktop entry.txt"
  printf '.config/abc/config.ini\n' > "$d/.abc-im-no-overwrite.list"
}

make_addon_prefix() {  # $1 = 模拟 cmake --install 产出的前缀树（A4 布局）
  local d=$1
  mkdir -p -- "$d/lib/fcitx5" "$d/share/fcitx5/addon" "$d/share/fcitx5/inputmethod"
  printf 'so\n'     > "$d/lib/fcitx5/abc.so"
  printf 'addon\n'  > "$d/share/fcitx5/addon/abc.conf"
  printf 'im\n'     > "$d/share/fcitx5/inputmethod/abc.conf"
}

prefix_manifest() { printf '%s/.abc-im/manifest.tsv' "$1"; }

snapshot() {  # $1 = 目录；相对路径+类型排序清单，用于最终审计
  (cd -- "$1" 2>/dev/null && find . -mindepth 1 -printf '%y %P\n' | LC_ALL=C sort) || true
}

echo "=== A5 部署测试套件（沙箱: $SBOX）"

# ---------------------------------------------------------------- 1 正常安装（前缀含空格）
t1_prefix="$SBOX/user prefix one"
t1_stage="$SBOX/stage1"
make_staging "$t1_stage"
out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$t1_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "1.1 正常安装退出码 0"
assert_file "$t1_prefix/.local/lib/fcitx5/abc-addon.so" "1.2 addon 文件已安装"
assert_file "$t1_prefix/.local/share/abc/index.abcdix" "1.3 索引文件已安装"
assert_file "$t1_prefix/.local/share/app dir with space/desktop entry.txt" "1.4 含空格路径已安装"
assert_file "$t1_prefix/.config/abc/config.ini" "1.5 配置模板已安装（目标不存在）"
assert_file "$(prefix_manifest "$t1_prefix")" "1.6 清单已生成"
assert_no_file "$t1_prefix/.abc-im-no-overwrite.list" "1.7 控制文件本身不安装"
assert_contains "$out" "新建 4" "1.8 计数：新建 4 个文件"
m="$(prefix_manifest "$t1_prefix")"
grep -qF $'F\t.local/lib/fcitx5/abc-addon.so\t' "$m" && ok "1.9 清单 F 条目存在" || fail "1.9 清单 F 条目存在"
installed_sha="$(sha256sum -- "$t1_prefix/.local/share/abc/index.abcdix" | awk '{print $1}')"
manifest_sha="$(awk -F'\t' '$1=="F" && $2==".local/share/abc/index.abcdix" {print $3}' "$m")"
assert_eq "$manifest_sha" "$installed_sha" "1.10 清单 sha 与实际文件一致"
assert_contains "$(head -n 5 "$m")" "# prefix: $t1_prefix" "1.11 清单登记前缀"

# ---------------------------------------------------------------- 2 重复安装（幂等）
out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$t1_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "2.1 重复安装退出码 0"
assert_contains "$out" "跳过 1" "2.2 no-overwrite 模板继续跳过"
n_backup="$(find "$t1_prefix/.abc-im/backup" -type f 2>/dev/null | wc -l)"
assert_eq "$n_backup" "0" "2.3 重复安装不积累备份"
assert_file "$t1_prefix/.local/lib/fcitx5/abc-addon.so" "2.4 文件仍在"

# ---------------------------------------------------------------- 3 内容变更升级
printf 'index-v2\n' > "$t1_stage/.local/share/abc/index.abcdix"
out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$t1_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "3.1 内容变更升级退出码 0"
assert_contains "$(cat "$t1_prefix/.local/share/abc/index.abcdix")" "index-v2" "3.2 内容已更新"
m="$(prefix_manifest "$t1_prefix")"
manifest_sha="$(awk -F'\t' '$1=="F" && $2==".local/share/abc/index.abcdix" {print $3}' "$m")"
file_sha="$(sha256sum -- "$t1_prefix/.local/share/abc/index.abcdix" | awk '{print $1}')"
assert_eq "$manifest_sha" "$file_sha" "3.3 清单 sha 已同步"
n_backup="$(find "$t1_prefix/.abc-im/backup" -type f 2>/dev/null | wc -l)"
assert_eq "$n_backup" "0" "3.4 本项目文件更新不留备份"

# ---------------------------------------------------------------- 4 升级不删除旧清单文件
printf 'only-v1\n' > "$t1_stage/.local/share/abc/legacy.txt"
bash "$INSTALL" --staging "$t1_stage" --prefix "$t1_prefix" >/dev/null 2>&1
rm -f -- "$t1_stage/.local/share/abc/legacy.txt"
out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$t1_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "4.1 缺文件的新 staging 安装成功"
assert_file "$t1_prefix/.local/share/abc/legacy.txt" "4.2 旧文件保留（升级不删除）"
grep -qF $'F\t.local/share/abc/legacy.txt\t' "$(prefix_manifest "$t1_prefix")" \
  && ok "4.3 旧文件仍在清单登记" || fail "4.3 旧文件仍在清单登记"

# ---------------------------------------------------------------- 5 外部冲突：备份+覆盖；卸载保留备份；之后可恢复
t5_prefix="$SBOX/user prefix five"
t5_stage="$SBOX/stage5"
make_staging "$t5_stage"
mkdir -p -- "$t5_prefix/.local/share/abc"
printf 'user-original\n' > "$t5_prefix/.local/share/abc/index.abcdix"
out="$(bash "$INSTALL" --staging "$t5_stage" --prefix "$t5_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "5.1 冲突安装退出码 0"
assert_contains "$out" "已备份" "5.2 输出含备份提示"
assert_contains "$(cat "$t5_prefix/.abc-im/backup/.local/share/abc/index.abcdix")" \
  "user-original" "5.3 备份内容为用户原文件"
assert_contains "$(cat "$t5_prefix/.local/share/abc/index.abcdix")" "index-v1" "5.4 我方文件已覆盖安装"
m="$(prefix_manifest "$t5_prefix")"
awk -F'\t' '$1=="B" && $2==".local/share/abc/index.abcdix" {found=1} END{exit !found}' "$m" \
  && ok "5.5 清单 B 条目登记" || fail "5.5 清单 B 条目登记"

# 卸载（默认保留备份）→ 清单转为"仅备份"状态，备份仍可追溯
out="$(bash "$UNINSTALL" --prefix "$t5_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "5.6 卸载退出码 0"
assert_no_file "$t5_prefix/.local/share/abc/index.abcdix" "5.7 我方文件已删除"
assert_file "$t5_prefix/.abc-im/backup/.local/share/abc/index.abcdix" "5.8 默认保留备份"
assert_file "$(prefix_manifest "$t5_prefix")" "5.9 有未恢复备份时清单保留（可追溯）"
assert_contains "$(cat "$(prefix_manifest "$t5_prefix")")" "uninstalled-backups-pending" \
  "5.10 清单标记为仅备份状态"
awk -F'\t' '$1=="F" {found=1} END{exit !found}' "$(prefix_manifest "$t5_prefix")" \
  && fail "5.11 仅备份清单不含 F 条目" || ok "5.11 仅备份清单不含 F 条目"

# 恢复备份（无需重新安装）
out="$(bash "$UNINSTALL" --prefix "$t5_prefix" --restore-backups 2>&1)"; rc=$?
assert_eq "$rc" "0" "5.12 卸载后 --restore-backups 退出码 0"
assert_contains "$(cat "$t5_prefix/.local/share/abc/index.abcdix")" "user-original" \
  "5.13 用户原文件已恢复"
assert_no_dir "$t5_prefix/.abc-im" "5.14 备份目录与清单已清理"
out="$(bash "$UNINSTALL" --prefix "$t5_prefix" --restore-backups 2>&1)"; rc=$?
assert_eq "$rc" "1" "5.15 无清单时再次恢复得到明确错误"

# ---------------------------------------------------------------- 6 修改过的本项目文件：默认跳过，--force 删除
t6_prefix="$SBOX/user6"
t6_stage="$SBOX/stage6"
make_staging "$t6_stage"
bash "$INSTALL" --staging "$t6_stage" --prefix "$t6_prefix" >/dev/null 2>&1
printf 'useredit\n' > "$t6_prefix/.local/lib/fcitx5/abc-addon.so"
out="$(bash "$UNINSTALL" --prefix "$t6_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "2" "6.1 含修改文件的卸载退出码 2"
assert_contains "$out" "安装后被修改" "6.2 警告指明跳过原因"
assert_file "$t6_prefix/.local/lib/fcitx5/abc-addon.so" "6.3 修改文件未被删除"
assert_contains "$(cat "$(prefix_manifest "$t6_prefix")" 2>/dev/null)" "uninstall-incomplete" \
  "6.3b 未删除文件保留登记（清单不丢）"
out="$(bash "$UNINSTALL" --prefix "$t6_prefix" --force-modified 2>&1)"; rc=$?
assert_eq "$rc" "0" "6.4 --force-modified 卸载退出码 0"
assert_no_file "$t6_prefix/.local/lib/fcitx5/abc-addon.so" "6.5 修改文件已强制删除"

# ---------------------------------------------------------------- 7 no-overwrite：绝不覆盖用户配置
t7_prefix="$SBOX/user7"
t7_stage="$SBOX/stage7"
make_staging "$t7_stage"
mkdir -p -- "$t7_prefix/.config/abc"
printf 'my-own-config\n' > "$t7_prefix/.config/abc/config.ini"
out="$(bash "$INSTALL" --staging "$t7_stage" --prefix "$t7_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "7.1 存在用户配置时安装退出码 0"
assert_contains "$(cat "$t7_prefix/.config/abc/config.ini")" "my-own-config" "7.2 用户配置原样保留"
assert_contains "$out" "跳过 1" "7.3 计数含跳过"
# 用户配置未被登记为 F（卸载不会删除它）
grep -qF $'F\t.config/abc/config.ini\t' "$(prefix_manifest "$t7_prefix")" \
  && fail "7.4 跳过的用户配置不登记为 F" || ok "7.4 跳过的用户配置不登记为 F"
out="$(bash "$UNINSTALL" --prefix "$t7_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "7.5 卸载退出码 0"
assert_contains "$(cat "$t7_prefix/.config/abc/config.ini")" "my-own-config" "7.6 卸载不删除用户配置"

# ---------------------------------------------------------------- 8 错误输入：缺 staging / 空 staging / 未知参数 / 无清单校验
t8_prefix="$SBOX/user8"
out="$(bash "$INSTALL" --staging "$SBOX/no-such-staging" --prefix "$t8_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "8.1 缺 staging 退出码 1"
assert_no_dir "$t8_prefix/.abc-im" "8.2 失败后无任何残留"
mkdir -p -- "$SBOX/empty-stage"
out="$(bash "$INSTALL" --staging "$SBOX/empty-stage" --prefix "$t8_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "8.3 空 staging 退出码 1"
out="$(bash "$INSTALL" --staging "$SBOX/empty-stage" --bogus 2>&1)"; rc=$?
assert_eq "$rc" "1" "8.4 未知参数退出码 1"
out="$(bash "$INSTALL" --verify --prefix "$SBOX/never-installed" 2>&1)"; rc=$?
assert_eq "$rc" "1" "8.5 --verify 无清单退出码 1"
assert_no_dir "$SBOX/never-installed/.abc-im" "8.6 --verify 不创建任何目录"

# ---------------------------------------------------------------- 9 前缀安全拒绝
for bad in / /usr /opt /usr/local /opt/abc; do
  out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$bad" 2>&1)"; rc=$?
  assert_eq "$rc" "1" "9.x 前缀 $bad 被拒绝"
done
[ ! -e "/opt/.abc-im" ] && ok "9.y /opt 无本项目痕迹" || fail "9.y /opt 无本项目痕迹"
[ ! -e "/usr/.abc-im" ] && ok "9.z /usr 无本项目痕迹" || fail "9.z /usr 无本项目痕迹"

# ---------------------------------------------------------------- 10 无权限（沙箱内模拟）
if [ "$IS_ROOT" = "0" ] && [ "$PERMISSIONS_WORK" = "1" ]; then
  t10_prefix="$SBOX/user10"
  t10_stage="$SBOX/stage10"
  make_staging "$t10_stage"
  mkdir -p -- "$t10_prefix/.local/share/abc"
  chmod 555 -- "$t10_prefix/.local/share/abc"
  out="$(bash "$INSTALL" --staging "$t10_stage" --prefix "$t10_prefix" 2>&1)"; rc=$?
  assert_eq "$rc" "2" "10.1 无权限目录安装退出码 2（回滚）"
  assert_contains "$out" "回滚" "10.2 输出说明回滚"
  chmod 755 -- "$t10_prefix/.local/share/abc"
  assert_no_file "$t10_prefix/.local/share/abc/index.abcdix" "10.3 无权限目录内未落盘"
  assert_no_dir "$t10_prefix/.abc-im" "10.4 回滚后无管理目录残留"
else
  skip "10 无权限测试（root 或文件系统不强制权限）"
fi

# ---------------------------------------------------------------- 11 中途失败：不可读 staging 文件 → 回滚
if [ "$IS_ROOT" = "0" ] && [ "$PERMISSIONS_WORK" = "1" ]; then
  t11_prefix="$SBOX/user11"
  t11_stage="$SBOX/stage11"
  mkdir -p -- "$t11_stage/.local/share/abc"
  printf 'aaa\n' > "$t11_stage/.local/share/abc/a.txt"
  printf 'bbb\n' > "$t11_stage/.local/share/abc/b.txt"
  printf 'ccc\n' > "$t11_stage/.local/share/abc/c.txt"
  chmod 000 -- "$t11_stage/.local/share/abc/b.txt"
  out="$(bash "$INSTALL" --staging "$t11_stage" --prefix "$t11_prefix" 2>&1)"; rc=$?
  chmod 644 -- "$t11_stage/.local/share/abc/b.txt"
  assert_eq "$rc" "2" "11.1 中途失败退出码 2"
  assert_no_file "$t11_prefix/.local/share/abc/a.txt" "11.2 已安装文件被回滚"
  assert_no_file "$t11_prefix/.local/share/abc/c.txt" "11.3 后续文件未安装"
  assert_no_dir "$t11_prefix/.abc-im" "11.4 管理目录已回滚"
else
  skip "11 中途失败回滚测试（root 或文件系统不强制权限）"
fi

# ---------------------------------------------------------------- 12 冲突与中途失败（无需权限）
# 12a 父路径被文件占用 → 计划期拒绝，零变更
t12_prefix="$SBOX/user12"
t12_stage="$SBOX/stage12"
make_staging "$t12_stage"
mkdir -p -- "$t12_prefix"
printf 'i-am-a-file\n' > "$t12_prefix/.local"
out="$(bash "$INSTALL" --staging "$t12_stage" --prefix "$t12_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "12.1 父路径被文件占用时计划期拒绝（退出码 1）"
assert_contains "$(cat "$t12_prefix/.local")" "i-am-a-file" "12.2 用户原有文件未被破坏"
assert_no_file "$t12_prefix/.config/abc/config.ini" "12.3 其余文件未落盘"
assert_no_dir "$t12_prefix/.abc-im" "12.4 计划期拒绝无任何变更"

# 12b 管理目录被文件占用 → 执行期失败并回滚（无需权限）
t12b_prefix="$SBOX/user12b"
t12b_stage="$SBOX/stage12b"
make_staging "$t12b_stage"
mkdir -p -- "$t12b_prefix"
printf 'not-a-dir\n' > "$t12b_prefix/.abc-im"
out="$(bash "$INSTALL" --staging "$t12b_stage" --prefix "$t12b_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "2" "12.5 管理目录被文件占用时退出码 2（回滚）"
assert_contains "$out" "回滚" "12.6 输出说明已回滚"
assert_contains "$(cat "$t12b_prefix/.abc-im")" "not-a-dir" "12.7 用户原有文件未被破坏"
assert_no_file "$t12b_prefix/.local/lib/fcitx5/abc-addon.so" "12.8 未留下已安装文件"
assert_no_dir "$t12b_prefix/.local" "12.9 回滚删除本次创建的目录"

# ---------------------------------------------------------------- 13 路径冲突：目标应为文件但被目录占用
t13_prefix="$SBOX/user13"
t13_stage="$SBOX/stage13"
make_staging "$t13_stage"
mkdir -p -- "$t13_prefix/.local/lib/fcitx5/abc-addon.so"
out="$(bash "$INSTALL" --staging "$t13_stage" --prefix "$t13_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "13.1 目标被目录占用时计划期拒绝（退出码 1）"
assert_contains "$out" "目录占用" "13.2 错误信息指明冲突类型"
assert_no_dir "$t13_prefix/.abc-im" "13.3 冲突时未做任何变更"

# ---------------------------------------------------------------- 14 dry-run
t14_prefix="$SBOX/user14"
out="$(bash "$INSTALL" --staging "$t1_stage" --prefix "$t14_prefix" --dry-run 2>&1)"; rc=$?
assert_eq "$rc" "0" "14.1 dry-run 退出码 0"
assert_contains_grep "$out" '\[新建\]' "14.2 计划列出新建项"
assert_no_dir "$t14_prefix/.abc-im" "14.3 dry-run 无任何变更"
assert_no_file "$t14_prefix/.local/lib/fcitx5/abc-addon.so" "14.4 dry-run 未安装文件"

# ---------------------------------------------------------------- 15 卸载后路径审计（最终变更清单）
t15_prefix="$SBOX/user15"
t15_stage="$SBOX/stage15"
make_staging "$t15_stage"
mkdir -p -- "$t15_prefix/preexisting"
printf 'keep\n' > "$t15_prefix/preexisting/keepme.txt"
before="$(snapshot "$t15_prefix")"
bash "$INSTALL" --staging "$t15_stage" --prefix "$t15_prefix" >/dev/null 2>&1
bash "$UNINSTALL" --prefix "$t15_prefix" >/dev/null 2>&1
after="$(snapshot "$t15_prefix")"
assert_eq "$after" "$before" "15.1 卸载后目录状态与安装前一致（路径清单审计）"
assert_file "$t15_prefix/preexisting/keepme.txt" "15.2 用户原有文件未受影响"

# ---------------------------------------------------------------- 16 build-abc.sh 基本行为
out="$(bash "$BUILD" --help 2>&1)"; rc=$?
assert_eq "$rc" "0" "16.1 build --help 退出码 0"
out="$(bash "$BUILD" --bogus-flag 2>&1)"; rc=$?
assert_eq "$rc" "1" "16.2 build 未知参数退出码 1"
out="$(bash "$BUILD" --prefix /opt --no-tests 2>&1)"; rc=$?
assert_eq "$rc" "1" "16.3 build 拒绝 /opt 前缀"
out="$(bash "$BUILD" --staging /opt/x --no-tests 2>&1)"; rc=$?
assert_eq "$rc" "1" "16.4 build 拒绝 /opt staging"

# ---------------------------------------------------------------- 17 --verify 校验模式
t17_prefix="$SBOX/user17"
t17_stage="$SBOX/stage17"
make_staging "$t17_stage"
bash "$INSTALL" --staging "$t17_stage" --prefix "$t17_prefix" >/dev/null 2>&1
out="$(bash "$INSTALL" --verify --prefix "$t17_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "17.1 完整安装校验通过（退出码 0）"
assert_contains "$out" "校验通过" "17.2 输出校验通过"
printf 'tampered\n' > "$t17_prefix/.local/lib/fcitx5/abc-addon.so"
out="$(bash "$INSTALL" --verify --prefix "$t17_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "2" "17.3 文件被修改时校验失败（退出码 2）"
assert_contains "$out" "内容与清单不符" "17.4 指出内容不符"
rm -f -- "$t17_prefix/.local/share/abc/index.abcdix"
out="$(bash "$INSTALL" --verify --prefix "$t17_prefix" 2>&1)"; rc=$?
assert_contains "$out" "缺失文件" "17.5 指出缺失文件"
assert_no_dir "$t17_prefix/.abc-im/verify-tmp" "17.6 校验不产生临时残留"

# ---------------------------------------------------------------- 18 staging 组装（build-staging.sh）
t18_root="$SBOX/stage asm"
t18_from="$t18_root/prefix tree"
t18_out="$t18_root/out tree"
make_addon_prefix "$t18_from"
out="$(bash "$STAGING_SH" --from "$t18_from" --out "$t18_out" 2>&1)"; rc=$?
assert_eq "$rc" "0" "18.1 组装退出码 0（路径含空格）"
assert_file "$t18_out/.local/lib/fcitx5/abc.so" "18.2 addon 映射到 .local/lib/fcitx5"
assert_file "$t18_out/.local/share/fcitx5/addon/abc.conf" "18.3 addon 描述已映射"
assert_file "$t18_out/.local/share/fcitx5/inputmethod/abc.conf" "18.4 输入法条目已映射"
assert_file "$t18_out/.config/abc/config.ini" "18.5 配置模板已放入 staging"
assert_contains "$(cat "$t18_out/.abc-im-no-overwrite.list")" ".config/abc/config.ini" \
  "18.6 控制文件登记配置模板"
assert_eq "$(cat "$t18_out/.config/abc/config.ini")" "$(cat "$CONFIG_TEMPLATE")" \
  "18.7 模板内容与 deploy/abc/config.ini 一致"

# 组装产物 → 安装 → 路径与 A4 清单一致
t18_prefix="$SBOX/user18"
out="$(bash "$INSTALL" --staging "$t18_out" --prefix "$t18_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "18.8 组装产物可安装（退出码 0）"
assert_file "$t18_prefix/.local/lib/fcitx5/abc.so" "18.9 安装后 addon 位于 \$HOME/.local/lib/fcitx5"
assert_file "$t18_prefix/.local/share/fcitx5/addon/abc.conf" "18.10 安装后 addon 描述就位"
assert_file "$t18_prefix/.config/abc/config.ini" "18.11 安装后配置位于 \$HOME/.config/abc"

# 重复组装需 --clean；否则明确拒绝
printf 'stale\n' > "$t18_out/stale-file"
out="$(bash "$STAGING_SH" --from "$t18_from" --out "$t18_out" 2>&1)"; rc=$?
assert_eq "$rc" "1" "18.12 非空 --out 未加 --clean 时拒绝（退出码 1）"
out="$(bash "$STAGING_SH" --from "$t18_from" --out "$t18_out" --clean 2>&1)"; rc=$?
assert_eq "$rc" "0" "18.13 --clean 可重复组装"
assert_no_file "$t18_out/stale-file" "18.14 重建后无残留文件"

# A4 产物缺失：默认失败并给出原因
t18_empty="$t18_root/empty prefix"
mkdir -p -- "$t18_empty"
out="$(bash "$STAGING_SH" --from "$t18_empty" --out "$t18_root/out2" 2>&1)"; rc=$?
assert_eq "$rc" "2" "18.15 空前缀树组装失败（退出码 2）"
assert_contains "$out" "为空目录" "18.16 指出前缀树为空"
t18_partial="$t18_root/partial prefix"
mkdir -p -- "$t18_partial/lib/fcitx5"
printf 'so\n' > "$t18_partial/lib/fcitx5/abc.so"
out="$(bash "$STAGING_SH" --from "$t18_partial" --out "$t18_root/out3" 2>&1)"; rc=$?
assert_eq "$rc" "2" "18.17 缺 addon 描述时组装失败（退出码 2）"
assert_contains "$out" "A4 产物缺失" "18.18 指出缺失的 A4 产物"
assert_no_dir "$t18_root/out3" "18.19 失败后不留下半成品 staging"
out="$(bash "$STAGING_SH" --from "$t18_partial" --out "$t18_root/out4" \
      --allow-missing-addon 2>&1)"; rc=$?
assert_eq "$rc" "0" "18.20 --allow-missing-addon 仅组装配置（退出码 0）"
assert_contains "$out" "A4 产物缺失" "18.21 明确警告缺失产物"
assert_file "$t18_root/out4/.config/abc/config.ini" "18.22 仅配置模式下模板仍就位"
# 安全拒绝
out="$(bash "$STAGING_SH" --from "$t18_from" --out /opt/x 2>&1)"; rc=$?
assert_eq "$rc" "1" "18.23 拒绝 /opt 作为 --out"
if [ -n "${HOME:-}" ]; then
  out="$(bash "$STAGING_SH" --from "$t18_from" --out "$HOME" 2>&1)"; rc=$?
  assert_eq "$rc" "1" "18.24 拒绝 \$HOME 作为 --out"
fi
# 符号链接拒绝
t18_link="$t18_root/link prefix"
mkdir -p -- "$t18_link"
ln -s /tmp "$t18_link/lib" 2>/dev/null
if [ -L "$t18_link/lib" ]; then
  out="$(bash "$STAGING_SH" --from "$t18_link" --out "$t18_root/out5" 2>&1)"; rc=$?
  assert_eq "$rc" "2" "18.25 前缀树含符号链接时拒绝（退出码 2）"
else
  skip "18.25 符号链接测试（无法创建符号链接）"
fi

# ---------------------------------------------------------------- 19 清单前缀不一致
t19_prefix="$SBOX/user19"
t19_other="$SBOX/user19-moved"
t19_stage="$SBOX/stage19"
make_staging "$t19_stage"
bash "$INSTALL" --staging "$t19_stage" --prefix "$t19_prefix" >/dev/null 2>&1
sed -i "s|^# prefix: .*|# prefix: $t19_other|" "$(prefix_manifest "$t19_prefix")"
out="$(bash "$UNINSTALL" --prefix "$t19_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "19.1 清单前缀不一致时拒绝卸载（退出码 1）"
assert_contains "$out" "不一致" "19.2 指出前缀不一致"
assert_file "$t19_prefix/.local/lib/fcitx5/abc-addon.so" "19.3 拒绝后文件仍在"
out="$(bash "$UNINSTALL" --prefix "$t19_prefix" --allow-prefix-mismatch 2>&1)"; rc=$?
assert_eq "$rc" "0" "19.4 --allow-prefix-mismatch 可继续卸载"
assert_no_file "$t19_prefix/.local/lib/fcitx5/abc-addon.so" "19.5 文件已删除"

# ---------------------------------------------------------------- 20 staging 含符号链接拒绝安装
t20_stage="$SBOX/stage20"
t20_prefix="$SBOX/user20"
mkdir -p -- "$t20_stage/.local/share"
ln -s /tmp "$t20_stage/.local/share/linkdir" 2>/dev/null
if [ -L "$t20_stage/.local/share/linkdir" ]; then
  out="$(bash "$INSTALL" --staging "$t20_stage" --prefix "$t20_prefix" 2>&1)"; rc=$?
  assert_eq "$rc" "1" "20.1 staging 含符号链接时拒绝安装（退出码 1）"
  assert_no_dir "$t20_prefix/.abc-im" "20.2 拒绝后无任何变更"
else
  skip "20 staging 符号链接测试（无法创建符号链接）"
fi

# ---------------------------------------------------------------- 21 用户配置保护
# 21a 已存在的用户配置：安装不覆盖，卸载不删除，内容逐字节不变
t21_prefix="$SBOX/user21"
t21_stage="$SBOX/stage21"
make_staging "$t21_stage"
mkdir -p -- "$t21_prefix/.config/abc"
printf 'initialMode = english\npageSize = 5\n# 用户自己的配置\n' > "$t21_prefix/.config/abc/config.ini"
cfg_before="$(sha_of "$t21_prefix/.config/abc/config.ini")"
out="$(bash "$INSTALL" --staging "$t21_stage" --prefix "$t21_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "21.1 已有用户配置时安装退出码 0"
assert_contains "$out" "跳过 1" "21.2 配置被跳过（不覆盖）"
assert_eq "$(sha_of "$t21_prefix/.config/abc/config.ini")" "$cfg_before" "21.3 安装后用户配置内容不变"
awk -F'\t' '$1=="F" && $2==".config/abc/config.ini" {found=1} END{exit !found}' \
  "$(prefix_manifest "$t21_prefix")" \
  && fail "21.4 跳过的用户配置不登记为 F" || ok "21.4 跳过的用户配置不登记为 F"
out="$(bash "$UNINSTALL" --prefix "$t21_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "21.5 卸载退出码 0"
assert_file "$t21_prefix/.config/abc/config.ini" "21.6 卸载保留用户配置"
assert_eq "$(sha_of "$t21_prefix/.config/abc/config.ini")" "$cfg_before" "21.7 卸载后内容与安装前一致"
assert_no_dir "$t21_prefix/.abc-im" "21.8 清单与备份目录已清理"

# 21b 本项目安装的配置被用户改过：卸载默认保留（不静默删用户数据）
t21b_prefix="$SBOX/user21b"
bash "$INSTALL" --staging "$t21_stage" --prefix "$t21b_prefix" >/dev/null 2>&1
printf 'initialMode = english\n# 用户后来改的\n' > "$t21b_prefix/.config/abc/config.ini"
edited_sha="$(sha_of "$t21b_prefix/.config/abc/config.ini")"
out="$(bash "$UNINSTALL" --prefix "$t21b_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "2" "21.9 用户改过配置时卸载退出码 2（不静默删除）"
assert_contains "$out" "安装后被修改" "21.10 报告跳过原因"
assert_eq "$(sha_of "$t21b_prefix/.config/abc/config.ini")" "$edited_sha" "21.11 用户改动内容完整保留"
assert_contains "$(cat "$(prefix_manifest "$t21b_prefix")" 2>/dev/null)" "uninstall-incomplete" \
  "21.12 未删除文件保留登记"

# ---------------------------------------------------------------- 22 重复安装幂等
t22_prefix="$SBOX/user22"
t22_stage="$SBOX/stage22"
make_staging "$t22_stage"
bash "$INSTALL" --staging "$t22_stage" --prefix "$t22_prefix" >/dev/null 2>&1
m1="$(grep -v '^#' "$(prefix_manifest "$t22_prefix")" | LC_ALL=C sort)"
s1="$(sha_of "$t22_prefix/.local/lib/fcitx5/abc-addon.so")"
out="$(bash "$INSTALL" --staging "$t22_stage" --prefix "$t22_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "22.1 第二次安装退出码 0"
assert_contains "$out" "新建 0" "22.2 第二次安装新建数为 0"
assert_contains "$out" "更新 3" "22.3 第二次安装更新 3 个文件"
assert_contains "$out" "跳过 1" "22.4 第二次安装跳过 1 个配置模板"
assert_contains "$out" "备份保留 0" "22.5 第二次安装不新增备份"
m2="$(grep -v '^#' "$(prefix_manifest "$t22_prefix")" | LC_ALL=C sort)"
assert_eq "$m2" "$m1" "22.6 清单条目（F/D/B 与 sha）完全一致"
assert_eq "$(sha_of "$t22_prefix/.local/lib/fcitx5/abc-addon.so")" "$s1" "22.7 文件内容未变化"
n_bak="$(find "$t22_prefix/.abc-im/backup" -type f 2>/dev/null | wc -l | tr -d ' ')"
assert_eq "$n_bak" "0" "22.8 备份目录为空"
assert_no_dir "$t22_prefix/.abc-im/rollback" "22.9 无回滚副本残留"
assert_eq "$(ls -A -- "$t22_prefix/.abc-im" | LC_ALL=C sort | tr '\n' ' ')" "manifest.tsv " \
  "22.10 .abc-im 只保留清单"

# ---------------------------------------------------------------- 23 dry-run 准确性
t23_prefix="$SBOX/user23"
t23_stage="$SBOX/stage23"
make_staging "$t23_stage"
out="$(bash "$INSTALL" --staging "$t23_stage" --prefix "$t23_prefix" --dry-run 2>&1)"; rc=$?
assert_eq "$rc" "0" "23.1 首次 dry-run 退出码 0"
assert_contains "$out" "新建 4" "23.2 首次计划：新建 4"
assert_contains "$out" "更新 0" "23.3 首次计划：更新 0"
assert_contains "$out" "跳过 0" "23.4 首次计划：跳过 0"
assert_eq "$(printf '%s\n' "$out" | grep -c '\[新建\]')" "4" "23.5 计划逐项列出 4 个新建"
assert_no_dir "$t23_prefix" "23.6 dry-run 不创建前缀目录"

bash "$INSTALL" --staging "$t23_stage" --prefix "$t23_prefix" >/dev/null 2>&1
snap_before="$(snapshot "$t23_prefix")"
out="$(bash "$INSTALL" --staging "$t23_stage" --prefix "$t23_prefix" --dry-run 2>&1)"; rc=$?
assert_eq "$rc" "0" "23.7 已安装后 dry-run 退出码 0"
assert_contains "$out" "新建 0" "23.8 已安装后计划：新建 0"
assert_contains "$out" "更新 3" "23.9 已安装后计划：更新 3"
assert_contains "$out" "跳过 1" "23.10 已安装后计划：跳过 1"
assert_eq "$(printf '%s\n' "$out" | grep -c '\[更新-本项目\]')" "3" "23.11 计划逐项列出 3 个更新"
assert_eq "$(snapshot "$t23_prefix")" "$snap_before" "23.12 dry-run 未修改文件系统（目录快照一致）"

# ---------------------------------------------------------------- 24 --restore-backups 失败处理
# 24a 备份缺失：必须报错并返回非零，不得报告成功
t24_prefix="$SBOX/user24"
t24_stage="$SBOX/stage24"
make_staging "$t24_stage"
mkdir -p -- "$t24_prefix/.local/share/abc"
printf 'user-original\n' > "$t24_prefix/.local/share/abc/index.abcdix"
bash "$INSTALL" --staging "$t24_stage" --prefix "$t24_prefix" >/dev/null 2>&1
rm -f -- "$t24_prefix/.abc-im/backup/.local/share/abc/index.abcdix"
out="$(bash "$UNINSTALL" --prefix "$t24_prefix" --restore-backups 2>&1)"; rc=$?
assert_eq "$rc" "2" "24.1 备份缺失时 --restore-backups 退出码 2"
assert_contains "$out" "备份缺失" "24.2 明确报告备份缺失"
assert_contains "$out" "卸载未完成" "24.3 不报告卸载成功"
assert_not_contains "$out" "卸载完成" "24.4 输出中无成功字样"

# 24b 备份校验值不符：报错、保留备份、不覆盖目标、清单留 restore-incomplete
t24b_prefix="$SBOX/user24b"
mkdir -p -- "$t24b_prefix/.local/share/abc"
printf 'user-original\n' > "$t24b_prefix/.local/share/abc/index.abcdix"
bash "$INSTALL" --staging "$t24_stage" --prefix "$t24b_prefix" >/dev/null 2>&1
bk="$t24b_prefix/.abc-im/backup/.local/share/abc/index.abcdix"
printf 'corrupted-backup\n' > "$bk"
out="$(bash "$UNINSTALL" --prefix "$t24b_prefix" --restore-backups 2>&1)"; rc=$?
assert_eq "$rc" "2" "24.5 备份校验不符时退出码 2"
assert_contains "$out" "校验不符" "24.6 明确报告校验不符"
assert_contains "$out" "卸载未完成" "24.7 不报告卸载成功"
assert_file "$bk" "24.8 不符的备份保留（不删除证据）"
assert_contains "$(cat "$(prefix_manifest "$t24b_prefix")" 2>/dev/null)" "restore-incomplete" \
  "24.9 清单标记 restore-incomplete"
assert_contains "$(cat "$t24b_prefix/.local/share/abc/index.abcdix")" "index-v1" \
  "24.10 目标文件未被损坏的备份覆盖"

# ---------------------------------------------------------------- 25 安装入口与路径边界
# 25a 检测到 A4 参考脚本清单（manifest.list）时拒绝，避免两套清单混用
t25_prefix="$SBOX/user25"
t25_stage="$SBOX/stage25"
make_staging "$t25_stage"
mkdir -p -- "$t25_prefix/.abc-im"
printf '.local/lib/fcitx5/abc.so\n' > "$t25_prefix/.abc-im/manifest.list"
out="$(bash "$INSTALL" --staging "$t25_stage" --prefix "$t25_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "25.1 检测到 A4 清单时拒绝安装（退出码 1）"
assert_contains "$out" "A4 参考脚本" "25.2 指出清单来源与不兼容"
assert_no_file "$(prefix_manifest "$t25_prefix")" "25.3 未写入 manifest.tsv"
out="$(bash "$UNINSTALL" --prefix "$t25_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "1" "25.4 检测到 A4 清单时拒绝卸载（退出码 1）"
assert_contains "$out" "A4 参考脚本" "25.5 卸载同样指出清单来源"

# 25b 正式入口只写前缀内：前缀外无本项目文件，系统目录无痕迹
t25b_root="$SBOX/iso25"
t25b_prefix="$t25b_root/user25b"
t25b_stage="$SBOX/stage25b"
make_staging "$t25b_stage"
mkdir -p -- "$t25b_root/keep"
printf 'keep\n' > "$t25b_root/keep/keepme.txt"
keep_before="$(sha_of "$t25b_root/keep/keepme.txt")"
out="$(bash "$INSTALL" --staging "$t25b_stage" --prefix "$t25b_prefix" 2>&1)"; rc=$?
assert_eq "$rc" "0" "25.6 隔离前缀安装退出码 0"
assert_eq "$(sha_of "$t25b_root/keep/keepme.txt")" "$keep_before" "25.7 前缀外文件未被改动"
assert_eq "$(find "$t25b_root" -name abc.so -not -path "$t25b_prefix/*" 2>/dev/null | wc -l | tr -d ' ')" "0" \
  "25.8 前缀外无 addon 文件"
assert_no_file "/usr/lib/fcitx5/abc.so" "25.9 /usr 无本项目 addon 库"
assert_no_file "/usr/share/fcitx5/addon/abc.conf" "25.10 /usr 无本项目 addon 描述"
assert_no_file "/opt/abc" "25.11 /opt 无本项目目录"

# ---------------------------------------------------------------- 26 FCITX_ADDON_DIRS 安全处理
t26_prefix="$SBOX/user26"
t26_stage="$SBOX/stage26"
make_staging "$t26_stage"
bash "$INSTALL" --staging "$t26_stage" --prefix "$t26_prefix" >/dev/null 2>&1
snap26="$(snapshot "$t26_prefix")"
out="$(env -u FCITX_ADDON_DIRS bash "$INSTALL" --print-env --prefix "$t26_prefix" 2>/dev/null)"; rc=$?
assert_eq "$rc" "0" "26.1 --print-env 退出码 0"
assert_eq "$out" "export FCITX_ADDON_DIRS=\"$t26_prefix/.local/lib/fcitx5:/usr/lib/fcitx5\"" \
  "26.2 未设置时给出「本项目目录 + 系统目录」"
assert_eq "$(snapshot "$t26_prefix")" "$snap26" "26.3 --print-env 不修改文件系统"

out="$(FCITX_ADDON_DIRS="/custom/lib:/usr/lib/fcitx5" bash "$INSTALL" --print-env --prefix "$t26_prefix" 2>/dev/null)"
assert_eq "$out" "export FCITX_ADDON_DIRS=\"$t26_prefix/.local/lib/fcitx5:/custom/lib:/usr/lib/fcitx5\"" \
  "26.4 已有设置被保留，本项目目录前置"

out="$(FCITX_ADDON_DIRS="$t26_prefix/.local/lib/fcitx5:/custom/lib" bash "$INSTALL" --print-env --prefix "$t26_prefix" 2>/dev/null)"
assert_eq "$out" "export FCITX_ADDON_DIRS=\"$t26_prefix/.local/lib/fcitx5:/custom/lib\"" \
  "26.5 已包含本项目目录时不重复追加"

# 不写用户 shell/环境配置
t26_fakehome="$SBOX/fakehome"
mkdir -p -- "$t26_fakehome"
printf 'alias ll="ls -l"\n' > "$t26_fakehome/.bashrc"
home_sha="$(sha_of "$t26_fakehome/.bashrc")"
HOME="$t26_fakehome" bash "$INSTALL" --staging "$t26_stage" --prefix "$SBOX/user26c" >/dev/null 2>&1
assert_eq "$(sha_of "$t26_fakehome/.bashrc")" "$home_sha" "26.6 不修改 ~/.bashrc"
assert_no_dir "$t26_fakehome/.config/environment.d" "26.7 不创建 environment.d 配置"
assert_no_file "$t26_fakehome/.profile" "26.8 不创建 ~/.profile"

# ---------------------------------------------------------------- 27 全局安全审计
[ ! -e "/opt/.abc-im" ] && ok "27.1 /opt 无 .abc-im" || fail "27.1 /opt 无 .abc-im"
[ ! -e "/usr/.abc-im" ] && ok "27.2 /usr 无 .abc-im" || fail "27.2 /usr 无 .abc-im"

# ---------------------------------------------------------------- 汇总
echo "=== 结果: pass=$PASS fail=$FAIL skip=$SKIP"
if [ "$FAIL" -eq 0 ]; then
  echo "abc_deploy_install_uninstall: ALL PASS"
  exit 0
fi
exit 1
