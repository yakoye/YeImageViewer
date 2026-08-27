#pragma once

namespace ImageViewTransform {

template<typename T>
struct Point {
    T x{};
    T y{};
};

template<typename T>
constexpr Point<T> displayToSource(T displayX, T displayY,
    int displayWidth, int displayHeight, int rotation,
    bool flipHorizontal, bool flipVertical) {
    if (flipHorizontal)
        displayX = static_cast<T>(displayWidth - 1) - displayX;
    if (flipVertical)
        displayY = static_cast<T>(displayHeight - 1) - displayY;

    switch (rotation & 3) {
    case 1:
        return { static_cast<T>(displayHeight - 1) - displayY, displayX };
    case 2:
        return {
            static_cast<T>(displayWidth - 1) - displayX,
            static_cast<T>(displayHeight - 1) - displayY,
        };
    case 3:
        return { displayY, static_cast<T>(displayWidth - 1) - displayX };
    default:
        return { displayX, displayY };
    }
}

}
