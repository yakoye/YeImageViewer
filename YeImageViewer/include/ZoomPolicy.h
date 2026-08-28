#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ZoomPolicy {

inline constexpr double STEP_FACTOR = 1.15;
inline constexpr int MIN_PERCENT = 1;
inline constexpr int MAX_PERCENT = 10000;
inline constexpr int ANIMATION_DURATION_MS = 220;
inline constexpr int INDICATOR_HOLD_MS = 650;
inline constexpr int INDICATOR_FADE_MS = 250;
inline constexpr int INDICATOR_TOTAL_MS = INDICATOR_HOLD_MS + INDICATOR_FADE_MS;

inline std::vector<int64_t> buildLevels(int64_t zoomBase) {
    std::vector<int> percentages;
    if (zoomBase <= 0)
        return {};

    percentages.push_back(100);
    for (double factor = STEP_FACTOR;; factor *= STEP_FACTOR) {
        const int percent = static_cast<int>(std::lround(100.0 / factor));
        if (percent < MIN_PERCENT)
            break;
        percentages.push_back(percent);
    }
    percentages.push_back(MIN_PERCENT);

    for (double factor = STEP_FACTOR;; factor *= STEP_FACTOR) {
        const int percent = static_cast<int>(std::lround(100.0 * factor));
        if (percent > MAX_PERCENT)
            break;
        percentages.push_back(percent);
    }
    percentages.push_back(MAX_PERCENT);

    std::sort(percentages.begin(), percentages.end());
    percentages.erase(std::unique(percentages.begin(), percentages.end()), percentages.end());

    std::vector<int64_t> levels;
    levels.reserve(percentages.size());
    for (const int percent : percentages)
        levels.push_back(std::max<int64_t>(1,
            std::llround(zoomBase * percent / 100.0)));
    return levels;
}

constexpr double easeSmoothStep(double progress) {
    const double t = std::clamp(progress, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline int displayPercent(int64_t zoom, int64_t zoomBase) {
    if (zoomBase <= 0)
        return 0;
    return static_cast<int>(std::llround(zoom * 100.0 / zoomBase));
}

constexpr int indicatorAlpha(int elapsedMs) {
    if (elapsedMs < 0 || elapsedMs >= INDICATOR_TOTAL_MS)
        return 0;
    if (elapsedMs <= INDICATOR_HOLD_MS)
        return 255;
    return 255 * (INDICATOR_TOTAL_MS - elapsedMs) / INDICATOR_FADE_MS;
}

}
