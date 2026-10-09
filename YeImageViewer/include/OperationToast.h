#pragma once

#include "UiLanguage.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

// 主窗口非模态操作提示的文案统一收在这里。消息如何画由 main.cpp 的
// showToast/drawToast 负责；这里只决定三语文案，便于单元测试精确钉住文件名和动作。
namespace OperationToast {

inline constexpr int DURATION_MS = 2200;

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool empty() const { return width <= 0 || height <= 0; }
};

constexpr int scaled(int logical, int dpi) {
    return (logical * dpi + 48) / 96;
}

// 长文件名不能让整条提示消失；限制在窗口内，超长文字由 fitText 从中间省略。
constexpr Rect place(int canvasWidth, int canvasHeight, int dpi, int textWidth,
    bool loading) {
    const int margin = scaled(8, dpi);
    const int height = scaled(28, dpi);
    const int y = scaled(14, dpi) + (loading ? height + scaled(8, dpi) : 0);
    const int maxWidth = std::min(canvasWidth - margin * 2, scaled(560, dpi));
    if (maxWidth < scaled(60, dpi) || canvasHeight < y + height + margin)
        return {};
    const int width = std::clamp(textWidth + 2 * scaled(13, dpi),
        std::min(scaled(120, dpi), maxWidth), maxWidth);
    return { (canvasWidth - width) / 2, y, width, height };
}

// 保留开头的文件名及结尾的操作结果。measure 使用绘制字体的真实宽度，且只在
// 提示或窗口宽度/DPI 变化时调用，避免每帧建字体、量文字。
template <typename Measure>
std::wstring fitText(std::wstring_view text, int maxWidth, Measure measure) {
    std::wstring result(text);
    if (maxWidth <= 0)
        return {};
    if (measure(result) <= maxWidth)
        return result;

    for (std::size_t kept = text.size(); kept > 0; --kept) {
        std::size_t prefix = kept / 2;
        std::size_t suffix = text.size() - (kept - prefix);
        // Windows 的 wchar_t 是 UTF-16；省略边界不要拆开代理对。
        if (prefix > 0 && text[prefix - 1] >= 0xD800 && text[prefix - 1] <= 0xDBFF)
            --prefix;
        if (suffix < text.size() && text[suffix] >= 0xDC00 && text[suffix] <= 0xDFFF)
            ++suffix;
        result.assign(text.substr(0, prefix));
        result.append(L"…");
        result.append(text.substr(suffix));
        if (measure(result) <= maxWidth)
            return result;
    }
    return measure(std::wstring(L"…")) <= maxWidth ? L"…" : L"";
}

enum class Action {
    Copy,
    Move,
    Recycle,
};

inline std::wstring success(Action action, std::wstring_view subject, uint32_t language) {
    switch (action) {
    case Action::Copy: {
        std::wstring result(UiLanguage::pick(language,
            L"已复制到 ", L"Copied to ", L"已複製到 "));
        result.append(subject);
        return result;
    }
    case Action::Move: {
        std::wstring result(UiLanguage::pick(language,
            L"已移动到 ", L"Moved to ", L"已移動到 "));
        result.append(subject);
        return result;
    }
    case Action::Recycle: {
        std::wstring result(subject);
        result.append(UiLanguage::pick(language,
            L"，已移至回收站", L" moved to the Recycle Bin", L"，已移至資源回收筒"));
        return result;
    }
    }
    return std::wstring(subject);
}

}  // namespace OperationToast
