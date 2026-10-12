#pragma once

#include <cstdint>

// 固定菜单入口独立于用户配置，不能被「右键退出」或自定义快捷键覆盖。
namespace ContextMenuPolicy {

constexpr bool rightClickOpensMenu(uint32_t rightClickAction, bool controlHeld) {
    return controlHeld || rightClickAction == 0;
}

constexpr bool isFixedMenuKey(uint32_t key, bool controlHeld, bool shiftHeld,
    bool altHeld) {
    return key == 0x79 && shiftHeld && !controlHeld && !altHeld; // Shift + F10
}

}
