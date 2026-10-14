#pragma once
#include "SettingLayout.h"
#include "SensitivityScale.h"
#include <cmath>

namespace SensitivitySliderLayout {
constexpr bool isSlider(int index) { return index == 24 || index == 26; }
constexpr SettingLayout::Rect track(const SettingLayout::Rect& row) {
    return {row.x + 150, row.y + row.height / 2 - 2, row.width - 228, 4};
}
constexpr SettingLayout::Rect hit(const SettingLayout::Rect& row) {
    const auto t = track(row);
    return {t.x - 8, row.y + 5, t.width + 16, row.height - 10};
}
inline int position(const SettingLayout::Rect& row, uint32_t level) {
    const auto t = track(row);
    return t.x + static_cast<int>(std::lround((t.width - 1) * SensitivityScale::fraction(level)));
}
inline int levelAt(const SettingLayout::Rect& row, int x) {
    const auto t = track(row);
    return static_cast<int>(std::lround(std::clamp(x - t.x, 0, t.width - 1) * 19.0 / (t.width - 1)));
}
}
