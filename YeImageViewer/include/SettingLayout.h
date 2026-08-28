#pragma once

#include "TextRenderingPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace SettingLayout {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int CANVAS_WIDTH = 620;
inline constexpr int CANVAS_HEIGHT = 620;
inline constexpr int TAB_HEIGHT = 52;
inline constexpr int TAB_WIDTH = CANVAS_WIDTH / 4;
inline constexpr int CONTENT_VIEW_HEIGHT = CANVAS_HEIGHT - TAB_HEIGHT;
inline constexpr int PAGE_PADDING = 20;
inline constexpr int CARD_WIDTH = CANVAS_WIDTH - PAGE_PADDING * 2;
inline constexpr int FONT_SIZE = 16;
inline constexpr int ABOUT_TITLE_FONT_SIZE = FONT_SIZE;
inline constexpr int SCROLLBAR_WIDTH = 4;
inline constexpr int SCROLLBAR_RIGHT_MARGIN = 5;

inline constexpr Rect GENERAL_BEHAVIOR_CARD{ 20, 20, 580, 190 };
inline constexpr Rect GENERAL_DISPLAY_CARD{ 20, 226, 580, 296 };
inline constexpr int GENERAL_CONTENT_HEIGHT = 542;

inline constexpr std::array<Rect, 7> GENERAL_CHECK_BOXES{
    Rect{ 38, 58, 262, 32 },
    Rect{ 318, 58, 262, 32 },
    Rect{ 38, 98, 262, 32 },
    Rect{ 318, 98, 262, 32 },
    Rect{ 38, 138, 262, 32 },
    Rect{ 318, 138, 262, 32 },
    Rect{ 38, 178, 262, 28 },
};

inline constexpr std::array<Rect, 4> GENERAL_RADIOS{
    Rect{ 38, 264, 544, 48 },
    Rect{ 38, 326, 544, 48 },
    Rect{ 38, 388, 544, 48 },
    Rect{ 38, 450, 544, 48 },
};

inline constexpr Rect ASSOCIATION_SEARCH{ 20, 20, 580, 46 };
inline constexpr int ASSOCIATION_GRID_X = 30;
inline constexpr int ASSOCIATION_GRID_Y = 80;
inline constexpr int ASSOCIATION_GRID_COLUMNS = 12;
inline constexpr int ASSOCIATION_TAG_WIDTH = 43;
inline constexpr int ASSOCIATION_TAG_HEIGHT = 30;
inline constexpr int ASSOCIATION_TAG_GAP_X = 3;
inline constexpr int ASSOCIATION_TAG_GAP_Y = 5;
inline constexpr int ASSOCIATION_BUTTON_HEIGHT = 42;

inline constexpr Rect HELP_HEADER{ 20, 20, 580, 38 };
inline constexpr std::array<Rect, 4> HELP_GROUP_HEADERS{
    Rect{ 20, 58, 580, 30 },
    Rect{ 20, 272, 580, 30 },
    Rect{ 20, 440, 580, 30 },
    Rect{ 20, 608, 580, 30 },
};
inline constexpr std::array<Rect, 12> HELP_ITEMS{
    Rect{ 20, 88, 580, 46 },
    Rect{ 20, 134, 580, 46 },
    Rect{ 20, 180, 580, 46 },
    Rect{ 20, 226, 580, 46 },
    Rect{ 20, 302, 580, 46 },
    Rect{ 20, 348, 580, 46 },
    Rect{ 20, 394, 580, 46 },
    Rect{ 20, 470, 580, 46 },
    Rect{ 20, 516, 580, 46 },
    Rect{ 20, 562, 580, 46 },
    Rect{ 20, 638, 580, 46 },
    Rect{ 20, 684, 580, 46 },
};
inline constexpr int HELP_CONTENT_HEIGHT = 750;

inline constexpr Rect ABOUT_HERO_CARD{ 20, 20, 580, 432 };
inline constexpr Rect ABOUT_PROJECT_BUTTON{ 20, 480, 282, 48 };
inline constexpr Rect ABOUT_UPSTREAM_BUTTON{ 318, 480, 282, 48 };
inline constexpr int ABOUT_CONTENT_HEIGHT = 548;

constexpr bool overlaps(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width &&
        left.y < right.y + right.height && right.y < left.y + left.height;
}

constexpr bool isInsidePage(const Rect& rect, int contentHeight) {
    return rect.x >= 0 && rect.y >= 0 &&
        rect.width > 0 && rect.height > 0 &&
        rect.x + rect.width <= CANVAS_WIDTH &&
        rect.y + rect.height <= contentHeight;
}

constexpr int maxScrollOffset(int contentHeight) {
    return std::max(0, contentHeight - CONTENT_VIEW_HEIGHT);
}

constexpr int clampScrollOffset(int contentHeight, int offset) {
    return std::clamp(offset, 0, maxScrollOffset(contentHeight));
}

constexpr int scrollbarThumbHeight(int contentHeight) {
    if (contentHeight <= CONTENT_VIEW_HEIGHT)
        return 0;
    return std::max(32, CONTENT_VIEW_HEIGHT * CONTENT_VIEW_HEIGHT / contentHeight);
}

constexpr int scrollbarThumbY(int contentHeight, int offset) {
    const int thumbHeight = scrollbarThumbHeight(contentHeight);
    if (thumbHeight == 0)
        return TAB_HEIGHT;
    const int trackTravel = CONTENT_VIEW_HEIGHT - thumbHeight - 12;
    const int maxOffset = maxScrollOffset(contentHeight);
    return TAB_HEIGHT + 6 + (maxOffset == 0 ? 0 : trackTravel * clampScrollOffset(contentHeight, offset) / maxOffset);
}

constexpr bool generalControlsAreSeparated() {
    for (std::size_t i = 0; i < GENERAL_CHECK_BOXES.size(); ++i) {
        if (!isInsidePage(GENERAL_CHECK_BOXES[i], GENERAL_CONTENT_HEIGHT))
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
        if (!isInsidePage(GENERAL_RADIOS[i], GENERAL_CONTENT_HEIGHT))
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
        if (!isInsidePage(HELP_ITEMS[i], HELP_CONTENT_HEIGHT))
            return false;
        for (std::size_t j = i + 1; j < HELP_ITEMS.size(); ++j) {
            if (overlaps(HELP_ITEMS[i], HELP_ITEMS[j]))
                return false;
        }
    }
    return true;
}

constexpr bool aboutLayoutIsOrdered() {
    return isInsidePage(ABOUT_HERO_CARD, ABOUT_CONTENT_HEIGHT) &&
        isInsidePage(ABOUT_PROJECT_BUTTON, ABOUT_CONTENT_HEIGHT) &&
        isInsidePage(ABOUT_UPSTREAM_BUTTON, ABOUT_CONTENT_HEIGHT) &&
        !overlaps(ABOUT_HERO_CARD, ABOUT_PROJECT_BUTTON) &&
        !overlaps(ABOUT_HERO_CARD, ABOUT_UPSTREAM_BUTTON) &&
        ABOUT_PROJECT_BUTTON.y == ABOUT_UPSTREAM_BUTTON.y &&
        ABOUT_PROJECT_BUTTON.y + ABOUT_PROJECT_BUTTON.height ==
            ABOUT_CONTENT_HEIGHT - PAGE_PADDING;
}

static_assert(CANVAS_WIDTH == 620 && CANVAS_HEIGHT == 620);
static_assert(TAB_WIDTH * 4 == CANVAS_WIDTH);
static_assert(GENERAL_CONTENT_HEIGHT <= CONTENT_VIEW_HEIGHT);
static_assert(HELP_CONTENT_HEIGHT > CONTENT_VIEW_HEIGHT);
static_assert(ABOUT_CONTENT_HEIGHT <= CONTENT_VIEW_HEIGHT);
static_assert(generalControlsAreSeparated());
static_assert(helpItemsAreSeparated());
static_assert(aboutLayoutIsOrdered());
static_assert(ABOUT_TITLE_FONT_SIZE == FONT_SIZE);

}
