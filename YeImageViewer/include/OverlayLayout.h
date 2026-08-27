#pragma once

namespace OverlayLayout {

inline constexpr int ICON_SIZE = 30;
inline constexpr int TOOLBAR_GAP = 6;
inline constexpr int TOOLBAR_RIGHT_MARGIN = 8;
inline constexpr int TOOLBAR_BOTTOM_MARGIN = 6;
inline constexpr int TOOLBAR_REVEAL_PADDING = 10;
inline constexpr int EDGE_HIT_WIDTH = 50;
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
    Settings,
    Toolbar,
    PresentationClose,
};

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
    // The actual borderless state is authoritative during startup resize
    // messages. The SVG is optional decoration and must not hide the button.
    return presentationMode || !windowHasCaption;
}

constexpr int toolbarWidth() {
    return ICON_SIZE * 5 + TOOLBAR_GAP * 4;
}

constexpr Rect toolbarPreviousRect(int canvasWidth, int canvasHeight) {
    return {
        canvasWidth - TOOLBAR_RIGHT_MARGIN - toolbarWidth(),
        canvasHeight - TOOLBAR_BOTTOM_MARGIN - ICON_SIZE,
        ICON_SIZE,
        ICON_SIZE,
    };
}

constexpr Rect toolbarNextRect(int canvasWidth, int canvasHeight) {
    auto rect = toolbarPreviousRect(canvasWidth, canvasHeight);
    rect.x += ICON_SIZE + TOOLBAR_GAP;
    return rect;
}

constexpr Rect rotateLeftRect(int canvasWidth, int canvasHeight) {
    auto rect = toolbarNextRect(canvasWidth, canvasHeight);
    rect.x += ICON_SIZE + TOOLBAR_GAP;
    return rect;
}

constexpr Rect rotateRightRect(int canvasWidth, int canvasHeight) {
    auto rect = rotateLeftRect(canvasWidth, canvasHeight);
    rect.x += ICON_SIZE + TOOLBAR_GAP;
    return rect;
}

constexpr Rect settingsRect(int canvasWidth, int canvasHeight) {
    auto rect = rotateRightRect(canvasWidth, canvasHeight);
    rect.x += ICON_SIZE + TOOLBAR_GAP;
    return rect;
}

constexpr Rect toolbarRevealRect(int canvasWidth, int canvasHeight) {
    const auto left = toolbarPreviousRect(canvasWidth, canvasHeight);
    return {
        left.x - TOOLBAR_REVEAL_PADDING,
        left.y - TOOLBAR_REVEAL_PADDING,
        toolbarWidth() + TOOLBAR_REVEAL_PADDING + TOOLBAR_RIGHT_MARGIN,
        ICON_SIZE + TOOLBAR_REVEAL_PADDING + TOOLBAR_BOTTOM_MARGIN,
    };
}

constexpr Rect previousImageIconRect(int, int canvasHeight) {
    return { 0, (canvasHeight - ICON_SIZE) / 2, ICON_SIZE, ICON_SIZE };
}

constexpr Rect nextImageIconRect(int canvasWidth, int canvasHeight) {
    return {
        canvasWidth - ICON_SIZE,
        (canvasHeight - ICON_SIZE) / 2,
        ICON_SIZE,
        ICON_SIZE,
    };
}

constexpr Hit hitTest(int canvasWidth, int canvasHeight, int x, int y) {
    if (canvasWidth < 100 || canvasHeight < 100) return Hit::None;

    if (presentationCloseRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::PresentationClose;
    if (toolbarPreviousRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarPreviousImage;
    if (toolbarNextRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::ToolbarNextImage;
    if (rotateLeftRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateLeft;
    if (rotateRightRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::RotateRight;
    if (settingsRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Settings;
    if (toolbarRevealRect(canvasWidth, canvasHeight).contains(x, y)) return Hit::Toolbar;

    const int edgeWidth = canvasWidth >= 500 ? EDGE_HIT_WIDTH : canvasWidth / 4;
    if (canvasHeight / 4 <= y && y < canvasHeight * 3 / 4) {
        if (0 <= x && x < edgeWidth) return Hit::EdgePreviousImage;
        if (canvasWidth - edgeWidth < x && x <= canvasWidth) return Hit::EdgeNextImage;
    }
    return Hit::None;
}

}
