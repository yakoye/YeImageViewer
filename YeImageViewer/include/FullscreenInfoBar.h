#pragma once

#include <algorithm>

// 全屏和沉浸预览没有标题栏，那一行「第几张 / 名称 / 尺寸 / 缩放」就没地方看了。
// 这里给它在画面左上角安排一条信息条，内容和标题栏完全一致，由同一个
// WindowTitlePresentation::build 生成，不另起一套格式。
//
// 和「实况」标记同在左上角，会打架：信息条占最上面一行，实况标记让到它下面，
// 由 badgeTopOffset 给出要让开的高度。
namespace FullscreenInfoBar {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool empty() const { return width <= 0 || height <= 0; }
};

inline constexpr int LOGICAL_MARGIN = 12;
inline constexpr int LOGICAL_HEIGHT = 26;
inline constexpr int LOGICAL_PADDING_X = 10;
inline constexpr int LOGICAL_GAP_BELOW = 6;

constexpr int scaled(int logical, int dpi) {
    return logical * dpi / 96;
}

// 信息条贴在画布左上角，宽度由文字宽度决定，最宽不超过画布的一半——
// 再长就会横穿整个画面，比没有还碍眼，超出部分交给绘制方省略。
constexpr Rect place(int canvasWidth, int canvasHeight, int dpi, int textWidth) {
    const int margin = scaled(LOGICAL_MARGIN, dpi);
    const int height = scaled(LOGICAL_HEIGHT, dpi);
    const int padding = scaled(LOGICAL_PADDING_X, dpi);
    const int maxWidth = canvasWidth / 2;
    const int width = std::clamp(textWidth + padding * 2, scaled(60, dpi), maxWidth);
    if (canvasWidth < width + margin * 2 || canvasHeight < height + margin * 2)
        return {};
    return { margin, margin, width, height };
}

// 显示信息条时，「实况」标记要让开的高度（含间距）。不显示时返回 0。
constexpr int badgeTopOffset(bool infoBarVisible, int dpi) {
    return infoBarVisible ? scaled(LOGICAL_HEIGHT + LOGICAL_GAP_BELOW, dpi) : 0;
}

}
