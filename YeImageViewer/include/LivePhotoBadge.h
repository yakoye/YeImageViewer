#pragma once

#include <algorithm>
#include <string_view>

// 实况照片左上角的「实况」标记，交互对齐 macOS「照片」：打开时自动播放一遍
// （按设置，默认静音），鼠标悬停在标记上则连同声音从头播放。
// 这里只管几何，纯函数，可脱离 Win32 单独测试。
namespace LivePhotoBadge {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool empty() const {
        return width <= 0 || height <= 0;
    }

    constexpr bool contains(int px, int py) const {
        return !empty() && px >= x && py >= y && px < x + width && py < y + height;
    }
};

inline constexpr int LOGICAL_HEIGHT = 24;
inline constexpr int LOGICAL_MARGIN = 12;
inline constexpr int LOGICAL_PADDING_LEFT = 7;
inline constexpr int LOGICAL_ICON_SIZE = 16;
inline constexpr int LOGICAL_GAP = 5;
inline constexpr int LOGICAL_PADDING_RIGHT = 10;

constexpr int scaled(int value, int dpi) {
    return (value * dpi + 48) / 96;
}

// 与顶部加载提示同一套估算：非 Latin-1 字符（中文）按 14 逻辑像素，其余按 7。
constexpr int logicalTextWidth(std::wstring_view text) {
    int width = 0;
    for (const wchar_t character : text)
        width += character > 0xFF ? 14 : 7;
    return width;
}

constexpr int logicalWidth(int textWidth) {
    return LOGICAL_PADDING_LEFT + LOGICAL_ICON_SIZE + LOGICAL_GAP + textWidth + LOGICAL_PADDING_RIGHT;
}

// 标记贴在图片可见部分的左上角；图片放大到超出窗口时就贴在窗口左上角。
// 窗口太小放不下时返回空矩形，不画也不参与命中。
constexpr Rect place(Rect image, int canvasWidth, int canvasHeight, int dpi, int textWidth) {
    const int width = scaled(logicalWidth(textWidth), dpi);
    const int height = scaled(LOGICAL_HEIGHT, dpi);
    const int margin = scaled(LOGICAL_MARGIN, dpi);
    if (canvasWidth < width + margin * 2 || canvasHeight < height + margin * 2)
        return {};
    const int visibleLeft = std::clamp(image.x, 0, canvasWidth);
    const int visibleTop = std::clamp(image.y, 0, canvasHeight);
    return {
        std::clamp(visibleLeft + margin, margin, canvasWidth - width - margin),
        std::clamp(visibleTop + margin, margin, canvasHeight - height - margin),
        width,
        height,
    };
}

}
