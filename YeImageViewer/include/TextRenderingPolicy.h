#pragma once

#include <cstdint>

namespace TextRenderingPolicy {

inline constexpr int LOGICAL_FONT_SIZE = 18;
inline constexpr int EXIF_SHADOW_OFFSET = 2;
inline constexpr uint32_t EXIF_SHADOW_COLOR = 0xD9000000u;
inline constexpr uint32_t EXIF_TEXT_COLOR = 0xFFF6F8FEu;

constexpr int scaledPixelSize(int logicalSize, uint32_t dpi) {
    if (logicalSize <= 0)
        return 1;
    if (dpi == 0)
        dpi = 96;
    return (logicalSize * static_cast<int>(dpi) + 48) / 96;
}

constexpr int legacyImmersiveExifPixelSize(uint32_t dpi) {
    if (dpi < 144)
        return 16;
    return dpi < 168 ? 24 : 32;
}

constexpr bool usesReadableFramedExif(bool presentationMode) {
    return !presentationMode;
}

constexpr uint8_t enhanceCoverage(uint8_t coverage) {
    if (coverage == 0 || coverage == 255)
        return coverage;
    return static_cast<uint8_t>(coverage +
        (static_cast<uint32_t>(coverage) * (255u - coverage) + 256u) / 512u);
}

}
