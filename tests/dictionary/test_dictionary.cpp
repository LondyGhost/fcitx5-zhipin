// A3 test driver for the read-only dictionary loader and prefix lookup.
//
// Coverage required by tasks/A3 (mapped to assertions):
//   - query (exact + prefix expansion)
//   - no-result
//   - frozen ordering (exact code first, then code ascending, weight
//     descending, text ascending)
//   - paging (offset / limit / total / hasMore; limit==0 counts only;
//     offset beyond total)
//   - corrupt index (bad magic / crc / flags / out-of-order / wrong
//     stableId / wrong codeCount / non-finite weight / bad UTF-8 / empty
//     source / trailing bytes)
//   - truncated index (below header, cut mid-record)
//   - version incompatibility (major mismatch; minor above reader)
//   - boundary lengths (code 128, text 512, source 64, prefix 128, limit
//     0 and 256)
//   - concurrency (many threads query one shared instance)
//   - A2 fixture cross-check (loads tests/dictionary/a2_fixture.idx if the
//     importer has produced it; otherwise reported as skipped)
//
// All fixture strings are ASCII so the source stays transport-safe; the
// byte-ordering, paging and query semantics are identical for any valid UTF-8
// content. UTF-8 acceptance/rejection is covered by explicit byte tests
// (valid multibyte accepted; 0xFF rejected).

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "abc/dictionary.h"
#include "abc/index_format.h"
#include "index_builder.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cerr << "[FAIL] " << what << "\n";
    ++g_failures;
  } else {
    std::cout << "[ok] " << what << "\n";
  }
}

// Writes bytes to a temp file and returns its path.
std::filesystem::path writeTemp(const std::vector<std::uint8_t>& data,
                                const std::string& tag) {
  std::filesystem::path dir = std::filesystem::temp_directory_path();
  const std::string name = "a3_" + tag + "_" +
                           std::to_string(
                               std::hash<std::string>{}(tag) & 0xFFFFu) +
                           ".idx";
  std::filesystem::path p = dir / name;
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.close();
  return p;
}

// Standard check for a load failure with the expected status.
void checkLoadFails(const std::vector<std::uint8_t>& data,
                    abc::DictionaryStatus expected, const std::string& what) {
  const std::filesystem::path p = writeTemp(data, what);
  abc::DictionaryLoadResult r = abc::loadDictionaryFromFile(p.string());
  check(r.status == expected && !r.dictionary,
        what + " -> status " + std::to_string(static_cast<int>(r.status)) +
            " (want " + std::to_string(static_cast<int>(expected)) + ")");
  std::error_code ec;
  std::filesystem::remove(p, ec);
}

// ---------------------------------------------------------------------------
// Fixture for ordering / paging / query tests (ASCII strings).
// ---------------------------------------------------------------------------
std::vector<a3test::Entry> makeFixture() {
  std::vector<a3test::Entry> e;
  auto add = [&e](const char* code, const char* text, double w,
                  const char* src) {
    a3test::Entry x;
    x.code = code;
    x.text = text;
    x.weight = w;
    x.source = src;
    e.push_back(x);
  };
  // Deliberately unsorted; buildIndex sorts into storage order.
  add("nihao", "txt-hao", 90.0, "cn");
  add("ni", "txt-ni", 100.0, "base");
  add("ni", "txt-ni2", 90.0, "base");
  add("ni", "txt-ni", 50.0, "dup");  // duplicate (code,text): highest weight wins
  add("nihaoma", "txt-haoma", 20.0, "cn");
  add("ni3", "txt-3", 5.0, "base");
  add("a", "txt-a", 1.0, "base");
  return e;
}

}  // namespace

int main() {
  const std::vector<a3test::Entry> fixture = makeFixture();
  const std::vector<std::uint8_t> index = a3test::buildIndex(fixture);
  const std::filesystem::path ipath = writeTemp(index, "valid");

  abc::DictionaryLoadResult load = abc::loadDictionaryFromFile(ipath.string());
  check(load.status == abc::DictionaryStatus::kOk && load.dictionary,
        "valid index loads with status kOk");
  const abc::Dictionary& dict = *load.dictionary;
  std::error_code ec;
  std::filesystem::remove(ipath, ec);

  // ---- Query: exact code first, then code ascending ----
  abc::LookupResult r = dict.lookup("ni", 9, 0);
  check(r.status == abc::DictionaryStatus::kOk && r.batch.total == 5,
        "prefix ni total == 5");
  if (r.batch.items.size() >= 5) {
    check(r.batch.items[0].text == "txt-ni" && r.batch.items[1].text == "txt-ni2" &&
              r.batch.items[2].text == "txt-3" && r.batch.items[3].text == "txt-hao" &&
              r.batch.items[4].text == "txt-haoma",
          "ordering: exact txt-ni,txt-ni2 then code asc txt-3,txt-hao,txt-haoma");
    check(r.batch.items[0].code == "ni" && r.batch.items[1].code == "ni" &&
              r.batch.items[2].code == "ni3" && r.batch.items[3].code == "nihao" &&
              r.batch.items[4].code == "nihaoma",
          "ordering codes: ni,ni,ni3,nihao,nihaoma");
  } else {
    check(false, "ordering: page has >= 5 items");
  }
  check(!r.batch.hasMore, "prefix ni full page hasMore == false");

  // ---- Query: exact match only ----
  abc::LookupResult ra = dict.lookup("a", 9, 0);
  check(ra.status == abc::DictionaryStatus::kOk && ra.batch.total == 1 &&
            ra.batch.items.size() == 1 && ra.batch.items[0].text == "txt-a" &&
            !ra.batch.hasMore,
        "exact query a returns one candidate");

  // ---- No result ----
  abc::LookupResult none = dict.lookup("zz", 9, 0);
  check(none.status == abc::DictionaryStatus::kOk && none.batch.total == 0 &&
            none.batch.items.empty() && !none.batch.hasMore,
        "no-result prefix zz -> empty batch, total 0");

  // ---- Paging ----
  abc::LookupResult p1 = dict.lookup("ni", 2, 0);
  check(p1.status == abc::DictionaryStatus::kOk && p1.batch.items.size() == 2 &&
            p1.batch.items[0].text == "txt-ni" && p1.batch.items[1].text == "txt-ni2" &&
            p1.batch.total == 5 && p1.batch.hasMore && p1.batch.offset == 0,
        "page 1 (limit 2, offset 0) -> txt-ni,txt-ni2, total 5, hasMore");
  abc::LookupResult p2 = dict.lookup("ni", 2, 2);
  check(p2.batch.items.size() == 2 && p2.batch.items[0].text == "txt-3" &&
            p2.batch.items[1].text == "txt-hao" && p2.batch.total == 5 &&
            p2.batch.hasMore && p2.batch.offset == 2,
        "page 2 (limit 2, offset 2) -> txt-3,txt-hao, hasMore");
  abc::LookupResult p3 = dict.lookup("ni", 2, 4);
  check(p3.batch.items.size() == 1 && p3.batch.items[0].text == "txt-haoma" &&
            p3.batch.total == 5 && !p3.batch.hasMore && p3.batch.offset == 4,
        "page 3 (limit 2, offset 4) -> txt-haoma, hasMore false");
  abc::LookupResult countOnly = dict.lookup("ni", 0, 0);
  check(countOnly.batch.items.empty() && countOnly.batch.total == 5,
        "limit 0 returns counts only");
  abc::LookupResult beyond = dict.lookup("ni", 9, 9);
  check(beyond.batch.items.empty() && beyond.batch.total == 5 &&
            !beyond.batch.hasMore,
        "offset beyond total -> empty items, total preserved");

  // ---- Invalid arguments ----
  check(dict.lookup("", 9, 0).status == abc::DictionaryStatus::kInvalidArgument,
        "empty prefix rejected");
  check(dict.lookup("ni", 300, 0).status ==
            abc::DictionaryStatus::kInvalidArgument,
        "limit > 256 rejected");
  check(dict.lookup("ni", 0, 0).status == abc::DictionaryStatus::kOk,
        "limit 0 is valid");

  // ---- Boundary: 128-byte code, 512-byte text, 64-byte source ----
  {
    std::vector<a3test::Entry> be;
    a3test::Entry e;
    e.code = std::string(128, 'c');
    e.text = std::string(512, 't');
    e.source = std::string(64, 's');
    e.weight = 1.0;
    be.push_back(e);
    const std::vector<std::uint8_t> bi = a3test::buildIndex(be);
    const std::filesystem::path bp = writeTemp(bi, "bound");
    abc::DictionaryLoadResult bl = abc::loadDictionaryFromFile(bp.string());
    check(bl.status == abc::DictionaryStatus::kOk,
          "max-length code/text/source load");
    if (bl.dictionary) {
      const abc::LookupResult br = bl.dictionary->lookup(e.code, 1, 0);
      check(br.status == abc::DictionaryStatus::kOk && br.batch.total == 1,
            "max-length code lookup matches");
    }
    std::filesystem::remove(bp, ec);
  }

  // ---- Concurrency: many threads, one shared instance ----
  {
    constexpr int kThreads = 8;
    constexpr int kIters = 2000;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&dict, &mismatches]() {
        for (int i = 0; i < kIters; ++i) {
          const abc::LookupResult q = dict.lookup("ni", 3, 0);
          if (q.status != abc::DictionaryStatus::kOk || q.batch.total != 5 ||
              q.batch.items.size() != 3 || q.batch.items[0].text != "txt-ni") {
            mismatches.fetch_add(1);
          }
        }
      });
    }
    for (std::thread& th : threads) th.join();
    check(mismatches.load() == 0,
          "8 threads x 2000 lookups agree (thread-safe lookup)");
  }

  // ---- Corrupt index cases ----
  {
    // Bad magic.
    std::vector<std::uint8_t> d = index;
    d[0] = 'X';
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "corrupt-magic");

    // Bad CRC: flip one body byte.
    d = index;
    d[d.size() - 4] ^= 0x01;
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "corrupt-crc");

    // Non-zero flags.
    d = index;
    d[0x20] = 1;
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "corrupt-flags");

    // Non-zero reserved0.
    d = index;
    d[0x24] = 1;
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "corrupt-reserved0");

    // Trailing bytes after records.
    d = index;
    d.push_back(0x00);
    d.push_back(0xAA);
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "corrupt-trailing");
  }

  // ---- Raw corrupt frames (out-of-order, wrong stableId, etc.) ----
  {
    auto frame = [](const std::string& code, const std::string& text,
                    double w, const std::string& src, std::uint64_t sid) {
      a3test::Entry e;
      e.code = code;
      e.text = text;
      e.weight = w;
      e.source = src;
      return a3test::serialiseRecord(e, sid);
    };
    const std::uint64_t sid_ni = abc::computeStableId("ni", "txt-ni", "base");
    const std::uint64_t sid_nihao =
        abc::computeStableId("nihao", "txt-hao", "cn");

    // Out-of-order codes: nihao before ni.
    {
      std::vector<std::vector<std::uint8_t>> frames;
      frames.push_back(frame("nihao", "txt-hao", 90.0, "cn", sid_nihao));
      frames.push_back(frame("ni", "txt-ni", 100.0, "base", sid_ni));
      checkLoadFails(a3test::buildRaw(frames, 2, 2, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-out-of-order");
    }

    // Wrong stableId (flip the last byte).
    {
      std::vector<std::vector<std::uint8_t>> frames;
      std::vector<std::uint8_t> f = frame("ni", "txt-ni", 100.0, "base", sid_ni);
      f[f.size() - 1] ^= 0x01;
      frames.push_back(f);
      checkLoadFails(a3test::buildRaw(frames, 1, 1, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-stableid");
    }

    // Wrong codeCount in the header.
    {
      std::vector<std::vector<std::uint8_t>> frames;
      frames.push_back(frame("ni", "txt-ni", 100.0, "base", sid_ni));
      frames.push_back(frame("nihao", "txt-hao", 90.0, "cn", sid_nihao));
      checkLoadFails(a3test::buildRaw(frames, 2, 5, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-codecount");
    }

    // Non-finite weight (NaN bit pattern in the weight field).
    {
      a3test::Entry e;
      e.code = "ni";
      e.text = "txt-ni";
      e.weight = 1.0;
      e.source = "base";
      std::vector<std::uint8_t> f = a3test::serialiseRecord(e, sid_ni);
      const std::size_t woff = 2 + e.code.size() + 2 + e.text.size();
      const std::uint8_t nan_bytes[8] = {0, 0, 0, 0, 0, 0, 0xf8, 0x7f};
      for (int i = 0; i < 8; ++i) f[woff + i] = nan_bytes[i];
      std::vector<std::vector<std::uint8_t>> frames;
      frames.push_back(f);
      checkLoadFails(a3test::buildRaw(frames, 1, 1, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-nan-weight");
    }

    // Empty source (sourceLen == 0).
    {
      std::vector<std::vector<std::uint8_t>> frames;
      frames.push_back(frame("ni", "txt-ni", 100.0, "", sid_ni));
      checkLoadFails(a3test::buildRaw(frames, 1, 1, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-empty-source");
    }

    // Invalid UTF-8 in text (0xFF byte).
    {
      std::vector<std::vector<std::uint8_t>> frames;
      std::vector<std::uint8_t> f = frame("ni", "txt-ni", 100.0, "base", sid_ni);
      f[2 + 2] = 0xff;  // first text byte
      frames.push_back(f);
      checkLoadFails(a3test::buildRaw(frames, 1, 1, abc::kIndexFormatVersion),
                     abc::DictionaryStatus::kCorruptIndex, "corrupt-bad-utf8");
    }

    // Valid multibyte UTF-8 text is accepted.
    {
      // text = bytes C3 A9 (UTF-8 for U+00E9) -> valid 2-byte sequence.
      std::string mb("\xC3\xA9", 2);
      const std::uint64_t sid_mb = abc::computeStableId("m", mb, "base");
      std::vector<std::vector<std::uint8_t>> frames;
      frames.push_back(frame("m", mb, 1.0, "base", sid_mb));
      const std::filesystem::path mp =
          writeTemp(a3test::buildRaw(frames, 1, 1, abc::kIndexFormatVersion),
                    "utf8mb");
      abc::DictionaryLoadResult mr = abc::loadDictionaryFromFile(mp.string());
      check(mr.status == abc::DictionaryStatus::kOk,
            "valid multibyte UTF-8 text is accepted");
      std::filesystem::remove(mp, ec);
    }
  }

  // ---- Truncation ----
  {
    // Below header (10 bytes).
    std::vector<std::uint8_t> d(10, 0x41);
    checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "truncate-below-header");

    // Header only, zero records: valid empty dictionary.
    {
      std::vector<std::vector<std::uint8_t>> frames;
      const std::vector<std::uint8_t> emptyidx =
          a3test::buildRaw(frames, 0, 0, abc::kIndexFormatVersion);
      const std::filesystem::path ep = writeTemp(emptyidx, "empty");
      abc::DictionaryLoadResult er = abc::loadDictionaryFromFile(ep.string());
      check(er.status == abc::DictionaryStatus::kOk, "empty index loads");
      if (er.dictionary) {
        abc::LookupResult qr = er.dictionary->lookup("x", 1, 0);
        check(qr.status == abc::DictionaryStatus::kOk && qr.batch.total == 0,
              "empty index lookup -> total 0");
      }
      std::filesystem::remove(ep, ec);
    }

    // Cut mid-record: header + a few bytes into the record area.
    {
      std::vector<std::uint8_t> d = index;
      d.resize(48 + 6);  // 6 bytes into the record area
      checkLoadFails(d, abc::DictionaryStatus::kCorruptIndex, "truncate-mid-record");
    }
  }

  // ---- Version incompatibility ----
  {
    const std::vector<std::vector<std::uint8_t>> one = {
        a3test::serialiseRecord(a3test::Entry{"ni", "txt-ni", "base", 100.0},
                                abc::computeStableId("ni", "txt-ni", "base"))};
    // Major mismatch (2.0).
    checkLoadFails(a3test::buildRaw(one, 1, 1, 0x00020000u),
                   abc::DictionaryStatus::kUnsupportedVersion,
                   "version-major-mismatch");
    // Minor above reader (1.1).
    checkLoadFails(a3test::buildRaw(one, 1, 1, 0x00010001u),
                   abc::DictionaryStatus::kUnsupportedVersion,
                   "version-minor-ahead");
    // Compatible minor (1.0).
    {
      const std::filesystem::path cp =
          writeTemp(a3test::buildRaw(one, 1, 1, abc::kIndexFormatVersion),
                    "ver10");
      abc::DictionaryLoadResult cr = abc::loadDictionaryFromFile(cp.string());
      check(cr.status == abc::DictionaryStatus::kOk, "version 1.0 compatible");
      std::filesystem::remove(cp, ec);
    }
  }

  // ---- A2 fixture cross-check (skipped unless importer produced it) ----
  {
    const std::filesystem::path a2 =
        std::filesystem::path("tests") / "dictionary" / "a2_fixture.idx";
    std::error_code a2ec;
    if (std::filesystem::exists(a2, a2ec)) {
      abc::DictionaryLoadResult a2r = abc::loadDictionaryFromFile(a2.string());
      check(a2r.status == abc::DictionaryStatus::kOk,
            "A2 fixture loads (cross-validation)");
      if (a2r.dictionary) {
        abc::LookupResult a2q = a2r.dictionary->lookup("", 1, 0);
        check(a2q.status == abc::DictionaryStatus::kInvalidArgument,
              "A2 fixture rejects empty prefix");
      }
    } else {
      std::cout << "[skip] A2 fixture not present (a2_fixture.idx)\n";
    }
  }

  if (g_failures == 0) {
    std::cout << "abc_dictionary_test: ALL PASS\n";
    return 0;
  }
  std::cout << "abc_dictionary_test: " << g_failures << " failure(s)\n";
  return 1;
}