#pragma once

#include <array>
#include <cstddef>

namespace SettingLayout {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int CANVAS_WIDTH = 1000;
inline constexpr int CANVAS_HEIGHT = 700;
inline constexpr int TAB_HEIGHT = 50;
inline constexpr int TAB_WIDTH = 150;
inline constexpr int FONT_SIZE = 18;

inline constexpr std::array<Rect, 7> GENERAL_CHECK_BOXES{
    Rect{ 50, 72, 480, 40 },
    Rect{ 50, 116, 480, 40 },
    Rect{ 50, 160, 480, 40 },
    Rect{ 50, 204, 480, 40 },
    Rect{ 50, 248, 480, 40 },
    Rect{ 50, 292, 480, 40 },
    Rect{ 50, 336, 480, 40 },
};

inline constexpr std::array<Rect, 4> GENERAL_RADIOS{
    Rect{ 50, 396, 620, 44 },
    Rect{ 50, 448, 620, 44 },
    Rect{ 50, 500, 480, 44 },
    Rect{ 50, 552, 480, 44 },
};

// The help artwork contains the original wheel rows. These rectangles cover
// only their text, retaining the icons while allowing the live shortcut text
// to describe modifier-wheel behavior in both languages and themes.
inline constexpr std::array<Rect, 3> HELP_WHEEL_HINTS{
    Rect{ 110, 298, 860, 36 },
    Rect{ 110, 335, 860, 36 },
    Rect{ 110, 409, 860, 36 },
};

constexpr bool overlaps(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width &&
        left.y < right.y + right.height && right.y < left.y + left.height;
}

constexpr bool isInsideCanvas(const Rect& rect) {
    return rect.x >= 0 && rect.y >= TAB_HEIGHT &&
        rect.width > 0 && rect.height > 0 &&
        rect.x + rect.width <= CANVAS_WIDTH &&
        rect.y + rect.height <= CANVAS_HEIGHT;
}

constexpr bool generalControlsAreSeparated() {
    for (std::size_t i = 0; i < GENERAL_CHECK_BOXES.size(); ++i) {
        if (!isInsideCanvas(GENERAL_CHECK_BOXES[i]))
            return false;
        for (std::size_t j = i + 1; j < GENERAL_CHECK_BOXES.size(); ++j) {
            if (overlaps(GENERAL_CHECK_BOXES[i], GENERAL_CHECK_BOXES[j]))
                return false;
        }
        for (const auto& radio : GENERAL_RADIOS) {
            if (overlaps(GENERAL_CHECK_BOXES[i], radio))
                return false;
        }
    }
    for (std::size_t i = 0; i < GENERAL_RADIOS.size(); ++i) {
        if (!isInsideCanvas(GENERAL_RADIOS[i]))
            return false;
        for (std::size_t j = i + 1; j < GENERAL_RADIOS.size(); ++j) {
            if (overlaps(GENERAL_RADIOS[i], GENERAL_RADIOS[j]))
                return false;
        }
    }
    return true;
}

constexpr bool helpWheelHintsAreSeparated() {
    for (std::size_t i = 0; i < HELP_WHEEL_HINTS.size(); ++i) {
        if (!isInsideCanvas(HELP_WHEEL_HINTS[i]))
            return false;
        for (std::size_t j = i + 1; j < HELP_WHEEL_HINTS.size(); ++j) {
            if (overlaps(HELP_WHEEL_HINTS[i], HELP_WHEEL_HINTS[j]))
                return false;
        }
    }
    return true;
}

static_assert(generalControlsAreSeparated());
static_assert(helpWheelHintsAreSeparated());

}
