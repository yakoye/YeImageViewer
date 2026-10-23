#pragma once
#include <algorithm>

// 输入尺寸全部是实际像素；文字宽度由当前 TextDrawer 测得，而非字符数推算。
namespace HintLayout
{
struct Rect
{
    int x, y, width, height;
};
constexpr int scale(int value, int dpi)
{
    return std::max(1, (value * std::max(96, dpi) + 48) / 96);
}
constexpr Rect tooltip(int width, int height, int centerX, int aboveY, int textWidth, int fontHeight, int dpi)
{
    const int margin = std::min(scale(4, dpi), std::max(0, (width - 1) / 2));
    const int w = std::clamp(textWidth + scale(24, dpi), 1, std::max(1, width - 2 * margin));
    const int h = std::min(std::max(1, height), fontHeight + scale(16, dpi));
    return {std::clamp(centerX - w / 2, margin, std::max(margin, width - w - margin)),
            std::clamp(aboveY - h - scale(8, dpi), 0, std::max(0, height - h)), w, h};
}
inline constexpr unsigned SEPARATOR_COLOR = 0xFF45474Du;
} // namespace HintLayout
