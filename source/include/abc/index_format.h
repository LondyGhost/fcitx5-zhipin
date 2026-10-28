// abc/index_format.h — 引擎索引格式常量（A0 维护）
// 契约：docs/interfaces/index-format.md（布局、版本策略、校验义务）。
// 写入方 A2 导入器；读取方 A3 loader。双方必须使用本文件的常量。
#ifndef ABC_INDEX_FORMAT_H_
#define ABC_INDEX_FORMAT_H_

#include <cstdint>

namespace abc {

// 魔数 "ABCDIX1\0"（8 字节，逐字节比较）。
inline constexpr unsigned char kIndexMagic[8] = {'A', 'B', 'C', 'D',
                                                 'I', 'X', '1', '\0'};

// 格式版本：(major << 16) | minor。v1.0 = 0x00010000。
// 版本策略见 index-format.md 第 4 节：major 不同或文件 minor 更高一律拒绝。
inline constexpr uint32_t kIndexFormatVersionMajor = 1;
inline constexpr uint32_t kIndexFormatVersionMinor = 0;
inline constexpr uint32_t kIndexFormatVersion =
    (kIndexFormatVersionMajor << 16) | kIndexFormatVersionMinor;

// 记录字符串字段上限（UTF-8 字节数）。
inline constexpr uint32_t kIndexMaxCodeBytes = 128;
inline constexpr uint32_t kIndexMaxTextBytes = 512;
inline constexpr uint32_t kIndexMaxSourceBytes = 64;

// 防御性记录数上限：loader 在分配内存前必须先校验（index-format.md 5.6）。
inline constexpr uint32_t kIndexMaxRecords = 10000000;

// 单条记录最小字节数（codeLen 等最短合法值时），供大小预算校验。
inline constexpr uint32_t kIndexMinRecordBytes = 24;

}  // namespace abc

#endif  // ABC_INDEX_FORMAT_H_
