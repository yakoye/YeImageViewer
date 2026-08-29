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
inline constexpr int GENERAL_EDITOR_CARD_Y = 538;
inline constexpr int GENERAL_EDITOR_ROW_Y = 578;
inline constexpr int GENERAL_EDITOR_ROW_HEIGHT = 44;
inline constexpr int GENERAL_EDITOR_ROW_GAP = 8;

constexpr Rect generalEditorName(int index) {
    return { 38, GENERAL_EDITOR_ROW_Y + index *
        (GENERAL_EDITOR_ROW_HEIGHT + GENERAL_EDITOR_ROW_GAP), 138,
        GENERAL_EDITOR_ROW_HEIGHT };
}

constexpr Rect generalEditorPath(int index) {
    return { 184, GENERAL_EDITOR_ROW_Y + index *
        (GENERAL_EDITOR_ROW_HEIGHT + GENERAL_EDITOR_ROW_GAP), 292,
        GENERAL_EDITOR_ROW_HEIGHT };
}

constexpr Rect generalEditorRemove(int index) {
    return { 484, GENERAL_EDITOR_ROW_Y + index *
        (GENERAL_EDITOR_ROW_HEIGHT + GENERAL_EDITOR_ROW_GAP), 98,
        GENERAL_EDITOR_ROW_HEIGHT };
}

constexpr Rect generalEditorAdd(int editorCount) {
    return { 408, GENERAL_EDITOR_ROW_Y + editorCount *
        (GENERAL_EDITOR_ROW_HEIGHT + GENERAL_EDITOR_ROW_GAP), 174,
        GENERAL_EDITOR_ROW_HEIGHT };
}

constexpr Rect generalEditorHint(int editorCount) {
    const auto add = generalEditorAdd(editorCount);
    return { 38, add.y + add.height + 8, 544, 28 };
}

constexpr Rect generalEditorCard(int editorCount) {
    const auto hint = generalEditorHint(editorCount);
    return { 20, GENERAL_EDITOR_CARD_Y, 580,
        hint.y + hint.height + 22 - GENERAL_EDITOR_CARD_Y };
}

constexpr int generalContentHeight(int editorCount) {
    const auto card = generalEditorCard(editorCount);
    return card.y + card.height + PAGE_PADDING;
}

inline constexpr int GENERAL_CONTENT_HEIGHT = generalContentHeight(10);

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

constexpr Rect associationButtonRect(int index, int buttonsY) {
    constexpr int gap = 8;
    constexpr int width = (CARD_WIDTH - gap * 3) / 4;
    return { PAGE_PADDING + index * (width + gap), buttonsY, width, ASSOCIATION_BUTTON_HEIGHT };
}

inline constexpr Rect SHORTCUT_CARD{ 20, 20, 580, 1510 };
inline constexpr Rect SHORTCUT_WHEEL_HEADER{ 36, 36, 548, 32 };
inline constexpr int SHORTCUT_WHEEL_ROW_Y = 72;
inline constexpr int SHORTCUT_WHEEL_ROW_HEIGHT = 42;
inline constexpr Rect SHORTCUT_RESET_BUTTON{ 404, 204, 180, 36 };
inline constexpr Rect SHORTCUT_KEYBOARD_HEADER{ 36, 252, 548, 32 };
inline constexpr int SHORTCUT_KEYBOARD_ROW_Y = 288;
inline constexpr int SHORTCUT_KEYBOARD_ROW_HEIGHT = 40;
inline constexpr int SHORTCUT_KEYBOARD_ROW_COUNT = 30;
inline constexpr int SHORTCUT_CONTENT_HEIGHT = 1550;

constexpr Rect shortcutWheelRow(int index) {
    return { 36, SHORTCUT_WHEEL_ROW_Y + index * SHORTCUT_WHEEL_ROW_HEIGHT,
        548, SHORTCUT_WHEEL_ROW_HEIGHT };
}

constexpr Rect shortcutKeyboardRow(int index) {
    return { 36, SHORTCUT_KEYBOARD_ROW_Y + index * SHORTCUT_KEYBOARD_ROW_HEIGHT,
        548, SHORTCUT_KEYBOARD_ROW_HEIGHT };
}

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
    const auto editorCard = generalEditorCard(10);
    if (!isInsidePage(editorCard, GENERAL_CONTENT_HEIGHT) ||
        GENERAL_DISPLAY_CARD.y + GENERAL_DISPLAY_CARD.height >= editorCard.y)
        return false;
    for (int index = 0; index < 10; ++index) {
        const auto name = generalEditorName(index);
        const auto path = generalEditorPath(index);
        const auto remove = generalEditorRemove(index);
        if (!isInsidePage(name, GENERAL_CONTENT_HEIGHT) ||
            !isInsidePage(path, GENERAL_CONTENT_HEIGHT) ||
            !isInsidePage(remove, GENERAL_CONTENT_HEIGHT) ||
            overlaps(name, path) || overlaps(path, remove))
            return false;
    }
    return isInsidePage(generalEditorAdd(10), GENERAL_CONTENT_HEIGHT) &&
        isInsidePage(generalEditorHint(10), GENERAL_CONTENT_HEIGHT);
}

constexpr bool shortcutItemsAreSeparated() {
    for (int i = 0; i < SHORTCUT_KEYBOARD_ROW_COUNT; ++i) {
        const auto item = shortcutKeyboardRow(i);
        if (!isInsidePage(item, SHORTCUT_CONTENT_HEIGHT))
            return false;
        for (int j = i + 1; j < SHORTCUT_KEYBOARD_ROW_COUNT; ++j) {
            if (overlaps(item, shortcutKeyboardRow(j)))
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
static_assert(GENERAL_CONTENT_HEIGHT > CONTENT_VIEW_HEIGHT);
static_assert(SHORTCUT_CONTENT_HEIGHT > CONTENT_VIEW_HEIGHT);
static_assert(ABOUT_CONTENT_HEIGHT <= CONTENT_VIEW_HEIGHT);
static_assert(generalControlsAreSeparated());
static_assert(shortcutItemsAreSeparated());
static_assert(aboutLayoutIsOrdered());
static_assert(ABOUT_TITLE_FONT_SIZE == FONT_SIZE);

}
