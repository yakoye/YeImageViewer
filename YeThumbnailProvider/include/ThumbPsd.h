#pragma once

#include "ThumbTypes.h"

#include <cstdint>
#include <span>

// PSD 单独一个编译单元，不和 ThumbDecoders.cpp 合并——psd_sdk 的 PsdPlatform.h
// 会 #define NOMSG / NOUSER / NOGDI 一整串，专门把 windows.h 砍瘦。只要它在
// COM 头之前被包含，后面的 oleidl.h / ocidl.h 就会因为缺 MSG 而编译失败。
// 隔开最省事，也不用担心以后谁调整 include 顺序又踩一次。
namespace YeThumbnail {
bool decodePsd(std::span<const uint8_t> data, ThumbBitmap& out) noexcept;
}
