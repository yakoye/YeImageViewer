#pragma once

#include <algorithm>

namespace OverlayLayout {

inline constexpr int BASE_TOOLBAR_WIDTH = 595;
inline constexpr int BASE_TOOLBAR_HEIGHT = 50;
inline constexpr int BASE_TOOLBAR_BOTTOM_MARGIN = 20;
inline constexpr int BASE_TOOLBAR_PADDING = 8;
inline constexpr int BASE_BUTTON_SIZE = 34;
inline constexpr int BASE_SMALL_BUTTON_SIZE = 22;
inline constexpr int BASE_ICON_SIZE = 18;
inline constexpr int BASE_SIDE_BUTTON_SIZE = 40;
inline constexpr int SIDE_BUTTON_MARGIN = 16;
inline constexpr int PRESENTATION_CLOSE_SIZE = 42;
inline constexpr int PRESENTATION_CLOSE_MARGIN = 12;

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

constexpr bool shouldDrawPresentationClose(
    bool presentationMode, bool windowHasCaption, bool) {
    return presentationMode || !windowHasCaption;
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

constexpr Rect toolbarPreviousRect(int width, int height) { return baseToolbarButtonRect(width, height, 0); }
constexpr Rect toolbarNextRect(int width, int height) { return baseToolbarButtonRect(width, height, 35); }
constexpr Rect rotateLeftRect(int width, int height) { return baseToolbarButtonRect(width, height, 82); }
constexpr Rect rotateRightRect(int width, int height) { return baseToolbarButtonRect(width, height, 117); }
constexpr Rect flipHorizontalRect(int width, int height) { return baseToolbarButtonRect(width, height, 152); }
constexpr Rect flipVerticalRect(int width, int height) { return baseToolbarButtonRect(width, height, 187); }
constexpr Rect zoomFitRect(int width, int height) { return baseToolbarButtonRect(width, height, 234); }
constexpr Rect zoomActualRect(int width, int height) { return baseToolbarButtonRect(width, height, 269); }
constexpr Rect fullscreenRect(int width, int height) { return baseToolbarButtonRect(width, height, 304); }
constexpr Rect favoriteRect(int width, int height) { return baseToolbarButtonRect(width, height, 351); }
constexpr Rect copyImageRect(int width, int height) { return baseToolbarButtonRect(width, height, 386); }
constexpr Rect deleteImageRect(int width, int height) { return baseToolbarButtonRect(width, height, 421); }
constexpr Rect settingsRect(int width, int height) { return baseToolbarButtonRect(width, height, 456); }
constexpr Rect zoomOutRect(int width, int height) { return baseToolbarButtonRect(width, height, 503, BASE_SMALL_BUTTON_SIZE); }
constexpr Rect zoomTextRect(int width, int height) {
    const int scale = toolbarScale(width);
    const auto toolbar = toolbarRect(width, height);
    const int rectHeight = scaled(BASE_SMALL_BUTTON_SIZE, scale);
    return {
        toolbar.x + scaled(BASE_TOOLBAR_PADDING + 527, scale),
        toolbar.y + (toolbar.height - rectHeight) / 2,
        scaled(36, scale),
        rectHeight,
    };
}
constexpr Rect zoomInRect(int width, int height) { return baseToolbarButtonRect(width, height, 565, BASE_SMALL_BUTTON_SIZE); }

constexpr Rect toolbarRevealRect(int canvasWidth, int canvasHeight) {
    // Keep the trigger exactly on the visible pill; no large invisible strip.
    return toolbarRect(canvasWidth, canvasHeight);
}

constexpr Rect previousImageIconRect(int, int canvasHeight) {
    return {
        SIDE_BUTTON_MARGIN,
        (canvasHeight - BASE_SIDE_BUTTON_SIZE) / 2,
        BASE_SIDE_BUTTON_SIZE,
        BASE_SIDE_BUTTON_SIZE,
    };
}

constexpr Rect nextImageIconRect(int canvasWidth, int canvasHeight) {
    return {
        canvasWidth - SIDE_BUTTON_MARGIN - BASE_SIDE_BUTTON_SIZE,
        (canvasHeight - BASE_SIDE_BUTTON_SIZE) / 2,
        BASE_SIDE_BUTTON_SIZE,
        BASE_SIDE_BUTTON_SIZE,
    };
}

constexpr bool isToolbarControl(Hit hit) {
    return hit >= Hit::ToolbarPreviousImage && hit <= Hit::Toolbar;
}

constexpr Hit hitTest(int canvasWidth, int canvasHeight, int x, int y) {
    if (canvasWidth < 100 || canvasHeight < 100)
        return Hit::None;

    if (presentationCloseRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::PresentationClose;
    if (previousImageIconRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::EdgePreviousImage;
    if (nextImageIconRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::EdgeNextImage;
    if (toolbarPreviousRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarPreviousImage;
    if (toolbarNextRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarNextImage;
    if (rotateLeftRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateLeft;
    if (rotateRightRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateRight;
    if (flipHorizontalRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::FlipHorizontal;
    if (flipVerticalRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::FlipVertical;
    if (zoomFitRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomFit;
    if (zoomActualRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomActual;
    if (fullscreenRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Fullscreen;
    if (favoriteRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Favorite;
    if (copyImageRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::CopyImage;
    if (deleteImageRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::DeleteImage;
    if (settingsRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Settings;
    if (zoomOutRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomOut;
    if (zoomInRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ZoomIn;
    if (toolbarRevealRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Toolbar;
    return Hit::None;
}

static_assert(toolbarRect(800, 600).width == BASE_TOOLBAR_WIDTH);
static_assert(toolbarRect(800, 600).x == (800 - BASE_TOOLBAR_WIDTH) / 2);
static_assert(toolbarRevealRect(800, 600).width == toolbarRect(800, 600).width);
static_assert(previousImageIconRect(800, 600).width == 40);

}
