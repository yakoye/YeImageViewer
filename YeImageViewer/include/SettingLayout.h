#pragma once

#include "TextRenderingPolicy.h"

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
inline constexpr int TAB_WIDTH = CANVAS_WIDTH / 4;
inline constexpr int FONT_SIZE = TextRenderingPolicy::LOGICAL_FONT_SIZE;
inline constexpr int ABOUT_TITLE_FONT_SIZE = FONT_SIZE;

inline constexpr std::array<Rect, 7> GENERAL_CHECK_BOXES{
    Rect{ 50, 80, 430, 40 },
    Rect{ 520, 80, 430, 40 },
    Rect{ 50, 130, 430, 40 },
    Rect{ 520, 130, 430, 40 },
    Rect{ 50, 180, 430, 40 },
    Rect{ 520, 180, 430, 40 },
    Rect{ 50, 230, 430, 40 },
};

inline constexpr std::array<Rect, 4> GENERAL_RADIOS{
    Rect{ 50, 310, 430, 44 },
    Rect{ 520, 310, 430, 44 },
    Rect{ 50, 380, 430, 44 },
    Rect{ 520, 380, 430, 44 },
};

inline constexpr std::array<Rect, 12> HELP_ITEMS{
    Rect{ 50, 80, 430, 60 },
    Rect{ 520, 80, 430, 60 },
    Rect{ 50, 155, 430, 60 },
    Rect{ 520, 155, 430, 60 },
    Rect{ 50, 230, 430, 60 },
    Rect{ 520, 230, 430, 60 },
    Rect{ 50, 305, 430, 60 },
    Rect{ 520, 305, 430, 60 },
    Rect{ 50, 380, 430, 60 },
    Rect{ 520, 380, 430, 60 },
    Rect{ 50, 455, 430, 60 },
    Rect{ 520, 455, 430, 60 },
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

constexpr bool helpItemsAreSeparated() {
    for (std::size_t i = 0; i < HELP_ITEMS.size(); ++i) {
        if (!isInsideCanvas(HELP_ITEMS[i]))
            return false;
        for (std::size_t j = i + 1; j < HELP_ITEMS.size(); ++j) {
            if (overlaps(HELP_ITEMS[i], HELP_ITEMS[j]))
                return false;
        }
    }
    return true;
}

static_assert(generalControlsAreSeparated());
static_assert(helpItemsAreSeparated());
static_assert(TAB_WIDTH * 4 == CANVAS_WIDTH);
static_assert(ABOUT_TITLE_FONT_SIZE == FONT_SIZE);

}
