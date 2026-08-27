#pragma once

#include "BackgroundRenderer.h"

namespace BackgroundPolicy {

inline constexpr uint32_t PRESENTATION_TINT = 0x44000000u;

constexpr BackgroundMode canvasMode(bool presentationMode, BackgroundMode configuredMode) {
    return presentationMode ? BackgroundMode::FrostedGlass : configuredMode;
}

constexpr BackgroundMode imageAreaMode(BackgroundMode configuredMode) {
    return configuredMode;
}

constexpr bool requestsFrostedGlass(bool presentationMode, BackgroundMode configuredMode) {
    return presentationMode || configuredMode == BackgroundMode::FrostedGlass;
}

constexpr uint32_t presentationCanvasPixel(bool frostedGlassActive, uint32_t themeBackground) {
    return frostedGlassActive ? PRESENTATION_TINT : themeBackground;
}

}
