#pragma once

#include "ThumbTypes.h"

#include <cstdint>
#include <span>

namespace YeThumbnail {
bool decodeThumbnail(std::span<const uint8_t> data, uint32_t maxEdge, ThumbBitmap& out) noexcept;
}
