#pragma once

#include "BackgroundRenderer.h"

namespace BackgroundPolicy {

inline constexpr uint32_t PRESENTATION_TINT = 0x99000000u;

constexpr BackgroundMode imageAreaMode(BackgroundMode configuredMode) {
    return configuredMode;
}

constexpr bool usesPerPixelAlphaSurface() {
    return true;
}

constexpr uint32_t presentationCanvasPixel(bool frostedGlassActive, uint32_t themeBackground) {
    return frostedGlassActive ? PRESENTATION_TINT : themeBackground;
}

}
