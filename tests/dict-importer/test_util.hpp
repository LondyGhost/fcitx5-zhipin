// A2 测试公共工具：运行配置、临时目录、文件读写、跨平台子进程执行、
// 独立 CRC-32（按位循环实现，与导入器的查表实现互相独立，用于交叉校验）、
// 小端读取助手。

#ifndef ABC_DICT_IMPORT_TEST_UTIL_HPP
#define ABC_DICT_IMPORT_TEST_UTIL_HPP

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "test_harness.hpp"

namespace abc_test {

// 运行配置：argv[1] = fixtures 目录；argv[2] = abc_dict_import 可执行路径
// （缺省时 e2e 用例跳过；CTest 会同时提供两者）。
struct RunConfig {
  std::string fixture_dir = "fixtures";
  std::string importer_exe;
};
inline RunConfig& runConfig() {
  static RunConfig c;
  return c;
}

inline std::string fixtureFile(const std::string& name) {
  return (std::filesystem::path(runConfig().fixture_dir) / name).string();
}

// 每用例独立的临时目录（析构时递归删除）。
struct TempDir {
  std::filesystem::path path;
  TempDir() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path();
    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int i = 0; i < 128; ++i) {
      const auto cand =
          base / ("abc_dict_import_test_" + std::to_string(now) + "_" +
                  std::to_string(i));
      std::error_code ec;
      if (fs::create_directory(cand, ec)) {
        path = cand;
        return;
      }
    }
    std::fprintf(stderr, "fatal: cannot create temp directory\n");
    std::exit(2);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  std::string file(const char* name) const {
    return (path / name).string();
  }
};

inline void writeFileBytes(const std::string& path, const std::string& bytes) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    fail(__FILE__, __LINE__, "cannot write " + path);
    return;
  }
  if (!bytes.empty()) {
    std::fwrite(bytes.data(), 1, bytes.size(), f);
  }
  std::fclose(f);
}

inline std::string readFileBytes(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return std::string();
  std::string out;
  char buf[4096];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  std::fclose(f);
  return out;
}

inline bool fileExists(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fclose(f);
  return true;
}

// 跨平台执行 shell 命令并返回进程退出码。
inline int runCommand(const std::string& cmd) {
#if defined(_WIN32)
  return std::system(cmd.c_str());
#else
  const int status = std::system(cmd.c_str());
  if (status == -1) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
#endif
}

// 仅在路径含空白/引号时加引号：避免 Windows cmd 对整条命令以引号开头的
// 特殊解析规则（"cmd /c "..."" 的经典问题）；POSIX 下同样正确。
inline std::string quoted(const std::string& path) {
  const bool need = path.find_first_of(" \t\"") != std::string::npos;
  return need ? ("\"" + path + "\"") : path;
}
inline std::string redirected(const std::string& exe, const std::string& args,
                              const std::string& log) {
  return quoted(exe) + " " + args + " > " + quoted(log) + " 2>&1";
}

inline bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

// 独立 CRC-32/IEEE（按位循环，不查表）：与导入器的查表实现互相独立。
inline std::uint32_t crc32Independent(const unsigned char* p,
                                      std::size_t n) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (int k = 0; k < 8; ++k) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

// 小端读取（调用方保证偏移越界前已检查长度）。
inline std::uint16_t leU16(const std::string& b, std::size_t off) {
  return static_cast<std::uint16_t>(static_cast<unsigned char>(b[off]) |
                                    (static_cast<unsigned char>(b[off + 1])
                                     << 8));
}
inline std::uint32_t leU32(const std::string& b, std::size_t off) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    v |= static_cast<std::uint32_t>(
             static_cast<unsigned char>(b[off + static_cast<std::size_t>(i)]))
         << (8 * i);
  }
  return v;
}
inline std::uint64_t leU64(const std::string& b, std::size_t off) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(
             static_cast<unsigned char>(b[off + static_cast<std::size_t>(i)]))
         << (8 * i);
  }
  return v;
}
inline double leF64(const std::string& b, std::size_t off) {
  const std::uint64_t bits = leU64(b, off);
  double v = 0.0;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

}  // namespace abc_test

#endif  // ABC_DICT_IMPORT_TEST_UTIL_HPP
