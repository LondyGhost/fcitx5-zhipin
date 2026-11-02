#!/usr/bin/env python3
"""从 rime-ice 字库生成 C++ 音节表（syllables.inc）。

只取「单字 + 单音节码」的词条对应的码，排序去重。
这些码就是合法拼音音节，用于把输入缓冲切成音节。
"""
import glob
import os
import sys

BASE = sys.argv[1] if len(sys.argv) > 1 else "."
OUT = sys.argv[2] if len(sys.argv) > 2 else "syllables.inc"

syllables = set()
for path in sorted(glob.glob(os.path.join(BASE, "cn_dicts", "*.dict.yaml"))):
    in_body = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not in_body:
                if line.strip() == "...":
                    in_body = True
                continue
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            text, code = parts[0], parts[1].strip()
            if len(text) != 1 or not code or " " in code:
                continue
            if not code.isascii() or not code.isalpha() or not code.islower():
                continue
            if len(code) > 6:
                continue
            syllables.add(code)

if not syllables:
    sys.exit("no syllables found")

lines = ["// 自动生成：由 rime-ice 字库提取的合法拼音音节表，请勿手改。",
         "// 生成脚本：tools/gen_syllables.py",
         "static const char* const kSyllables[] = {"]
row = []
for s in sorted(syllables):
    row.append('"%s"' % s)
    if len(row) == 10:
        lines.append("    " + ", ".join(row) + ",")
        row = []
if row:
    lines.append("    " + ", ".join(row) + ",")
lines.append("};")
lines.append("")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
sys.stderr.write("wrote %s: %d syllables\n" % (OUT, len(syllables)))
