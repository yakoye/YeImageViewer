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

// 毛玻璃靠 DWM 的亚克力背景实现：画布留空，透出 DWM 在窗口背后绘制的模糊层。
// 沉浸模式用自绘的半透明压暗层，不需要亚克力，所以只有用户显式选毛玻璃才请求。
constexpr bool requestsFrostedGlass(bool presentationMode, BackgroundMode configuredMode) {
    return !presentationMode && configuredMode == BackgroundMode::FrostedGlass;
}

// 图片之外的窗口留白区。此前普通窗口恒为不透明中灰，背景设置只作用于图片自身的
// 透明像素，用户改了设置却看不出变化；现在普通窗口的留白也跟随设置。
//
// alphaSurfaceActive 表示交换链是支持预乘 alpha 的 DirectComposition 表面，
// frostedGlassActive 表示 DWM 亚克力确实启用成功——两者含义不同：前者几乎总是成立，
// 若拿它当毛玻璃的判据，画布会在没有模糊层的情况下变成纯透明，窗口等于消失。
constexpr uint32_t windowCanvasPixel(
    BackgroundMode configuredMode,
    bool presentationMode,
    bool alphaSurfaceActive,
    bool frostedGlassActive,
    int x,
    int y,
    uint32_t themeBackground) {
    // 沉浸模式维持统一的半透明压暗层，让画面聚焦在图片上，不受背景设置影响。
    if (presentationMode)
        return alphaSurfaceActive ? PRESENTATION_TINT : themeBackground;
    return BackgroundRenderer::canvasPixel(
        configuredMode, frostedGlassActive, x, y, themeBackground);
}

// 棋盘格以外的模式整片同色，填充时可以走 std::fill 而不必逐像素求值。
constexpr bool usesUniformWindowCanvas(
    BackgroundMode configuredMode, bool presentationMode) {
    return presentationMode || configuredMode != BackgroundMode::Transparent;
}

}
