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
// 行高、卡片高度等都按这个字号设计。实际绘制取的是系统界面字体的像素高度（见
// Setting::uiFontPixelSize），这里只作为布局基准，保证行高留得下一行字。
inline constexpr int FONT_SIZE = 16;
inline constexpr int ABOUT_TITLE_FONT_SIZE = FONT_SIZE;
inline constexpr int SCROLLBAR_WIDTH = 4;
inline constexpr int SCROLLBAR_RIGHT_MARGIN = 5;

// 复选框的几何。和单选组一样，定义在卡片之前，好让「行为」卡片的高度、以及它下面
// 所有卡片的起点都从行数推导出来——写死数字的话，增减一个开关就得手算一遍。
inline constexpr int GENERAL_CHECK_FIRST_Y = 54;
inline constexpr int GENERAL_CHECK_PITCH = 36;
inline constexpr int GENERAL_CHECK_HEIGHT = 32;
inline constexpr int GENERAL_CHECK_ROWS = 3;   // 两列排布，共 6 个开关

constexpr Rect generalCheckBoxRect(int index) {
    return { (index % 2 == 0) ? 38 : 318,
        GENERAL_CHECK_FIRST_Y + (index / 2) * GENERAL_CHECK_PITCH, 262,
        GENERAL_CHECK_HEIGHT };
}

inline constexpr int GENERAL_CHECK_BOTTOM =
    GENERAL_CHECK_FIRST_Y + (GENERAL_CHECK_ROWS - 1) * GENERAL_CHECK_PITCH +
    GENERAL_CHECK_HEIGHT;

inline constexpr Rect GENERAL_BEHAVIOR_CARD{ 20, 20, 580, GENERAL_CHECK_BOTTOM + 4 - 20 };

// 显示卡片紧跟在行为卡片下面，间距 16
inline constexpr int GENERAL_DISPLAY_CARD_Y =
    GENERAL_BEHAVIOR_CARD.y + GENERAL_BEHAVIOR_CARD.height + 16;

// 单选组的几何。定义在卡片之前，好让卡片高度和编辑器起点从行数推导出来——
// 写死数字的话，每次增减单选组都得手算一遍，漏改就会和下面的编辑器卡片重叠。
inline constexpr int GENERAL_RADIO_COUNT = 10;
inline constexpr int GENERAL_RADIO_FIRST_Y = GENERAL_DISPLAY_CARD_Y + 38;
inline constexpr int GENERAL_RADIO_PITCH = 48;
inline constexpr int GENERAL_RADIO_HEIGHT = 38;
inline constexpr int GENERAL_RADIO_BOTTOM =
    GENERAL_RADIO_FIRST_Y + (GENERAL_RADIO_COUNT - 1) * GENERAL_RADIO_PITCH +
    GENERAL_RADIO_HEIGHT;

// 显示卡片包住全部单选组，末行下面留 18 的边距
inline constexpr Rect GENERAL_DISPLAY_CARD{ 20, GENERAL_DISPLAY_CARD_Y, 580,
    GENERAL_RADIO_BOTTOM + 18 - GENERAL_DISPLAY_CARD_Y };
inline constexpr int GENERAL_EDITOR_CARD_Y =
    GENERAL_DISPLAY_CARD_Y + GENERAL_DISPLAY_CARD.height + 16;
// 行起点与卡片顶边保持 40 的间距，留出卡片标题。
inline constexpr int GENERAL_EDITOR_ROW_Y = GENERAL_EDITOR_CARD_Y + 40;
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

// 三行两列，行距 36、高度 32，留得下一行字。几何见上面的 generalCheckBoxRect。
inline constexpr std::array<Rect, GENERAL_CHECK_ROWS * 2> GENERAL_CHECK_BOXES{
    generalCheckBoxRect(0), generalCheckBoxRect(1), generalCheckBoxRect(2),
    generalCheckBoxRect(3), generalCheckBoxRect(4), generalCheckBoxRect(5),
};

// 行距从 62 收到 48：十组单选按 62 排会占掉 620 像素，整页显得松垮。
// reserve 里的布尔值（翻页箭头、拖动行为、直方图开关）统一做成两选项单选组，
// 而不是复选框：复选框绑的是 SettingParameter 的 bool 成员，reserve 是 uint32_t。
// 几何常量定义在上面的卡片区，卡片高度要靠它们推导。
constexpr Rect generalRadioRow(int index) {
    return { 38, GENERAL_RADIO_FIRST_Y + index * GENERAL_RADIO_PITCH, 544,
        GENERAL_RADIO_HEIGHT };
}

inline constexpr std::array<Rect, 11> GENERAL_RADIOS{
    generalRadioRow(0), generalRadioRow(1), generalRadioRow(2), generalRadioRow(3),
    generalRadioRow(4), generalRadioRow(5), generalRadioRow(6), generalRadioRow(7),
    generalRadioRow(8), generalRadioRow(9), generalRadioRow(10),
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

inline constexpr Rect SHORTCUT_WHEEL_HEADER{ 36, 36, 548, 32 };
inline constexpr int SHORTCUT_WHEEL_ROW_Y = 72;
inline constexpr int SHORTCUT_WHEEL_ROW_HEIGHT = 42;
inline constexpr Rect SHORTCUT_RESET_BUTTON{ 404, 204, 180, 36 };
inline constexpr Rect SHORTCUT_KEYBOARD_HEADER{ 36, 252, 548, 32 };
inline constexpr int SHORTCUT_KEYBOARD_ROW_Y = 288;
inline constexpr int SHORTCUT_KEYBOARD_ROW_HEIGHT = 40;
// 必须与 ShortcutConfig::Action::Count 一致；这里不引用那个头以保持布局可独立测试，
// 由单元测试断言两者相等。新增动作时只改这一个数，卡片和内容高度会跟着算出来。
inline constexpr int SHORTCUT_KEYBOARD_ROW_COUNT = 32;
inline constexpr int SHORTCUT_KEYBOARD_BOTTOM =
    SHORTCUT_KEYBOARD_ROW_Y + SHORTCUT_KEYBOARD_ROW_COUNT * SHORTCUT_KEYBOARD_ROW_HEIGHT;
inline constexpr Rect SHORTCUT_CARD{ 20, 20, 580, SHORTCUT_KEYBOARD_BOTTOM + 10 - 20 };
inline constexpr int SHORTCUT_CONTENT_HEIGHT =
    SHORTCUT_CARD.y + SHORTCUT_CARD.height + PAGE_PADDING;

constexpr Rect shortcutWheelRow(int index) {
    return { 36, SHORTCUT_WHEEL_ROW_Y + index * SHORTCUT_WHEEL_ROW_HEIGHT,
        548, SHORTCUT_WHEEL_ROW_HEIGHT };
}

constexpr Rect shortcutKeyboardRow(int index) {
    return { 36, SHORTCUT_KEYBOARD_ROW_Y + index * SHORTCUT_KEYBOARD_ROW_HEIGHT,
        548, SHORTCUT_KEYBOARD_ROW_HEIGHT };
}

// 一行里的三块：动作名、当前按键、清空按钮。按键格子留得下 Ctrl+Shift+Alt+某键
// 这样的四键组合，清空按钮单独占一格——取消快捷键得有个看得见的入口。
inline constexpr int SHORTCUT_NAME_WIDTH = 240;
inline constexpr int SHORTCUT_KEY_CELL_X = 256;
inline constexpr int SHORTCUT_KEY_CELL_WIDTH = 250;
inline constexpr int SHORTCUT_CLEAR_X = 512;
inline constexpr int SHORTCUT_CLEAR_SIZE = 30;

constexpr Rect shortcutKeyCell(int index) {
    const auto row = shortcutKeyboardRow(index);
    return { row.x + SHORTCUT_KEY_CELL_X, row.y + 5, SHORTCUT_KEY_CELL_WIDTH,
        row.height - 10 };
}

constexpr Rect shortcutClearButton(int index) {
    const auto row = shortcutKeyboardRow(index);
    return { row.x + SHORTCUT_CLEAR_X, row.y + (row.height - SHORTCUT_CLEAR_SIZE) / 2,
        SHORTCUT_CLEAR_SIZE, SHORTCUT_CLEAR_SIZE };
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
        // 动作名、按键格子、清空按钮三块必须都在行内且互不重叠，
        // 否则点「清空」会落到按键格子上、反而进入录制状态。
        const auto key = shortcutKeyCell(i);
        const auto clear = shortcutClearButton(i);
        const int nameRight = item.x + 8 + SHORTCUT_NAME_WIDTH;
        if (nameRight > key.x || overlaps(key, clear) ||
            key.y < item.y || key.y + key.height > item.y + item.height ||
            clear.y < item.y || clear.y + clear.height > item.y + item.height ||
            clear.x + clear.width > item.x + item.width)
            return false;
        for (int j = i + 1; j < SHORTCUT_KEYBOARD_ROW_COUNT; ++j) {
            if (overlaps(item, shortcutKeyboardRow(j)))
                return false;
        }
    }
    // 卡片要装得下最后一行，内容高度要滚得到卡片底部
    return SHORTCUT_CARD.y + SHORTCUT_CARD.height >= SHORTCUT_KEYBOARD_BOTTOM &&
        isInsidePage(SHORTCUT_CARD, SHORTCUT_CONTENT_HEIGHT);
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
