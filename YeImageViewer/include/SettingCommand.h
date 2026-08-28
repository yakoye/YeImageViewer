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
    AssociationSearch,
    AssociationExtension,
    AssociationDefaults,
    AssociationAll,
    AssociationNone,
    AssociationApply,
    ShortcutWheel,
    ShortcutReset,
    ShortcutBinding,
    AboutProject,
    AboutUpstream,
};

struct Command {
    Kind kind = Kind::None;
    int index = -1;
    int option = -1;

    constexpr bool operator==(const Command&) const = default;
};

constexpr bool contains(const SettingLayout::Rect& rect, int x, int y) {
    return rect.x <= x && x < rect.x + rect.width &&
        rect.y <= y && y < rect.y + rect.height;
}

constexpr Command resolve(int tab, int x, int windowY, int scrollOffset,
    int associationExtensionCount = 0, int associationButtonsY = 0) {
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
        constexpr std::array<int, 4> optionCounts{ 3, 3, 2, 2 };
        constexpr int labelWidth = 138;
        for (int index = 0; index < static_cast<int>(SettingLayout::GENERAL_RADIOS.size()); ++index) {
            const auto& row = SettingLayout::GENERAL_RADIOS[index];
            const SettingLayout::Rect segments{
                row.x + labelWidth, row.y + 5, row.width - labelWidth, row.height - 10 };
            if (!contains(segments, x, y))
                continue;
            const int itemWidth = segments.width / optionCounts[index];
            return { Kind::GeneralRadioOption, index,
                std::clamp((x - segments.x) / itemWidth, 0, optionCounts[index] - 1) };
        }
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
        for (int index = 0; index < 3; ++index) {
            if (contains(SettingLayout::shortcutWheelRow(index), x, y))
                return { Kind::ShortcutWheel, index, -1 };
        }
        if (contains(SettingLayout::SHORTCUT_RESET_BUTTON, x, y))
            return { Kind::ShortcutReset, 0, -1 };
        for (int index = 0; index < SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT; ++index) {
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
    }
    return {};
}

}
