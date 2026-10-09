#pragma once

#include "SettingLayout.h"

#include <algorithm>
#include <array>

namespace SettingCommand {

enum class Kind {
    None,
    Tab,
    Scrollbar,
    GeneralToggle,
    GeneralRadioOption,
    GeneralEditorAdd,
    GeneralEditorRename,
    GeneralEditorPath,
    GeneralEditorRemove,
    AssociationSearch,
    AssociationExtension,
    AssociationDefaults,
    AssociationAll,
    AssociationNone,
    AssociationApply,
    ShortcutWheel,
    ShortcutTargetEdit,
    ShortcutTargetChoose,
    ShortcutTargetClear,
    ShortcutReset,
    ShortcutBinding,
    ShortcutClear,
    AboutProject,
    AboutUpstream,
    AboutDonateToggle,
};

struct Command {
    Kind kind = Kind::None;
    int index = -1;
    int option = -1;

    constexpr bool operator==(const Command&) const = default;
};

// 「常规」页每个单选组有几个选项，顺序与 Setting::generalTabRadioList 一一对应。
// 这张表定分段宽度和命中判定，必须和那边 stringIDs 的个数（减掉开头的标签）相等。
// 曾经不等过：加繁體中文时 stringIDs 从三项变四项，这里还写着 2，于是
// resolve() 报出来的选项下标按两段算，和界面上实际选中的那一项对不上。
// 单元测试会拿 Setting.h 那边的条数反过来核对这张表。
inline constexpr std::array<int, 12> GENERAL_RADIO_OPTION_COUNTS{
    3,  // 切图动画：无/上下/左右
    3,  // 主题：跟随系统/浅色/深色
    3,  // 语言：简体中文/English/繁體中文
    2,  // 鼠标右键
    4,  // 打开方式
    4,  // 双击动作
    2,  // 翻页箭头
    2,  // 拖动行为
    2,  // 直方图
    4,  // 信息面板不透明度
    2,  // 实况声音
    2,  // 全屏信息条
};
static_assert(GENERAL_RADIO_OPTION_COUNTS.size() == SettingLayout::GENERAL_RADIOS.size());

constexpr bool contains(const SettingLayout::Rect& rect, int x, int y) {
    return rect.x <= x && x < rect.x + rect.width &&
        rect.y <= y && y < rect.y + rect.height;
}

constexpr Command resolve(int tab, int x, int windowY, int scrollOffset,
    int associationExtensionCount = 0, int associationButtonsY = 0,
    int externalEditorCount = 0, bool donateExpanded = false) {
    if (windowY < SettingLayout::TAB_HEIGHT) {
        return { Kind::Tab,
            std::clamp(x / SettingLayout::TAB_WIDTH, 0, 3), -1 };
    }
    if (x >= SettingLayout::CANVAS_WIDTH - 16)
        return { Kind::Scrollbar, tab, -1 };

    const int y = windowY - SettingLayout::TAB_HEIGHT + scrollOffset;
    if (tab == 0) {
        for (int index = 0; index < static_cast<int>(SettingLayout::GENERAL_CHECK_BOXES.size()); ++index) {
            if (contains(SettingLayout::GENERAL_CHECK_BOXES[index], x, y))
                return { Kind::GeneralToggle, index, -1 };
        }
        constexpr int labelWidth = 138;
        for (int index = 0; index < static_cast<int>(SettingLayout::GENERAL_RADIOS.size()); ++index) {
            const auto& row = SettingLayout::GENERAL_RADIOS[index];
            const SettingLayout::Rect segments{
                row.x + labelWidth, row.y + 5, row.width - labelWidth, row.height - 10 };
            if (!contains(segments, x, y))
                continue;
            const int itemWidth = segments.width / GENERAL_RADIO_OPTION_COUNTS[index];
            return { Kind::GeneralRadioOption, index,
                std::clamp((x - segments.x) / itemWidth, 0,
                    GENERAL_RADIO_OPTION_COUNTS[index] - 1) };
        }
        for (int index = 0; index < externalEditorCount; ++index) {
            if (contains(SettingLayout::generalEditorName(index), x, y))
                return { Kind::GeneralEditorRename, index, -1 };
            if (contains(SettingLayout::generalEditorPath(index), x, y))
                return { Kind::GeneralEditorPath, index, -1 };
            if (contains(SettingLayout::generalEditorRemove(index), x, y))
                return { Kind::GeneralEditorRemove, index, -1 };
        }
        if (contains(SettingLayout::generalEditorAdd(externalEditorCount), x, y))
            return { Kind::GeneralEditorAdd, externalEditorCount, -1 };
        return {};
    }

    if (tab == 1) {
        if (contains(SettingLayout::ASSOCIATION_SEARCH, x, y))
            return { Kind::AssociationSearch, 0, -1 };
        for (int index = 0; index < associationExtensionCount; ++index) {
            const int column = index % SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const int row = index / SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const SettingLayout::Rect rect{
                SettingLayout::ASSOCIATION_GRID_X + column *
                    (SettingLayout::ASSOCIATION_TAG_WIDTH + SettingLayout::ASSOCIATION_TAG_GAP_X),
                SettingLayout::ASSOCIATION_GRID_Y + row *
                    (SettingLayout::ASSOCIATION_TAG_HEIGHT + SettingLayout::ASSOCIATION_TAG_GAP_Y),
                SettingLayout::ASSOCIATION_TAG_WIDTH,
                SettingLayout::ASSOCIATION_TAG_HEIGHT };
            if (contains(rect, x, y))
                return { Kind::AssociationExtension, index, -1 };
        }
        constexpr std::array<Kind, 4> associationKinds{
            Kind::AssociationDefaults, Kind::AssociationAll,
            Kind::AssociationNone, Kind::AssociationApply };
        for (int index = 0; index < static_cast<int>(associationKinds.size()); ++index) {
            if (contains(SettingLayout::associationButtonRect(index, associationButtonsY), x, y))
                return { associationKinds[index], index, -1 };
        }
        return {};
    }

    if (tab == 2) {
        for (int index = 0; index < 2; ++index) {
            if (contains(SettingLayout::shortcutTargetClear(index), x, y))
                return { Kind::ShortcutTargetClear, index, -1 };
            if (contains(SettingLayout::shortcutTargetPath(index), x, y))
                return { Kind::ShortcutTargetEdit, index, -1 };
            if (contains(SettingLayout::shortcutTargetChoose(index), x, y))
                return { Kind::ShortcutTargetChoose, index, -1 };
        }
        for (int index = 0; index < 3; ++index) {
            if (contains(SettingLayout::shortcutWheelRow(index), x, y))
                return { Kind::ShortcutWheel, index, -1 };
        }
        if (contains(SettingLayout::SHORTCUT_RESET_BUTTON, x, y))
            return { Kind::ShortcutReset, 0, -1 };
        for (int index = 0; index < SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT; ++index) {
            // 清空按钮压在行内，必须先判，否则点它会被当成「开始录制」
            if (contains(SettingLayout::shortcutClearButton(index), x, y))
                return { Kind::ShortcutClear, index, -1 };
            if (contains(SettingLayout::shortcutKeyboardRow(index), x, y))
                return { Kind::ShortcutBinding, index, -1 };
        }
        return {};
    }

    if (tab == 3) {
        if (contains(SettingLayout::ABOUT_PROJECT_BUTTON, x, y))
            return { Kind::AboutProject, 0, -1 };
        if (contains(SettingLayout::ABOUT_UPSTREAM_BUTTON, x, y))
            return { Kind::AboutUpstream, 1, -1 };
        // 收款码那一面铺满整张 hero 卡片，点卡片任何地方都翻回去；收起时只有
        // 底下那一行小字是热区，免得点卡片空白处莫名其妙翻过去。
        if (donateExpanded ? contains(SettingLayout::ABOUT_HERO_CARD, x, y) :
            contains(SettingLayout::ABOUT_DONATE_LINK, x, y))
            return { Kind::AboutDonateToggle, 0, -1 };
    }
    return {};
}

}
