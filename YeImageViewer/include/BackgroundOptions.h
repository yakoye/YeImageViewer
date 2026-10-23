#pragma once

#include "BackgroundRenderer.h"
#include <cstddef>

// 独立占用 reserve[368..371]，不移动 4096 字节 DB 中的任何成员。
namespace BackgroundOptions {
inline constexpr std::size_t BASE = 368, SLOTS = 4;
inline constexpr uint32_t MAGIC = 0x42474F50u, VERSION = 1;
inline constexpr std::size_t TONE = BASE + 2;

inline void initialize(uint32_t* storage, std::size_t count) {
    if (!storage || count < BASE + SLOTS) return;
    if (storage[BASE] != MAGIC || storage[BASE + 1] != VERSION) {
        storage[BASE] = MAGIC;
        storage[BASE + 1] = VERSION;
        storage[TONE] = static_cast<uint32_t>(CheckerboardTone::Automatic);
    }
    storage[TONE] = static_cast<uint32_t>(BackgroundRenderer::normalizeTone(storage[TONE]));
}

inline CheckerboardTone tone(const uint32_t* storage) {
    return BackgroundRenderer::normalizeTone(storage[TONE]);
}
}
