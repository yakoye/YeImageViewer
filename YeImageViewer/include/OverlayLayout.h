#pragma once

#include <algorithm>
#include <cstdint>

namespace OverlayLayout {

inline constexpr int BASE_TOOLBAR_WIDTH = 580;
inline constexpr int BASE_TOOLBAR_HEIGHT = 50;
inline constexpr int BASE_TOOLBAR_BOTTOM_MARGIN = 20;
inline constexpr int BASE_TOOLBAR_REVEAL_SIDE_PADDING = 48;
inline constexpr int BASE_TOOLBAR_REVEAL_TOP_PADDING = 12;
inline constexpr int BASE_TOOLBAR_PADDING = 8;
inline constexpr int BASE_BUTTON_SIZE = 34;
inline constexpr int BASE_SMALL_BUTTON_SIZE = 22;
inline constexpr int BASE_ICON_SIZE = 20;
inline constexpr int BASE_ICON_INSET = 6;
inline constexpr int BASE_COMPACT_ICON_INSET = 2;
inline constexpr int TOOLBAR_TEXT_SIZE = 16;
inline constexpr int TOOLBAR_TEXT_BOLD_OFFSET = 0;
inline constexpr int ICON_STROKE_EXPANSION = 0;
inline constexpr int PRESENTATION_CLOSE_SIZE = 42;
inline constexpr int PRESENTATION_CLOSE_MARGIN = 12;
inline constexpr int ZOOM_INDICATOR_WIDTH = 72;
inline constexpr int ZOOM_INDICATOR_HEIGHT = 38;
inline constexpr int ZOOM_INDICATOR_MARGIN = 24;
inline constexpr uint32_t TOOLBAR_BORDER = 0x00000000u;
inline constexpr int BASE_DPI = 96;
inline constexpr int MINIMUM_TOOLBAR_SCALE = 400;
// 两侧翻页按钮画成竖向长方形，比正方形更贴合「翻页」的视觉语言，也更容易点中。
inline constexpr int BASE_EDGE_ARROW_WIDTH = 34;
inline constexpr int BASE_EDGE_ARROW_HEIGHT = 76;
inline constexpr int BASE_EDGE_ARROW_MARGIN = 12;
// 热区比按钮本身宽得多、也高得多：鼠标从画面中间往边上扫时不必精确对准按钮。
inline constexpr int BASE_EDGE_HIT_WIDTH = 72;
inline constexpr int BASE_EDGE_HIT_HEIGHT = 220;
// SVG 图标按基准尺寸的若干倍渲染，绘制时只做 INTER_AREA 缩小。高 DPI 下工具栏会
// 按比例放大，若仍按基准尺寸渲染位图再拉大，图标就是糊的。4 倍覆盖到 384 DPI。
inline constexpr int ICON_SUPERSAMPLE = 4;

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool contains(int pointX, int pointY) const {
        return x <= pointX && pointX < x + width &&
            y <= pointY && pointY < y + height;
    }
};

enum class Hit {
    None,
    EdgePreviousImage,
    EdgeNextImage,
    ToolbarPreviousImage,
    ToolbarPlayPause,
    ToolbarNextImage,
    RotateLeft,
    RotateRight,
    FlipHorizontal,
    FlipVertical,
    ZoomFit,
    ZoomActual,
    Fullscreen,
    Favorite,
    CopyImage,
    DeleteImage,
    Settings,
    ZoomOut,
    ZoomText,
    ZoomIn,
    Toolbar,
    PresentationClose,
};

constexpr int dpiScale(int dpi) {
    return (dpi > 0 ? dpi : BASE_DPI) * 1000 / BASE_DPI;
}

// 进程声明了 PerMonitorHighDPIAware，Windows 不会替我们拉伸，所以工具栏必须自己按
// DPI 放大：此前 scale 只看窗口宽度且封顶 1000，200% 缩放下只有应有尺寸的一半。
// 目标是保持固定的逻辑尺寸，窗口宽度不够时再等比收缩，两者取小。
constexpr int toolbarScale(int canvasWidth, int dpi = BASE_DPI) {
    const int target = dpiScale(dpi);
    const int minimum = MINIMUM_TOOLBAR_SCALE * target / 1000;
    if (canvasWidth <= 16)
        return 600 * target / 1000;
    const int widthScale = (canvasWidth - 16) * 1000 / BASE_TOOLBAR_WIDTH;
    return std::clamp(std::min(widthScale, target), minimum, target);
}

constexpr int scaled(int value, int scale) {
    return std::max(1, (value * scale + 500) / 1000);
}

constexpr int toolbarIconSize(int canvasWidth, const Rect& target,
    bool compactControl = false, int dpi = BASE_DPI) {
    const int scale = toolbarScale(canvasWidth, dpi);
    const int inset = scaled(compactControl ? BASE_COMPACT_ICON_INSET : BASE_ICON_INSET, scale);
    return std::max(10, std::min({ target.width - inset, target.height - inset,
        scaled(BASE_ICON_SIZE, scale) }));
}

constexpr Rect presentationCloseRect(int canvasWidth, int, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int size = scaled(PRESENTATION_CLOSE_SIZE, scale);
    const int margin = scaled(PRESENTATION_CLOSE_MARGIN, scale);
    return { canvasWidth - margin - size, margin, size, size };
}

constexpr Rect zoomIndicatorRect(int, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int width = scaled(ZOOM_INDICATOR_WIDTH, scale);
    const int height = scaled(ZOOM_INDICATOR_HEIGHT, scale);
    const int margin = scaled(ZOOM_INDICATOR_MARGIN, scale);
    return { margin, std::max(0, canvasHeight - margin - height), width, height };
}

constexpr bool shouldDrawPresentationClose(
    bool presentationMode, bool windowHasCaption, bool) {
    return presentationMode || !windowHasCaption;
}

constexpr bool usesTopInfoBar() {
    return false;
}

constexpr bool showsRedundantFileActions() {
    return false;
}

constexpr Rect toolbarRect(int canvasWidth, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = toolbarScale(canvasWidth, dpi);
    const int width = scaled(BASE_TOOLBAR_WIDTH, scale);
    const int height = scaled(BASE_TOOLBAR_HEIGHT, scale);
    const int bottom = scaled(BASE_TOOLBAR_BOTTOM_MARGIN, scale);
    return { (canvasWidth - width) / 2, canvasHeight - bottom - height, width, height };
}

constexpr Rect baseToolbarButtonRect(int canvasWidth, int canvasHeight,
    int baseOffset, int baseSize = BASE_BUTTON_SIZE, int dpi = BASE_DPI) {
    const int scale = toolbarScale(canvasWidth, dpi);
    const auto toolbar = toolbarRect(canvasWidth, canvasHeight, dpi);
    const int size = scaled(baseSize, scale);
    return {
        toolbar.x + scaled(BASE_TOOLBAR_PADDING + baseOffset, scale),
        toolbar.y + (toolbar.height - size) / 2,
        size,
        size,
    };
}

constexpr Rect settingsRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 0, BASE_BUTTON_SIZE, dpi); }
constexpr Rect rotateLeftRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 50, BASE_BUTTON_SIZE, dpi); }
constexpr Rect rotateRightRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 85, BASE_BUTTON_SIZE, dpi); }
constexpr Rect flipHorizontalRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 120, BASE_BUTTON_SIZE, dpi); }
constexpr Rect flipVerticalRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 155, BASE_BUTTON_SIZE, dpi); }
constexpr Rect toolbarPreviousRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 230, BASE_BUTTON_SIZE, dpi); }
constexpr Rect toolbarPlayPauseRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 265, BASE_BUTTON_SIZE, dpi); }
constexpr Rect toolbarNextRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 300, BASE_BUTTON_SIZE, dpi); }
constexpr Rect zoomFitRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 350, BASE_BUTTON_SIZE, dpi); }
constexpr Rect zoomActualRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 385, BASE_BUTTON_SIZE, dpi); }
constexpr Rect fullscreenRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 420, BASE_BUTTON_SIZE, dpi); }
constexpr Rect zoomOutRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 467, BASE_SMALL_BUTTON_SIZE, dpi); }
constexpr Rect zoomTextRect(int width, int height, int dpi = BASE_DPI) {
    const int scale = toolbarScale(width, dpi);
    const auto toolbar = toolbarRect(width, height, dpi);
    const int rectHeight = scaled(BASE_SMALL_BUTTON_SIZE, scale);
    return {
        toolbar.x + scaled(BASE_TOOLBAR_PADDING + 491, scale),
        toolbar.y + (toolbar.height - rectHeight) / 2,
        scaled(50, scale),
        rectHeight,
    };
}
constexpr Rect zoomInRect(int width, int height, int dpi = BASE_DPI) { return baseToolbarButtonRect(width, height, 545, BASE_SMALL_BUTTON_SIZE, dpi); }

// 左右两侧的翻页按钮。默认关闭，由设置里的开关决定是否参与命中与绘制。
constexpr Rect edgePreviousRect(int, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int width = scaled(BASE_EDGE_ARROW_WIDTH, scale);
    const int height = std::min(scaled(BASE_EDGE_ARROW_HEIGHT, scale), canvasHeight);
    const int margin = scaled(BASE_EDGE_ARROW_MARGIN, scale);
    return { margin, (canvasHeight - height) / 2, width, height };
}

constexpr Rect edgeNextRect(int canvasWidth, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int width = scaled(BASE_EDGE_ARROW_WIDTH, scale);
    const int height = std::min(scaled(BASE_EDGE_ARROW_HEIGHT, scale), canvasHeight);
    const int margin = scaled(BASE_EDGE_ARROW_MARGIN, scale);
    return { canvasWidth - margin - width, (canvasHeight - height) / 2, width, height };
}

// 命中用的热区：以按钮为中心向外扩，贴着窗口边缘，从画面中间扫过去就能命中。
constexpr Rect edgePreviousHitRect(int canvasWidth, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int width = std::min(scaled(BASE_EDGE_HIT_WIDTH, scale), canvasWidth / 3);
    const int height = std::min(scaled(BASE_EDGE_HIT_HEIGHT, scale), canvasHeight);
    return { 0, (canvasHeight - height) / 2, width, height };
}

constexpr Rect edgeNextHitRect(int canvasWidth, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = dpiScale(dpi);
    const int width = std::min(scaled(BASE_EDGE_HIT_WIDTH, scale), canvasWidth / 3);
    const int height = std::min(scaled(BASE_EDGE_HIT_HEIGHT, scale), canvasHeight);
    return { canvasWidth - width, (canvasHeight - height) / 2, width, height };
}

constexpr Rect toolbarRevealRect(int canvasWidth, int canvasHeight, int dpi = BASE_DPI) {
    const int scale = toolbarScale(canvasWidth, dpi);
    const auto toolbar = toolbarRect(canvasWidth, canvasHeight, dpi);
    const int sidePadding = scaled(BASE_TOOLBAR_REVEAL_SIDE_PADDING, scale);
    const int topPadding = scaled(BASE_TOOLBAR_REVEAL_TOP_PADDING, scale);
    const int left = std::max(0, toolbar.x - sidePadding);
    const int top = std::max(0, toolbar.y - topPadding);
    const int right = std::min(canvasWidth, toolbar.x + toolbar.width + sidePadding);
    // Extend through the lower window edge so approaching from the taskbar or
    // bottom background reliably reveals the controls.
    return { left, top, right - left, canvasHeight - top };
}

constexpr bool isToolbarControl(Hit hit) {
    return hit >= Hit::ToolbarPreviousImage && hit <= Hit::Toolbar;
}

constexpr Hit hitTest(int canvasWidth, int canvasHeight, int x, int y, int dpi = BASE_DPI,
    bool edgeArrowsEnabled = false) {
    if (canvasWidth < 100 || canvasHeight < 100)
        return Hit::None;

    if (presentationCloseRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::PresentationClose;
    // 边缘翻页排在工具栏之前判定：唤出工具栏的热区很大，会盖住两侧按钮。
    if (edgeArrowsEnabled) {
        if (edgePreviousHitRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::EdgePreviousImage;
        if (edgeNextHitRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::EdgeNextImage;
    }
    if (toolbarPreviousRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ToolbarPreviousImage;
    if (toolbarPlayPauseRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ToolbarPlayPause;
    if (toolbarNextRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ToolbarNextImage;
    if (rotateLeftRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::RotateLeft;
    if (rotateRightRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::RotateRight;
    if (flipHorizontalRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::FlipHorizontal;
    if (flipVerticalRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::FlipVertical;
    if (zoomFitRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ZoomFit;
    if (zoomActualRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ZoomActual;
    if (fullscreenRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::Fullscreen;
    if (settingsRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::Settings;
    if (zoomOutRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ZoomOut;
    if (zoomTextRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ZoomText;
    if (zoomInRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::ZoomIn;
    if (toolbarRevealRect(canvasWidth, canvasHeight, dpi).contains(x, y)) return Hit::Toolbar;
    return Hit::None;
}

static_assert(toolbarRect(800, 600).width == BASE_TOOLBAR_WIDTH);
static_assert(toolbarRect(800, 600).x == (800 - BASE_TOOLBAR_WIDTH) / 2);
static_assert(toolbarRevealRect(800, 600).width == BASE_TOOLBAR_WIDTH +
    BASE_TOOLBAR_REVEAL_SIDE_PADDING * 2);
static_assert(toolbarRevealRect(800, 600).y == toolbarRect(800, 600).y -
    BASE_TOOLBAR_REVEAL_TOP_PADDING);
static_assert(toolbarRevealRect(800, 600).y + toolbarRevealRect(800, 600).height == 600);

}
