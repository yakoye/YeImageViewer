#pragma once

#include <cstdint>

namespace TextRenderingPolicy {

inline constexpr int LOGICAL_FONT_SIZE = 16;

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

constexpr bool usesNativeClearType(bool adaptiveForeground, bool enhanceGlyphCoverage) {
    return !adaptiveForeground && enhanceGlyphCoverage;
}

constexpr uint8_t enhanceCoverage(uint8_t coverage) {
    if (coverage == 0 || coverage == 255)
        return coverage;
    return static_cast<uint8_t>(coverage +
        (static_cast<uint32_t>(coverage) * (255u - coverage) + 256u) / 512u);
}

}
