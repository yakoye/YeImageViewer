#pragma once

#include "BackgroundRenderer.h"

namespace BackgroundPolicy {

inline constexpr uint32_t PRESENTATION_TINT = 0x99000000u;
inline constexpr uint32_t FRAMED_CANVAS = 0xFF7F7F7Fu;

constexpr BackgroundMode imageAreaMode(BackgroundMode configuredMode) {
    return configuredMode;
}

constexpr bool usesPerPixelAlphaSurface() {
    return true;
}

constexpr uint32_t windowCanvasPixel(
    bool presentationMode,
    bool alphaSurfaceActive,
    uint32_t themeBackground) {
    if (!presentationMode)
        return FRAMED_CANVAS;
    return alphaSurfaceActive ? PRESENTATION_TINT : themeBackground;
}

}
