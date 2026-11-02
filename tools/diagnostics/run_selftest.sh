#!/bin/bash
# 运行 GTK 自测并抓取 fcitx5 侧日志（临时诊断脚本）
cd "$HOME/abc-dbg" || exit 1
env -u DISPLAY \
  XDG_RUNTIME_DIR=/run/user/1000 \
  WAYLAND_DISPLAY=wayland-1 \
  XDG_SESSION_TYPE=wayland \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
  LANG=zh_CN.UTF-8 \
  timeout 20 ./gtk_im_selftest 2>&1 | grep -v -e Gdk-WARNING -e '^$' -e 'Event with type'
echo "===JOURNAL==="
journalctl --user -u app-org.fcitx.Fcitx5@autostart.service --since "-2 min" --no-pager \
  | grep -E 'ABCTRACE|KeyEvent:' | tail -30
