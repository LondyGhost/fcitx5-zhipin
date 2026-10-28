# A3 - Runtime Dictionary and Candidate Lookup (dictionary-runtime.md)

Owner: A3. Scope per `tasks/A3-* (runtime dictionary)`.

## Independent acceptance status (2026-10-28)

- Current source baseline: `45d5cb70a7a835359bb6ccae55d63112233a2591`.
- A3 implementation delivery: `76a83eeb2558e12c7af592179ac46808c2d1b78e`;
  this acceptance supplement changes `tests/dictionary/CMakeLists.txt`,
  `tests/dictionary/test_dictionary.cpp`, and the English/Chinese module
  records listed here; its commit is reported with the handoff.
- Interface contract: **1.0.0-rc1**; index format: **v1.0**.
- The A2 paths checked for this acceptance are:
  - Task: `D:\workbuddy\linux智能ABC\tasks\A2-Rime词库导入器.md`
  - Importer source: `D:\workbuddy\linux智能ABC\source\dict-importer\`
  - Importer tests/fixtures: `D:\workbuddy\linux智能ABC\tests\dict-importer\`
  These A2 source/test directories are absent from the current baseline; the
  importer CMake file is only a placeholder. No new A2 owner, delivery commit,
  A2-specific interface-version declaration, or A2-produced fixture/report
  can be verified from the available history.
  **A2→A3 cross-validation: 等待 A2 交付.** Fixture path, SHA-256, generation
  command, report path, index hash, load/query result, and duplicate-build
  comparison are therefore not available. The A3 test builder below is not
  evidence for this cross-validation.
- A3 source: `D:\workbuddy\linux智能ABC\source\dictionary\`;
  tests/CMake: `D:\workbuddy\linux智能ABC\tests\dictionary\`;
  index contract: `D:\workbuddy\linux智能ABC\docs\interfaces\index-format.md`;
  dictionary contract: `D:\workbuddy\linux智能ABC\docs\interfaces\dictionary.md`;
  English module record: `D:\workbuddy\linux智能ABC\docs\modules\dictionary-runtime.md`;
  Chinese module record: `D:\workbuddy\linux智能ABC\docs\modules\dictionary-runtime.zh.md`;
  A6-owned acceptance record (not edited here):
  `D:\workbuddy\linux智能ABC\docs\验收记录.md`.
- `tests/dictionary/CMakeLists.txt` was missing. It is now supplied by A3;
  the root `CMakeLists.txt` already lists `dictionary` in `ABC_TEST_MODULES`
  and automatically adds the directory when its CMake file exists. It adds
  `abc_dictionary_tests` to CTest and builds `abc_bench_dictionary`.
- Boundary regression coverage now includes `recordCount > kIndexMaxRecords`
  and a count whose `count * kIndexMinRecordBytes` exceeds the record area;
  existing tests cover truncation, CRC/malformed fields, major/minor version
  rejection, offsets beyond total and page boundaries. Loader uses u64 for the
  on-disk count and validates the 10,000,000 cap before multiplying by 24;
  after that, the product is bounded well below u64 overflow. It checks the
  byte budget before converting to `size_t` or reserving, then caps reserve by
  `record_bytes / 24`. File-size allocation is independently capped at 3 GiB.
- Clean build/test commands (run from the repository root):
  `cmake -S . -B build-a3-clean -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`,
  `cmake --build build-a3-clean`,
  `ctest --test-dir build-a3-clean --output-on-failure`.
  CMake and Ninja are absent in this Windows session, so configure/build/CTest
  have not run here. A direct MinGW functional-test build did run:
  `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include
  -Isource/dictionary -Itests/dictionary source/dictionary/index_loader.cpp
  source/dictionary/dictionary_index.cpp tests/dictionary/test_dictionary.cpp
  -o %TEMP%\abc_test_dictionary_a3.exe -pthread`, followed by
  `%TEMP%\abc_test_dictionary_a3.exe`.
  Result: exit 0; **41 checks passed, 0 failed, 1 skipped** (`A2 fixture not
  present`). The 41 pass result includes record-count maximum, record-area
  budget, truncation/corruption, version, sorting, paging and offset tests.
  This direct compile does not establish CMake/CTest registration or clean
  build results. Target Linux test execution is also pending.
- Target Linux device profile available in `docs/目标机环境.md`: CatOS,
  x86_64, kernel 7.2.11-arch1-1, 4 CPU, GCC 16.2.1, CMake 4.4.4, Ninja 1.13.2.
  No A3 benchmark was run there. The existing Windows 200,000-record figures
  below are development measurements only and do not satisfy target-Linux
  acceptance. **Target-Linux performance at the contract scale (up to
  2,000,000 records / 200 MB): 待完成.** Responsible: A3 (DNP Dev); plan: build
  `abc_bench_dictionary` on the target, run at the representative 2,000,000
  records, and archive full stdout, device details, index size, load time and
  lookup latency under an A3-owned evidence location before acceptance.
- Redacted A3 build/test and benchmark log path: **待生成并交由 A6 归档**;
  this change does not write to A6-owned `logs/`, `tests/integration/`,
  `tests/manual/`, `artifacts/manifest.md`, or `docs/验收记录.md`.
- Known acceptance limits: new A2 owner/commit and cross-produced artifact are
  absent; clean CMake/CTest and target-Linux measurements remain unverified.
  Existing module self-tests and Windows synthetic benchmarks do not close
  those items.

This module provides the read-only runtime index loader and the
`abc::Dictionary` implementation behind the frozen public contract
(`docs/interfaces/dictionary.md`, `docs/interfaces/index-format.md`).

## 1. Module summary

- **`abc::loadDictionaryFromFile(path)`** - opens a v1.0 engine index, runs the
  full loader-validation sequence of `index-format.md` section 5, and returns a
  `DictionaryLoadResult` holding a read-only `Dictionary` on success.
- **`abc::Dictionary::lookup(prefix, limit, offset)`** - deterministic prefix
  query with frozen ordering, stable IDs and paging semantics.

The loader never writes a file, never overwrites an existing index, and never
parses Rime schema/Lua. It treats the index strictly as data.

## 2. Interface version and index format

- Interface contract version: **1.0.0-rc1** (`docs/interfaces/README.md`).
- Delivery commit: `76a83eeb2558e12c7af592179ac46808c2d1b78e`.
- Index format: **v1.0** (`abc::kIndexFormatVersion == 0x00010000`,
  magic `"ABCDIX1\0"`).

No public header under `source/include/abc/` was modified; the index layout was
not changed. `abc::computeStableId` is shared (A0 header), not re-implemented.

## 3. Files owned / changed by A3

```
source/dictionary/CMakeLists.txt        build target abc_dictionary (static lib)
source/dictionary/index_loader.cpp      loader: read + validate + build
source/dictionary/dictionary_index.cpp  RuntimeDictionary::lookup + helpers
source/dictionary/internal/bytes.hpp    byte / UTF-8 / CRC-32 helpers
source/dictionary/internal/index.hpp    private model + method declarations
tests/dictionary/index_builder.hpp      independent v1.0 fixture builder + CRC-32
tests/dictionary/test_dictionary.cpp    functional test driver (main)
tests/dictionary/bench_dictionary.cpp   performance benchmark (main)
docs/modules/dictionary-runtime.md      this document
```

No file outside the A3 ownership boundary was modified.

## 4. Loader behaviour and error semantics

The loader validates in the fixed order of `index-format.md` section 5, stopping
at the first failure with a located `detail`:

1. File readable (`kIoError`); size >= 48 bytes (`kCorruptIndex`).
2. Magic byte-for-byte (`kCorruptIndex`).
3. Version compatibility (`kUnsupportedVersion`; major mismatch or file minor
   above the reader minor is rejected, see section 4 of the format contract).
4. flags / reserved0 / reserved1 all zero (`kCorruptIndex`).
5. CRC-32/IEEE over `[0x20, EOF)` (`kCorruptIndex`).
6. recordCount <= `kIndexMaxRecords` and recordCount * 24 <= remaining bytes,
   checked **before** any allocation (`kCorruptIndex`).
7. Each record frame: every length field compared against remaining bytes; code
   control-free and length-bounded; text/source valid UTF-8, control-free,
   non-empty, length-bounded; weight finite; stableId re-checked against
   `computeStableId` (`kCorruptIndex`). Record area must consume the file exactly
   (`kCorruptIndex` on trailing bytes).
8. Storage order (code ascending, weight descending, text ascending) and
   `(code, text)` uniqueness (`kCorruptIndex`).
9. codeCount equals the actual distinct-code count (`kCorruptIndex`).

**Resource bounds.** The buffer size comes from `fstat`, never from a header
field. A fixed ceiling `kMaxLoadableBytes = 3 GiB` caps a single load; the record
table is reserved with a budget derived from the file size. `std::bad_alloc` is
caught and mapped to `kIoError` with a diagnostic. No file is written.

## 5. Lookup semantics

`lookup` is `const` and thread-safe: the object is immutable after construction;
no cache, counter, or static mutable state is written, so any number of threads
may query one shared instance concurrently (sessions themselves stay
single-threaded per `engine.md` section 7).

- Parameter validation (`dictionary.md` 4.2): prefix non-empty, <= 128 bytes,
  no control byte; limit 0..256; offset any uint32. Violations return
  `kInvalidArgument` with an empty batch.
- Ordering (`dictionary.md` 4.1): exact-code group first (`code == prefix`), then
  codes ascending, then within a code weight descending then text ascending.
  Because the index is stored in that byte order, both groups are contiguous
  runs; a page is served by two binary-searched runs with **no per-query sort**
  and materialises at most `limit` candidates.
- Paging (`dictionary.md` 3): `offset` is the 0-based global offset; `total` is
  the exact match count; `hasMore == (offset + items.size() < total)`; `limit==0`
  is count-only; `offset >= total` returns an empty batch with `total` preserved.
- Determinism: identical index + identical arguments yield byte-identical results
  on every thread and call order.

## 6. Performance measurement

Development-only measurement: Windows host, MinGW g++ 16.1.0 (`-O2`), single
thread, local SSD. This is not target-Linux acceptance evidence.

Representative dataset: 200,000 entries, 8.8 MB index, distinct codes, synthetic
`c00000000`-style codes.

| Measurement | Result |
|---|---|
| Index build (synthetic A3 benchmark fixture) | 500.139 ms |
| Write index | 10.3174 ms |
| Load into serviceable state (`loadDictionaryFromFile`) | 74.0305 ms |
| Lookup, 100k prefix queries, page size 9 | 215.223 ms total; 2.15223 us / lookup |

Command: `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isource/include
-Isource/dictionary -Itests/dictionary source/dictionary/index_loader.cpp
source/dictionary/dictionary_index.cpp tests/dictionary/bench_dictionary.cpp
-o %TEMP%\abc_bench_dictionary_a3.exe -pthread`, then
`%TEMP%\abc_bench_dictionary_a3.exe 200000`.
Full output: `bench: records=200000 index_bytes=8800048 build_ms=500.139
write_ms=10.3174 load_ms=74.0305 lookup_ms_per_100k=215.223
us_per_lookup=2.15223 prefix_total=10 first_page_size=9 served=900000`.
Method: `tests/dictionary/bench_dictionary.cpp`; timings use
`std::chrono::steady_clock`.

The contract baseline is <= 2,000,000 records and <= 200 MB. Lookup is
O(log n) per page plus O(limit) materialisation; load is O(n) over the file.

## 7. Build and test commands

Standalone compilation (cmake/ninja are not installed in this environment; the
CMake target is still provided for the root build):

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isource/include -Isource/dictionary -Itests/dictionary \
  source/dictionary/index_loader.cpp source/dictionary/dictionary_index.cpp \
  tests/dictionary/test_dictionary.cpp -o test_dictionary.exe -pthread
./test_dictionary.exe
```

Result: `abc_dictionary_test: ALL PASS` (37 checks). Coverage includes exact and
prefix query, no-result, frozen ordering, paging (offset/limit/total/hasMore,
limit 0, offset beyond total), invalid arguments, boundary lengths (code 128,
text 512, source 64, limit 0/256), concurrency (8 threads x 2000 lookups),
corrupt magic/CRC/flags/reserved/trailing/out-of-order/stableId/codeCount/
non-finite weight/empty source/invalid UTF-8, truncation (below header,
mid-record, empty index), and version incompatibility (major mismatch, minor
ahead, compatible 1.0). A2 fixture cross-check is skipped when
`tests/dictionary/a2_fixture.idx` is absent.

Benchmark:

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -Isource/include -Isource/dictionary -Itests/dictionary \
  source/dictionary/index_loader.cpp source/dictionary/dictionary_index.cpp \
  tests/dictionary/bench_dictionary.cpp -o bench_dictionary.exe -pthread
./bench_dictionary.exe 200000
```

## 8. Threading constraints

- `lookup` is safe to call concurrently from any number of threads on one
  instance (no locking needed; the object is immutable after construction).
- Loading is not required to be thread-safe across calls on the same path (each
  `loadDictionaryFromFile` call is independent; the contract permits no shared
  cache).
- Engine sessions are single-threaded; sharing a loaded dictionary across
  sessions is allowed by the above.

## 9. Known limitations

- **A2 cross-validation pending.** The importer fixture `tests/dictionary/a2_fixture.idx`
  is not present in this repository, so the A2-produced-index cross-check is
  reported as skipped. It will run automatically once A2 delivers the fixture;
  the loader logic is otherwise exercised by independently built v1.0 indexes
  (including an independent CRC-32 implementation).
- The `stableId` re-check is performed (recommended by `dictionary.md` 2.2); a
  producer that writes a wrong stableId is rejected.
- Loader holds the whole file in memory (no mmap on this build); the ceiling is
  documented (`kMaxLoadableBytes`). Streaming is possible for very large files
  but is not implemented.
- Non-adjacent duplicate `(code, text)` entries with different weights are a
  producer obligation (dedup rule, `dictionary.md` 2.3); the loader rejects
  storage-order violations and adjacent duplicates but cannot detect a
  non-adjacent duplicate that is otherwise in valid storage order. Compliant
  importers never produce such files.

## 10. Self-check

- No XIM/XTest/xdotool, no `/opt` or system `/usr` writes, no deployment
  credentials in the repository.
- Public headers and the index layout were not modified; `computeStableId` is
  shared, not re-implemented.
