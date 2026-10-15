#pragma once

#include "ThumbTypes.h"

#include <cstdint>
#include <span>

// 主程序与缩略图共用 PsdCompositeDecoder；不再依赖 psd_sdk 或其 Win32 宏副作用。
namespace YeThumbnail {
bool decodePsd(std::span<const uint8_t> data, ThumbBitmap& out) noexcept;
}
