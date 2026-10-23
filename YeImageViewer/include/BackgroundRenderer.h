#pragma once

#include <cstdint>

enum class BackgroundMode : uint32_t {
    Transparent = 0,
    White = 1,
    Black = 2,
    FrostedGlass = 3,
    Checkerboard = 4,
};

enum class CheckerboardTone : uint32_t { Automatic = 0, Light = 1, Dark = 2 };

namespace BackgroundRenderer {

inline constexpr int GRID_WIDTH = 12;
inline constexpr uint32_t GRID_DARK = 0xFFEBEBEBu;
inline constexpr uint32_t GRID_LIGHT = 0xFFF5F5F5u;
inline constexpr uint32_t DARK_GRID_DARK = 0xFF303030u;
inline constexpr uint32_t DARK_GRID_LIGHT = 0xFF3C3C3Cu;

constexpr BackgroundMode normalizeMode(uint32_t value) {
    return value <= static_cast<uint32_t>(BackgroundMode::Checkerboard) ?
        static_cast<BackgroundMode>(value) : BackgroundMode::Transparent;
}

constexpr CheckerboardTone normalizeTone(uint32_t value) {
    return value <= 2 ? static_cast<CheckerboardTone>(value) : CheckerboardTone::Automatic;
}

constexpr bool darkCheckerboard(CheckerboardTone tone, uint32_t themeBackground) {
    if (tone != CheckerboardTone::Automatic) return tone == CheckerboardTone::Dark;
    return (299 * ((themeBackground >> 16) & 255) +
        587 * ((themeBackground >> 8) & 255) + 114 * (themeBackground & 255)) < 128000;
}

constexpr uint32_t canvasPixel(
    BackgroundMode mode,
    bool frostedGlassActive,
    int x,
    int y,
    uint32_t themeBackground,
    CheckerboardTone tone = CheckerboardTone::Automatic) {
    switch (mode) {
    case BackgroundMode::White:
        return 0xFFFFFFFFu;
    case BackgroundMode::Black:
        return 0xFF000000u;
    case BackgroundMode::FrostedGlass:
        return frostedGlassActive ? 0x00000000u : themeBackground;
    case BackgroundMode::Transparent:
        return 0;
    case BackgroundMode::Checkerboard:
    default:
        const bool alternate = ((x / GRID_WIDTH + y / GRID_WIDTH) & 1) != 0;
        return darkCheckerboard(tone, themeBackground) ?
            (alternate ? DARK_GRID_DARK : DARK_GRID_LIGHT) :
            (alternate ? GRID_DARK : GRID_LIGHT);
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
    uint32_t themeBackground,
    CheckerboardTone tone = CheckerboardTone::Automatic,
    bool alphaSurfaceActive = true) {
    if (alphaSurfaceActive && (mode == BackgroundMode::Transparent ||
        (mode == BackgroundMode::FrostedGlass && frostedGlassActive))) {
        return premultiplyBgra(source);
    }

    const uint32_t alpha = source >> 24;
    if (alpha == 255) return source;

    const uint32_t background = !alphaSurfaceActive &&
        (mode == BackgroundMode::Transparent || mode == BackgroundMode::FrostedGlass) ?
        (themeBackground | 0xFF000000u) :
        canvasPixel(mode, frostedGlassActive, x, y, themeBackground, tone);
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
