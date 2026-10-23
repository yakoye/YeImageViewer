#pragma once

#include "BackgroundRenderer.h"

namespace BackgroundPolicy {

// 图片外留白固定为原沉浸显示的六成黑透明灰；不得随图片背景选项变化。
inline constexpr uint32_t PRESENTATION_TINT = 0x99000000u;

constexpr BackgroundMode imageAreaMode(BackgroundMode configuredMode) {
    return configuredMode;
}

constexpr bool usesPerPixelAlphaSurface() {
    return true;
}
constexpr bool preferCompatibilityBlur(bool remoteSession,bool forcedProbe){return remoteSession||forcedProbe;}

// 普通/沉浸/全屏统一使用用户选择；毛玻璃由系统合成器模糊背后的实时内容。
constexpr bool requestsFrostedGlass(bool /*presentationMode*/, BackgroundMode configuredMode) {
    return configuredMode == BackgroundMode::FrostedGlass;
}

// 图片外透明灰与五种图片背景色独立；不支持alpha时才降级到不透明主题底。
constexpr uint32_t windowCanvasPixel(
    BackgroundMode /*configuredMode*/,
    bool /*presentationMode*/,
    bool alphaSurfaceActive,
    bool /*frostedGlassActive*/,
    int /*x*/,
    int /*y*/,
    uint32_t themeBackground,
    CheckerboardTone /*tone*/ = CheckerboardTone::Automatic) {
    return alphaSurfaceActive ? PRESENTATION_TINT : themeBackground | 0xFF000000u;
}

// 图片外始终整片同色，可走 std::fill，无须逐像素绘制棋盘格。
constexpr bool usesUniformWindowCanvas(
    BackgroundMode /*configuredMode*/, bool /*presentationMode*/) {
    return true;
}

}
