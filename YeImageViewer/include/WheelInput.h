#pragma once

#include <cstdint>

namespace WheelInput {

inline constexpr uint32_t SHIFT_FLAG = 0x0004u;
inline constexpr uint32_t CONTROL_FLAG = 0x0008u;

enum class Intent {
    Default,
    PanVertical,
    PreviousImage,
    NextImage,
};

struct Result {
    Intent intent = Intent::Default;
    int verticalDelta = 0;
};

constexpr Result resolve(uint32_t flags, int wheelDelta, int panStep) {
    if (wheelDelta == 0)
        return {};

    // Ctrl wins when both modifiers are held so image switching remains
    // deterministic and never also moves the current image.
    if ((flags & CONTROL_FLAG) != 0) {
        return {
            wheelDelta > 0 ? Intent::PreviousImage : Intent::NextImage,
            0
        };
    }

    if ((flags & SHIFT_FLAG) != 0) {
        return {
            Intent::PanVertical,
            wheelDelta > 0 ? panStep : -panStep
        };
    }

    return {};
}

}
