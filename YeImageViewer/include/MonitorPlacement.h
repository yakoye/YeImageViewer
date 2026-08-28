#pragma once

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <span>
#include <string_view>

namespace MonitorPlacement {

inline constexpr size_t NO_MONITOR = static_cast<size_t>(-1);

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool valid() const { return width() > 0 && height() > 0; }
};

struct Monitor {
    std::wstring_view deviceName;
    Rect workArea;
    bool primary = false;
};

struct Selection {
    size_t index = NO_MONITOR;
    bool matchedRememberedMonitor = false;
};

inline bool equalDeviceName(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size())
        return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index]))
            return false;
    }
    return true;
}

inline Selection select(std::span<const Monitor> monitors, bool rememberLastMonitor,
    std::wstring_view rememberedDeviceName) {
    if (monitors.empty())
        return {};

    if (rememberLastMonitor && !rememberedDeviceName.empty()) {
        for (size_t index = 0; index < monitors.size(); ++index) {
            if (equalDeviceName(monitors[index].deviceName, rememberedDeviceName))
                return { index, true };
        }
    }

    for (size_t index = 0; index < monitors.size(); ++index) {
        if (monitors[index].primary)
            return { index, false };
    }
    return { 0, false };
}

inline Selection selectForImageOpen(std::span<const Monitor> monitors,
    size_t cursorMonitorIndex, bool rememberLastMonitor,
    std::wstring_view rememberedDeviceName) {
    if (cursorMonitorIndex < monitors.size())
        return { cursorMonitorIndex, false };
    return select(monitors, rememberLastMonitor, rememberedDeviceName);
}

inline Rect toRelative(Rect absoluteWindow, Rect workArea) {
    absoluteWindow.left -= workArea.left;
    absoluteWindow.right -= workArea.left;
    absoluteWindow.top -= workArea.top;
    absoluteWindow.bottom -= workArea.top;
    return absoluteWindow;
}

inline Rect restore(Rect relativeWindow, Rect workArea) {
    const int workWidth = std::max(1, workArea.width());
    const int workHeight = std::max(1, workArea.height());
    int width = relativeWindow.valid() ? relativeWindow.width() : std::min(800, workWidth);
    int height = relativeWindow.valid() ? relativeWindow.height() : std::min(600, workHeight);
    width = std::clamp(width, 1, workWidth);
    height = std::clamp(height, 1, workHeight);

    int left = relativeWindow.valid() ? workArea.left + relativeWindow.left :
        workArea.left + (workWidth - width) / 2;
    int top = relativeWindow.valid() ? workArea.top + relativeWindow.top :
        workArea.top + (workHeight - height) / 2;
    left = std::clamp(left, workArea.left, workArea.right - width);
    top = std::clamp(top, workArea.top, workArea.bottom - height);
    return { left, top, left + width, top + height };
}

}
