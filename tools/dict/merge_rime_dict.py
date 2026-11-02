#!/usr/bin/env python3
# merge_rime_dict.py — 把 Rime 的 .dict.yaml（含 import_tables）合并成
# abc_dict_import 可读的 `text<TAB>code<TAB>weight` 表体。
#
# 只做数据搬运：不执行词库里的任何内容（lua/processor/speller 一律忽略）。
# 用法：
#   merge_rime_dict.py --main <rime_ice.dict.yaml> [--out merged.tsv] [--no-import]
#   merge_rime_dict.py --main ... --list        # 只列出会被合并的表
#
# 约定（与 docs/interfaces/index-format.md 对齐）：
#   - 表体按 columns 顺序取字段，缺省 text / code / weight；
#   - code 原样输出（含空格分隔符），归一化由 abc_dict_import 负责；
#   - 缺 weight 时只输出两列；
#   - 跳过注释行、空行、以及 `---`/`...` 之外的头部键值行。

import argparse
import os
import sys


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="strict") as handle:
        for line in handle:
            yield line.rstrip("\n").rstrip("\r")


def parse_dict(path):
    """返回 (import_tables, columns, body_lines)。"""
    import_tables = []
    columns = []
    body = []
    in_header = False
    seen_header = False
    current_key = ""

    for line in read_lines(path):
        stripped = line.strip()
        if stripped == "---" and not seen_header:
            in_header = True
            seen_header = True
            continue
        if stripped == "...":
            in_header = False
            continue
        if in_header:
            if not stripped or stripped.startswith("#"):
                continue
            if stripped.startswith("- "):
                item = stripped[2:].strip()
                # import_tables 的列表项；columns 的列表项单独处理
                if current_key == "import_tables":
                    item = item.split("#", 1)[0].strip()
                    if item:
                        import_tables.append(item)
                elif current_key == "columns":
                    item = item.split("#", 1)[0].strip()
                    if item:
                        columns.append(item)
                continue
            if ":" in stripped:
                key, _, value = stripped.partition(":")
                current_key = key.strip()
                value = value.split("#", 1)[0].strip()
                if current_key == "import_tables" and value:
                    # 行内写法 import_tables: [a, b]
                    if value.startswith("[") and value.endswith("]"):
                        for part in value[1:-1].split(","):
                            part = part.strip().strip("'\"")
                            if part:
                                import_tables.append(part)
                elif current_key == "columns" and value:
                    if value.startswith("[") and value.endswith("]"):
                        for part in value[1:-1].split(","):
                            part = part.strip().strip("'\"")
                            if part:
                                columns.append(part)
                continue
            current_key = ""
            continue
        # 表体
        if not stripped or stripped.startswith("#"):
            continue
        body.append(line)

    return import_tables, columns, body


def resolve_table(base_dir, name):
    """import_tables 里的名字解析成实际文件路径。"""
    candidate = os.path.join(base_dir, name)
    for suffix in ("", ".dict.yaml", ".yaml"):
        path = candidate + suffix
        if os.path.isfile(path):
            return path
    return None


def abbr_variants(code, max_syllables):
    """生成简拼/混拼码：全拼之外追加「全首字母」和「首字母 + 末音节全拼」。

    例：bi ru shuo -> brs, brshuo（用户敲 brshuo 就是靠这条命中）。
    只对 2..max_syllables 个音节的词生成，控制索引体积。
    """
    parts = code.split()
    n = len(parts)
    if n < 2 or n > max_syllables:
        return []
    if any(not p.isalpha() or not p.isascii() for p in parts):
        return []
    initials = [p[0] for p in parts]
    out = []
    all_init = "".join(initials)
    if all_init != "".join(parts):
        out.append(all_init)
    mixed = "".join(initials[:-1]) + parts[-1]
    if mixed != all_init and mixed != "".join(parts):
        out.append(mixed)
    return out


def emit_body(body, columns, out, abbr_max_syllables=0):
    order = columns or ["text", "code", "weight"]
    try:
        text_idx = order.index("text")
        code_idx = order.index("code")
        weight_idx = order.index("weight") if "weight" in order else -1
    except ValueError:
        sys.stderr.write("skip: columns missing text/code: %r\n" % (order,))
        return 0, 0
    written = 0
    skipped = 0
    for line in body:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) <= max(text_idx, code_idx):
            skipped += 1
            continue
        text = fields[text_idx].strip()
        code = fields[code_idx].strip()
        if not text or not code:
            skipped += 1
            continue
        weight = ""
        if weight_idx >= 0 and len(fields) > weight_idx:
            weight = fields[weight_idx].strip()
        if weight:
            out.write("%s\t%s\t%s\n" % (text, code, weight))
        else:
            out.write("%s\t%s\n" % (text, code))
        written += 1
        if abbr_max_syllables >= 2:
            for variant in abbr_variants(code, abbr_max_syllables):
                if weight:
                    out.write("%s\t%s\t%s\n" % (text, variant, weight))
                else:
                    out.write("%s\t%s\n" % (text, variant))
                written += 1
    return written, skipped


def collect(path, seen, order, base_dir):
    if path in seen:
        return
    seen.add(path)
    import_tables, columns, body = parse_dict(path)
    for name in import_tables:
        child = resolve_table(base_dir, name)
        if child is None:
            sys.stderr.write("warn: import table not found: %s\n" % name)
            continue
        collect(child, seen, order, base_dir)
    order.append((path, columns, body))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--main", required=True, help="rime_ice.dict.yaml 之类的入口词库")
    parser.add_argument("--out", help="输出的表体文件；缺省写 stdout")
    parser.add_argument("--list", action="store_true", help="只列出参与合并的词库")
    parser.add_argument("--no-import", action="store_true", help="不合并入口词库自身，只合并 import_tables")
    parser.add_argument("--abbr-max-syllables", type=int, default=0,
                        help="为 2..N 音节的词额外生成简拼/混拼码（0 = 不生成，建议 4）")
    args = parser.parse_args()

    main_path = os.path.abspath(args.main)
    base_dir = os.path.dirname(main_path)

    order = []
    collect(main_path, set(), order, base_dir)
    if args.no_import and order:
        order = order[1:]

    if args.list:
        for path, columns, body in order:
            print("%s\t%d lines\tcolumns=%s" % (path, len(body), columns or "text,code,weight"))
        return 0

    out = open(args.out, "w", encoding="utf-8", newline="\n") if args.out else sys.stdout
    total_written = 0
    total_skipped = 0
    try:
        for path, columns, body in order:
            written, skipped = emit_body(body, columns, out,
                                         args.abbr_max_syllables)
            total_written += written
            total_skipped += skipped
            sys.stderr.write("merged %-60s %8d lines (%d skipped)\n" % (path, written, skipped))
    finally:
        if out is not sys.stdout:
            out.close()
    sys.stderr.write("total: %d entries, %d skipped\n" % (total_written, total_skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
