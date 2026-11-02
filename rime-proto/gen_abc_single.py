#!/usr/bin/env python3
"""从 rime-ice 字库生成 lua/abc_single.lua（音节 -> 单字表 + 首音节回落翻译器）。

只取「单字 + 单音节码」的词条，按权重降序；每个音节最多保留 CAP 个字。
输出文件同时包含数据表与 translator 函数，直接 return 给 librime-lua。
"""
import glob
import os
import sys

BASE = sys.argv[1] if len(sys.argv) > 1 else "."
OUT = sys.argv[2] if len(sys.argv) > 2 else "lua/abc_single.lua"
CAP = 30

best = {}  # syllable -> {char: weight}

files = sorted(glob.glob(os.path.join(BASE, "cn_dicts", "*.dict.yaml")))
if not files:
    sys.exit("no dict files under %s/cn_dicts" % BASE)

for path in files:
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
            text = parts[0]
            code = parts[1].strip()
            if len(text) != 1 or not code or " " in code:
                continue
            if not code.isascii() or not code.isalpha() or not code.islower():
                continue
            if len(code) > 6:
                continue
            try:
                w = float(parts[2]) if len(parts) > 2 and parts[2].strip() else 0.0
            except ValueError:
                w = 0.0
            slot = best.setdefault(code, {})
            if text not in slot or w > slot[text]:
                slot[text] = w

# 生成 Lua 数据表
lines = []
lines.append("-- abc_single.lua — 由 tools/gen_abc_single.py 从 rime-ice 字库生成，请勿手改。")
lines.append("-- 作用：智能ABC 的「词后回落首音节单字」。")
lines.append("local CHARS = {")
for syl in sorted(best):
    items = sorted(best[syl].items(), key=lambda kv: -kv[1])[:CAP]
    body = ",".join('"%s"' % ch for ch, _ in items)
    lines.append('  ["%s"]={%s},' % (syl, body))
lines.append("}")
lines.append("""
-- 把输入切成音节数最少的方案，返回首音节；并列时取更长首音节。
local function first_syllable(code)
  local n = #code
  local INF = 1e9
  local best_cost, pick = {}, {}
  best_cost[n + 1] = 0
  for i = n, 1, -1 do
    best_cost[i] = INF
    local maxlen = math.min(6, n - i + 1)
    for len = 1, maxlen do
      local sub = code:sub(i, i + len - 1)
      local tail = best_cost[i + len]
      if CHARS[sub] and tail and tail < INF then
        local cand = tail + 1
        if cand < best_cost[i] or (cand == best_cost[i] and len > (pick[i] or 0)) then
          best_cost[i] = cand
          pick[i] = len
        end
      end
    end
  end
  if best_cost[1] >= INF or not pick[1] then return nil end
  return code:sub(1, pick[1])
end

local function translator(input, seg, env)
  if not seg:has_tag("abc") then return end
  if not input:match("^[a-z]+$") then return end
  local syl = first_syllable(input)
  if not syl or #syl >= #input then return end
  local list = CHARS[syl]
  if not list then return end
  local n = 0
  for _, ch in ipairs(list) do
    n = n + 1
    if n > 30 then break end
    -- quality 给负值：排在拼音词之后
    -- 注意 Segment 的结束位置是 _end（end 是 Lua 保留字）
    yield(Candidate("abc_single", seg.start, seg._end, ch, "", -1))
  end
end

return translator
""")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

sys.stderr.write("wrote %s: %d syllables, %d chars\n" %
                 (OUT, len(best), sum(len(v) for v in best.values())))
