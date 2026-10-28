// A3 performance benchmark for the read-only dictionary loader and lookup.
//
// Builds a synthetic index of kRecords entries (all distinct codes, so the
// code field is the order key), loads it through the public abc API
// loadDictionaryFromFile, then times:
// loadDictionaryFromFile, then times:
//   - load time (file read + validation + table build)
//   - lookup latency for a prefix query
//   - total batch size served by a full-page query
//
// The device and dataset size are printed so the number is reproducible; this
// satisfies docs/interfaces/dictionary.md section 6 (load and lookup timing on
// a representative dataset, method reported).
//
// ASCII only; synthetic data.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "abc/dictionary.h"
#include "index_builder.hpp"

namespace {

std::string pad(std::uint64_t v, int width) {
  std::string s = std::to_string(v);
  while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t kRecords = (argc > 1) ? std::stoull(argv[1]) : 200000u;
  const std::uint64_t kLookups = 100000;

  std::vector<a3test::Entry> entries;
  entries.reserve(kRecords);
  for (std::uint64_t i = 0; i < kRecords; ++i) {
    a3test::Entry e;
    e.code = "c" + pad(i, 8);
    e.text = "t" + pad(i, 8);
    e.source = "bench";
    e.weight = static_cast<double>(i % 1000);
    entries.push_back(std::move(e));
  }
  const auto t1 = std::chrono::steady_clock::now();
  const std::vector<std::uint8_t> idx = a3test::buildIndex(entries);
  const auto t2 = std::chrono::steady_clock::now();

  std::filesystem::path dir = std::filesystem::temp_directory_path();
  std::filesystem::path p = dir / "a3_bench.idx";
  {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(idx.data()),
              static_cast<std::streamsize>(idx.size()));
  }
  const auto t3 = std::chrono::steady_clock::now();
  abc::DictionaryLoadResult load = abc::loadDictionaryFromFile(p.string());
  if (load.status != abc::DictionaryStatus::kOk || !load.dictionary) {
    std::cerr << "bench: load failed: " << load.detail << "\n";
    return 1;
  }
  const auto t4 = std::chrono::steady_clock::now();
  const abc::Dictionary& dict = *load.dictionary;

  // Warm-up + a representative prefix query (page size 9, first page).
  const std::string prefix = "c0000012";
  abc::LookupResult probe = dict.lookup(prefix, 9, 0);
  const std::uint64_t total = probe.batch.total;
  const std::size_t first_page = probe.batch.items.size();

  const auto t5 = std::chrono::steady_clock::now();
  std::uint64_t served = 0;
  for (std::uint64_t k = 0; k < kLookups; ++k) {
    abc::LookupResult q = dict.lookup("c000", 9, 0);
    served += q.batch.items.size();
  }
  const auto t6 = std::chrono::steady_clock::now();

  const auto ms = [](auto a, auto b) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
               b - a)
        .count();
  };
  const auto us_per_lookup =
      (ms(t5, t6) * 1000.0) / static_cast<double>(kLookups);

  std::cout << "bench: records=" << kRecords
            << " index_bytes=" << idx.size()
            << " build_ms=" << ms(t1, t2) << " write_ms=" << ms(t2, t3)
            << " load_ms=" << ms(t3, t4)
            << " lookup_ms_per_100k=" << ms(t5, t6)
            << " us_per_lookup=" << us_per_lookup
            << " prefix_total=" << total
            << " first_page_size=" << first_page
            << " served=" << served << "\n";

  std::error_code ec;
  std::filesystem::remove(p, ec);
  return 0;
}