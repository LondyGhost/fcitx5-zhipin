#!/usr/bin/env python3
"""把 rime 加进 fcitx5 输入法组并设为默认，保留原有条目以便随时切回。

用法: switch_to_rime.py <profile> [--revert]
"""
import re
import sys

path = sys.argv[1]
revert = "--revert" in sys.argv

with open(path, encoding="utf-8") as f:
    text = f.read()

if revert:
    text = re.sub(r"^DefaultIM=.*$", "DefaultIM=abc", text, count=1, flags=re.M)
    text = re.sub(r"\n\[Groups/0/Items/\d+\]\n(?:#[^\n]*\n)*Name=rime\n", "\n", text)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print("reverted: DefaultIM=abc, rime removed")
    sys.exit(0)

# 已有 rime 条目就不重复加
if re.search(r"^Name=rime$", text, flags=re.M):
    print("rime already present")
else:
    idx = 0
    while re.search(r"^\[Groups/0/Items/%d\]$" % idx, text, flags=re.M):
        idx += 1
    block = "\n[Groups/0/Items/%d]\n# Name\nName=rime\n# Layout\n# Layout=\n" % idx
    # 插到 [GroupOrder] 之前
    marker = "\n[GroupOrder]"
    if marker in text:
        text = text.replace(marker, block + marker, 1)
    else:
        text = text.rstrip("\n") + "\n" + block
    print("added rime as Items/%d" % idx)

text = re.sub(r"^DefaultIM=.*$", "DefaultIM=rime", text, count=1, flags=re.M)
with open(path, "w", encoding="utf-8") as f:
    f.write(text)
print("DefaultIM=rime")
