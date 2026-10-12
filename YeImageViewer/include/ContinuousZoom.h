#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ContinuousZoom {
inline constexpr double START_DELAY_MS = 280;
inline constexpr double RATE = 1.3862943611198906; // 每秒四倍，对数空间中的正反速度一致
inline int64_t at(int64_t start, int direction, double milliseconds, int64_t base) {
    const double elapsed = (std::max)(0.0, milliseconds - START_DELAY_MS) / 1000.0;
    return (std::clamp<int64_t>)(static_cast<int64_t>(std::llround(
        start * std::exp(direction * RATE * (std::min)(elapsed, 10.0)))),
        (std::max<int64_t>)(1, base / 100), base * 100);
}
}
