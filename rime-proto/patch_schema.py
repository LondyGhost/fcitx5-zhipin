#!/usr/bin/env python3
"""生成 abc.schema.yaml 并把 default.yaml 的 schema_list 只留 abc。

在 rime-ice 方案上只做三处改动：
  1. schema_id / name 改成 abc；
  2. switches 增加 abc_reveal（候选闸门，默认关）；
  3. engine/processors 首位插 lua_processor@*abc_space，
     engine/filters 首位插 lua_filter@*abc_hide。
其余（词库、翻译器、滤镜链）完全沿用 rime-ice，不做任何删减。
"""
import os
import sys

BASE = sys.argv[1] if len(sys.argv) > 1 else "."
SRC = os.path.join(BASE, "rime_ice.schema.yaml")
DST = os.path.join(BASE, "abc.schema.yaml")
DEFAULT = os.path.join(BASE, "default.yaml")

with open(SRC, encoding="utf-8") as f:
    lines = f.read().split("\n")

out = []
done = {"schema_id": False, "name": False, "switch": False,
        "processor": False, "filter": False, "translator": False}

for line in lines:
    if not done["schema_id"] and line.strip() == "schema_id: rime_ice":
        out.append("  schema_id: abc")
        done["schema_id"] = True
        continue
    if not done["name"] and line.startswith("  name:"):
        out.append("  name: 智能ABC（空格出词）")
        done["name"] = True
        continue

    out.append(line)

    if not done["switch"] and line.rstrip() == "switches:":
        out.append("  - name: abc_reveal")
        out.append("    states: [ 隐, 显 ]")
        out.append("    reset: 0")
        done["switch"] = True
        continue

    if not done["processor"] and line.rstrip() == "  processors:":
        out.append("    - lua_processor@*abc_space  # 智能ABC：空格才出候选")
        done["processor"] = True
        continue
    if not done["filter"] and line.rstrip() == "  filters:":
        out.append("    - lua_filter@*abc_hide  # 智能ABC：闸门关闭时丢弃全部候选")
        done["filter"] = True
        continue
    if not done["translator"] and line.rstrip() == "  translators:":
        out.append("    - lua_translator@*abc_single  # 智能ABC：词后回落首音节单字")
        done["translator"] = True
        continue

missing = [k for k, v in done.items() if not v]
if missing:
    sys.stderr.write("schema patch failed, not found: %s\n" % ", ".join(missing))
    sys.exit(1)

with open(DST, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
sys.stderr.write("wrote %s (%d lines)\n" % (DST, len(out)))

# ---- 每页 9 个候选（智能ABC 习惯，Rime 默认是 5）----
with open(DST, encoding="utf-8") as f:
    text = f.read()
if "\nmenu:" in text:
    text = text.replace("\nmenu:", "\nmenu:\n  page_size: 9", 1)
else:
    text = text.rstrip("\n") + "\n\n# 每页候选数（智能ABC 习惯）\nmenu:\n  page_size: 9\n"
with open(DST, "w", encoding="utf-8") as f:
    f.write(text)
sys.stderr.write("menu.page_size -> 9\n")

# ---- default.yaml: schema_list 只保留 abc ----
with open(DEFAULT, encoding="utf-8") as f:
    dlines = f.read().split("\n")

dout = []
in_list = False
for line in dlines:
    if line.strip() == "schema_list:":
        dout.append(line)
        dout.append("  - schema: abc")
        in_list = True
        continue
    if in_list:
        if line.startswith("  - ") or line.lstrip().startswith("#"):
            continue
        in_list = False
    dout.append(line)

with open(DEFAULT, "w", encoding="utf-8") as f:
    f.write("\n".join(dout))
sys.stderr.write("patched default.yaml schema_list -> abc\n")
