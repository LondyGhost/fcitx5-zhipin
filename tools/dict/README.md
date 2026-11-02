# tools/dict — Rime 词库 → abc 引擎索引

把 Rime 的 `.dict.yaml`（含 `import_tables`）编译成引擎索引 `index.abcdix`
（格式见 `docs/interfaces/index-format.md` v1.0）。

## 两个工具

| 文件 | 作用 |
|---|---|
| `merge_rime_dict.py` | 递归展开 `import_tables`，按 `columns` 取字段，输出 `text<TAB>code<TAB>weight` 表体 |
| `build-rime-index.sh` | 合并 → `abc_dict_import` 编译 → 备份旧索引 → 安装到 `~/.local/share/abc/` → 可选重启 fcitx5 |

## 用法

```bash
# 1) 先构建导入器（项目根目录）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4

# 2) 一键生成并安装（rime-ice 默认路径 ~/.local/share/fcitx5/rime）
tools/dict/build-rime-index.sh \
  --import-tool build/source/dict-importer/abc_dict_import \
  --restart

# 只看会合并哪些表
python3 tools/dict/merge_rime_dict.py \
  --main ~/.local/share/fcitx5/rime/ice/rime_ice.dict.yaml --list
```

`build-rime-index.sh` 只写用户目录：索引落到 `<prefix>/.local/share/abc/index.abcdix`，
覆盖前把旧文件备份成 `index.abcdix.<时间戳>.bak`。

## 简拼 / 混拼

`--abbr-max-syllables N`（默认 4）会为 2..N 个音节的词额外生成两条码：

| 码型 | `bi ru shuo` 的例子 | 说明 |
|---|---|---|
| 全拼 | `birushuo` | 原始码 |
| 全首字母 | `brs` | 纯简拼 |
| 首字母 + 末音节全拼 | `brshuo` | 混拼（QQ/搜狗最常见的打法） |

所以 `brshuo`、`brs` 都能打出「比如说」，`zhrmin` 能打出「中华人民」。
单音节词不生成（没有简拼可言），`--abbr-max-syllables 0` 可整体关闭。

代价是索引变大（约 2.6 倍记录数），见下面的实测。

## 已知限制

- **没有 `code` 列的词库不能导入**：rime-ice 的 `cn_dicts/tencent.dict.yaml` 只有
  `text weight` 两列，拼音由 Rime 方案按字表自动注音；导入器要求显式 code，
  因此该表会被跳过（脚本会打印 `skip: columns missing text/code`）。
- 导入器不支持 `columns/import_tables/vocabulary` 之外的 Rime 特性（lua、processor、
  speller/algebra 一律忽略）；词库内容只当数据，绝不执行。
- 代码归一化：去掉空格分隔符并转小写，结果必须是 `a-z`；不合格的行由导入器计入
  `skipped-lines`。

## 实测（目标机，2026-10-29）

```text
8105 + base + ext + others + rime_ice 表体
  → 891,731 行 → 891,471 条记录 / 788,104 个 code
  → index.abcdix 46,256,310 字节（约 45 MB），crc32 0xCE1090CF（同输入同字节）
  → fcitx5 常驻内存约 95 MB
  → 实测：nihao→你好、zhongguo→中国、wo→我、shurufa→输入法
```

开启简拼（`--abbr-max-syllables 4`）：

```text
表体 2,286,897 行 → 2,283,643 条记录 / 1,227,100 个 code
index.abcdix 106,273,882 字节（约 102 MB），crc32 0x0FE9C483
fcitx5 常驻内存约 170 MB
实测：brshuo→比如说、brs→比如说、nh→女孩、zhrmin→中华人民
```

内存吃紧时可以 `--abbr-max-syllables 3`（只给 2..3 音节词生成简拼，索引约 78 MB），
或者 `0` 完全关闭。

词典只在 addon 加载时读取，改完索引必须重启 fcitx5。
