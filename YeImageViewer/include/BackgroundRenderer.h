#pragma once

#include <cstdint>

enum class BackgroundMode : uint32_t {
    Transparent = 0,
    White = 1,
    Black = 2,
    FrostedGlass = 3,
};

namespace BackgroundRenderer {

inline constexpr int GRID_WIDTH = 12;
inline constexpr uint32_t GRID_DARK = 0xFFEBEBEBu;
inline constexpr uint32_t GRID_LIGHT = 0xFFF5F5F5u;

constexpr BackgroundMode normalizeMode(uint32_t value) {
    return value <= static_cast<uint32_t>(BackgroundMode::FrostedGlass) ?
        static_cast<BackgroundMode>(value) : BackgroundMode::Transparent;
}

constexpr uint32_t canvasPixel(
    BackgroundMode mode,
    bool frostedGlassActive,
    int x,
    int y,
    uint32_t themeBackground) {
    switch (mode) {
    case BackgroundMode::White:
        return 0xFFFFFFFFu;
    case BackgroundMode::Black:
        return 0xFF000000u;
    case BackgroundMode::FrostedGlass:
        return frostedGlassActive ? 0x00000000u : themeBackground;
    case BackgroundMode::Transparent:
    default:
        return ((x / GRID_WIDTH + y / GRID_WIDTH) & 1) ? GRID_DARK : GRID_LIGHT;
    }
}

constexpr uint32_t premultiplyBgra(uint32_t source) {
    const uint32_t alpha = source >> 24;
    if (alpha == 255) return source;
    if (alpha == 0) return 0;

    const uint32_t blue = source & 0xFFu;
    const uint32_t green = (source >> 8) & 0xFFu;
    const uint32_t red = (source >> 16) & 0xFFu;
    const uint32_t premultipliedBlue = (blue * alpha + 127) / 255;
    const uint32_t premultipliedGreen = (green * alpha + 127) / 255;
    const uint32_t premultipliedRed = (red * alpha + 127) / 255;
    return alpha << 24 | premultipliedRed << 16 | premultipliedGreen << 8 | premultipliedBlue;
}

constexpr uint32_t compositeBgra(
    uint32_t source,
    BackgroundMode mode,
    bool frostedGlassActive,
    int x,
    int y,
    uint32_t themeBackground) {
    if (mode == BackgroundMode::FrostedGlass && frostedGlassActive) {
        return premultiplyBgra(source);
    }

    const uint32_t alpha = source >> 24;
    if (alpha == 255) return source;

    const uint32_t background = canvasPixel(
        mode, frostedGlassActive, x, y, themeBackground);
    if (alpha == 0) return background;

    const uint32_t inverseAlpha = 255 - alpha;
    const auto blendChannel = [alpha, inverseAlpha](uint32_t foreground, uint32_t behind) constexpr {
        return (foreground * alpha + behind * inverseAlpha + 127) / 255;
    };
    const uint32_t blue = blendChannel(source & 0xFFu, background & 0xFFu);
    const uint32_t green = blendChannel((source >> 8) & 0xFFu, (background >> 8) & 0xFFu);
    const uint32_t red = blendChannel((source >> 16) & 0xFFu, (background >> 16) & 0xFFu);
    return 0xFF000000u | red << 16 | green << 8 | blue;
}

}
