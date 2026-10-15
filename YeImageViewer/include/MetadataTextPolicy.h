#pragma once
#include <cstddef>
namespace MetadataTextPolicy {
// Photoshop 图层块、ICC、超长数组不是应逐字节展开的可读 EXIF。
inline constexpr std::size_t MAX_VALUE_BYTES = 64 * 1024;
inline constexpr bool summarize(std::size_t bytes) { return bytes > MAX_VALUE_BYTES; }
}
