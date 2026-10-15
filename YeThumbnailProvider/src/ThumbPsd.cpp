#include "ThumbPsd.h"
#include "PsdCompositeDecoder.h"

namespace YeThumbnail {
bool decodePsd(std::span<const uint8_t> data, ThumbBitmap& out) noexcept {
    out = {};
    auto image = PsdCompositeDecoder::decode(data, 256ULL * 1024 * 1024);
    if (image.bgra.empty()) return false;
    out.width = image.width;
    out.height = image.height;
    out.hasAlpha = image.hasAlpha;
    out.bgra = std::move(image.bgra);
    return true;
}
}
