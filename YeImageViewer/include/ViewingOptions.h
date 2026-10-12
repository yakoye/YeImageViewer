#pragma once

#include "ViewerOptions.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

// 与既有交互选项、快捷键存储互不重叠；不改变固定设置结构的成员布局。
namespace ViewingOptions {
inline constexpr std::size_t BASE = 320;
inline constexpr std::size_t SLOTS = 20;
inline constexpr uint32_t MAGIC = 0x56574531;
inline constexpr std::size_t LOCK = BASE + 2;
inline constexpr std::size_t SHAPE = BASE + 3;
inline constexpr std::size_t SCALE = BASE + 4;
inline constexpr std::size_t WINDOW = BASE + 5;
inline constexpr std::size_t UPSCALE = BASE + 6;
inline constexpr std::size_t LOOP = BASE + 7;
inline constexpr std::size_t QUALITY = BASE + 8;
inline constexpr std::size_t FRAMES = BASE + 9;
inline constexpr std::size_t READING = BASE + 10;
inline constexpr std::size_t AXIS = BASE + 11;
inline constexpr std::size_t DOUBLE_PAGE = BASE + 12;
inline constexpr std::size_t LONG_IMAGE = BASE + 13;
inline constexpr std::size_t CENTER_LONG = BASE + 14;
inline constexpr std::size_t DRAG_SPEED = BASE + 15;
inline uint32_t changed = 0; // 1=观看几何，2=阅读布局，4=绘制/输入，8/16/32=锁定/形态/缩放选择。

enum class Shape : uint32_t { Normal, Immersive, Fullscreen };
enum class Scale : uint32_t { Fit, Actual, Remember };
enum class Window : uint32_t { Remember, MatchImage };
enum class Quality : uint32_t { High, Fast, Pixel };
enum class LongImage : uint32_t { OriginalTop, Fit };

inline void initialize(uint32_t* storage, std::size_t count, bool preserveLegacy = true) {
    if (!storage || count < BASE + SLOTS) return;
    if (storage[BASE] != MAGIC || storage[BASE + 1] != 1) {
        const auto oldMode = ViewerOptions::openMode(storage);
        std::fill_n(storage + BASE, SLOTS, 0u);
        storage[BASE] = MAGIC;
        storage[BASE + 1] = 1;
        storage[SHAPE] = static_cast<uint32_t>(ViewerOptions::opensImmersive(oldMode) ?
            Shape::Immersive : Shape::Normal);
        storage[WINDOW] = static_cast<uint32_t>(oldMode == ViewerOptions::OpenMode::FitImage ?
            Window::MatchImage : Window::Remember);
        storage[SCALE] = static_cast<uint32_t>(Scale::Fit);
        storage[LOOP] = 1;
        storage[QUALITY] = static_cast<uint32_t>(preserveLegacy ? Quality::Fast : Quality::High);
        storage[CENTER_LONG] = 1;
        storage[LONG_IMAGE] = static_cast<uint32_t>(preserveLegacy ? LongImage::Fit : LongImage::OriginalTop);
        storage[FRAMES] = 1;
    }
    for (const auto slot : {LOCK, UPSCALE, LOOP, READING, AXIS, DOUBLE_PAGE, CENTER_LONG})
        storage[slot] = storage[slot] ? 1u : 0u;
    for (const auto slot : {SHAPE, SCALE, QUALITY, FRAMES, DRAG_SPEED})
        if (storage[slot] > 2) storage[slot] = 0;
    for (const auto slot : {WINDOW, LONG_IMAGE})
        if (storage[slot] > 1) storage[slot] = 0;
}
inline bool enabled(const uint32_t* storage, std::size_t slot) {
    return storage && storage[slot] != 0;
}
inline int dragFactor(const uint32_t* storage) {
    return storage ? static_cast<int>((std::min)(storage[DRAG_SPEED], 2u)) + 1 : 1;
}
inline int neighbor(int index, int count, int direction, bool loop) {
    if (count <= 0) return -1;
    const int target = index + (direction < 0 ? -1 : 1);
    if (loop) return (target + count) % count;
    return std::clamp(target, 0, count - 1);
}
}
