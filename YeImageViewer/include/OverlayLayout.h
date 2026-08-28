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
inline constexpr int TOOLBAR_TEXT_SIZE = 14;
inline constexpr int ICON_STROKE_EXPANSION = 1;
inline constexpr int PRESENTATION_CLOSE_SIZE = 42;
inline constexpr int PRESENTATION_CLOSE_MARGIN = 12;
inline constexpr int ZOOM_INDICATOR_WIDTH = 72;
inline constexpr int ZOOM_INDICATOR_HEIGHT = 38;
inline constexpr int ZOOM_INDICATOR_MARGIN = 24;
inline constexpr uint32_t TOOLBAR_BORDER = 0x00000000u;

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
    ZoomIn,
    Toolbar,
    PresentationClose,
};

constexpr int toolbarScale(int canvasWidth) {
    if (canvasWidth <= 16)
        return 600;
    return std::clamp((canvasWidth - 16) * 1000 / BASE_TOOLBAR_WIDTH, 400, 1000);
}

constexpr int scaled(int value, int scale) {
    return std::max(1, (value * scale + 500) / 1000);
}

constexpr Rect presentationCloseRect(int canvasWidth, int) {
    return {
        canvasWidth - PRESENTATION_CLOSE_MARGIN - PRESENTATION_CLOSE_SIZE,
        PRESENTATION_CLOSE_MARGIN,
        PRESENTATION_CLOSE_SIZE,
        PRESENTATION_CLOSE_SIZE,
    };
}

constexpr Rect zoomIndicatorRect(int, int canvasHeight) {
    return {
        ZOOM_INDICATOR_MARGIN,
        std::max(0, canvasHeight - ZOOM_INDICATOR_MARGIN - ZOOM_INDICATOR_HEIGHT),
        ZOOM_INDICATOR_WIDTH,
        ZOOM_INDICATOR_HEIGHT,
    };
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

constexpr Rect toolbarRect(int canvasWidth, int canvasHeight) {
    const int scale = toolbarScale(canvasWidth);
    const int width = scaled(BASE_TOOLBAR_WIDTH, scale);
    const int height = scaled(BASE_TOOLBAR_HEIGHT, scale);
    const int bottom = scaled(BASE_TOOLBAR_BOTTOM_MARGIN, scale);
    return { (canvasWidth - width) / 2, canvasHeight - bottom - height, width, height };
}

constexpr Rect baseToolbarButtonRect(int canvasWidth, int canvasHeight,
    int baseOffset, int baseSize = BASE_BUTTON_SIZE) {
    const int scale = toolbarScale(canvasWidth);
    const auto toolbar = toolbarRect(canvasWidth, canvasHeight);
    const int size = scaled(baseSize, scale);
    return {
        toolbar.x + scaled(BASE_TOOLBAR_PADDING + baseOffset, scale),
        toolbar.y + (toolbar.height - size) / 2,
        size,
        size,
    };
}

constexpr Rect settingsRect(int width, int height) { return baseToolbarButtonRect(width, height, 0); }
constexpr Rect rotateLeftRect(int width, int height) { return baseToolbarButtonRect(width, height, 50); }
constexpr Rect rotateRightRect(int width, int height) { return baseToolbarButtonRect(width, height, 85); }
constexpr Rect flipHorizontalRect(int width, int height) { return baseToolbarButtonRect(width, height, 120); }
constexpr Rect flipVerticalRect(int width, int height) { return baseToolbarButtonRect(width, height, 155); }
constexpr Rect toolbarPreviousRect(int width, int height) { return baseToolbarButtonRect(width, height, 230); }
constexpr Rect toolbarPlayPauseRect(int width, int height) { return baseToolbarButtonRect(width, height, 265); }
constexpr Rect toolbarNextRect(int width, int height) { return baseToolbarButtonRect(width, height, 300); }
constexpr Rect zoomFitRect(int width, int height) { return baseToolbarButtonRect(width, height, 350); }
constexpr Rect zoomActualRect(int width, int height) { return baseToolbarButtonRect(width, height, 385); }
constexpr Rect fullscreenRect(int width, int height) { return baseToolbarButtonRect(width, height, 420); }
constexpr Rect zoomOutRect(int width, int height) { return baseToolbarButtonRect(width, height, 467, BASE_SMALL_BUTTON_SIZE); }
constexpr Rect zoomTextRect(int width, int height) {
    const int scale = toolbarScale(width);
    const auto toolbar = toolbarRect(width, height);
    const int rectHeight = scaled(BASE_SMALL_BUTTON_SIZE, scale);
    return {
        toolbar.x + scaled(BASE_TOOLBAR_PADDING + 491, scale),
        toolbar.y + (toolbar.height - rectHeight) / 2,
        scaled(36, scale),
        rectHeight,
    };
}
constexpr Rect zoomInRect(int width, int height) { return baseToolbarButtonRect(width, height, 529, BASE_SMALL_BUTTON_SIZE); }

constexpr Rect toolbarRevealRect(int canvasWidth, int canvasHeight) {
    const int scale = toolbarScale(canvasWidth);
    const auto toolbar = toolbarRect(canvasWidth, canvasHeight);
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

constexpr Hit hitTest(int canvasWidth, int canvasHeight, int x, int y) {
    if (canvasWidth < 100 || canvasHeight < 100)
        return Hit::None;

    if (presentationCloseRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::PresentationClose;
    if (toolbarPreviousRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarPreviousImage;
    if (toolbarPlayPauseRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarPlayPause;
    if (toolbarNextRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarNextImage;
    if (rotateLeftRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateLeft;
    if (rotateRightRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateRight;
    if (flipHorizontalRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::FlipHorizontal;
    if (flipVerticalRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::FlipVertical;
    if (zoomFitRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomFit;
    if (zoomActualRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomActual;
    if (fullscreenRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Fullscreen;
    if (settingsRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Settings;
    if (zoomOutRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomOut;
    if (zoomInRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomIn;
    if (toolbarRevealRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Toolbar;
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
