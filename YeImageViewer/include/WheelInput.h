#pragma once

#include "ShortcutConfig.h"

#include <cstdint>

namespace WheelInput {

inline constexpr uint32_t SHIFT_FLAG = 0x0004u;
inline constexpr uint32_t CONTROL_FLAG = 0x0008u;

enum class Intent {
    Default,
    ZoomIn,
    ZoomOut,
    PanVertical,
    PanHorizontal,
    PreviousImage,
    NextImage,
};

struct Result {
    Intent intent = Intent::Default;
    int verticalDelta = 0;
    int horizontalDelta = 0;
};

constexpr Result resolveAction(ShortcutConfig::WheelAction action,
    int wheelDelta, int panStep) {
    if (wheelDelta == 0)
        return {};
    switch (action) {
    case ShortcutConfig::WheelAction::Zoom:
        return { wheelDelta > 0 ? Intent::ZoomIn : Intent::ZoomOut, 0, 0 };
    case ShortcutConfig::WheelAction::PanVertical:
        return { Intent::PanVertical, wheelDelta > 0 ? panStep : -panStep, 0 };
    case ShortcutConfig::WheelAction::PanHorizontal:
        return { Intent::PanHorizontal, 0, wheelDelta > 0 ? panStep : -panStep };
    case ShortcutConfig::WheelAction::SwitchImage:
        return {
            wheelDelta > 0 ? Intent::PreviousImage : Intent::NextImage,
            0,
            0,
        };
    }
    return {};
}

constexpr Result resolve(uint32_t flags, int wheelDelta, int panStep,
    ShortcutConfig::WheelAction plainAction,
    ShortcutConfig::WheelAction controlAction,
    ShortcutConfig::WheelAction shiftAction) {
    // Ctrl wins when both modifiers are held, preserving deterministic input.
    const auto action = (flags & CONTROL_FLAG) != 0 ? controlAction :
        (flags & SHIFT_FLAG) != 0 ? shiftAction : plainAction;
    return resolveAction(action, wheelDelta, panStep);
}

constexpr Result resolveDefault(uint32_t flags, int wheelDelta, int panStep) {
    return resolve(flags, wheelDelta, panStep,
        ShortcutConfig::DEFAULT_WHEEL_ACTIONS[0],
        ShortcutConfig::DEFAULT_WHEEL_ACTIONS[1],
        ShortcutConfig::DEFAULT_WHEEL_ACTIONS[2]);
}

}
