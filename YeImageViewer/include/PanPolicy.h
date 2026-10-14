#pragma once
#include "ViewingOptions.h"
#include "SensitivityScale.h"
#include <array>
#include <cmath>

namespace PanOptions {
inline constexpr std::size_t BASE = 352, SLOTS = 8;
inline constexpr std::size_t MODE = BASE + 2, FIXED = BASE + 3, SENSITIVITY = BASE + 4;
inline constexpr uint32_t MAGIC = 0x50414E31;
inline constexpr std::array<double, 12> FACTORS{ .1, .25, .5, .75, 1, 1.25, 1.5, 2, 3, 5, 7, 9 };
inline constexpr std::array<const char*, 12> LABELS{
    "0.1×", "0.25×", "0.5×", "0.75×", "1×", "1.25×", "1.5×", "2×", "3×", "5×", "7×", "9×" };
inline void initialize(uint32_t* s, std::size_t count, bool preserveLegacy = true) {
    if (!s || count < BASE + SLOTS) return;
    if (s[BASE] == MAGIC && s[BASE + 1] == 1) {
        s[SENSITIVITY] = s[SENSITIVITY] <= 8 ? SensitivityScale::fromLegacy(s[SENSITIVITY]) : SensitivityScale::DEFAULT;
        s[BASE + 1] = 2;
    }
    if (s[BASE] != MAGIC || s[BASE + 1] != 2) {
        std::fill_n(s + BASE, SLOTS, 0u);
        s[BASE] = MAGIC; s[BASE + 1] = 2;
        s[MODE] = preserveLegacy ? 1 : 0;
        const auto old = (std::min)(s[ViewingOptions::DRAG_SPEED], 2u);
        s[FIXED] = preserveLegacy ? std::array<uint32_t, 3>{4, 7, 8}[old] : 4;
        s[SENSITIVITY] = SensitivityScale::DEFAULT;
    }
    s[MODE] = s[MODE] ? 1 : 0;
    if (s[FIXED] >= FACTORS.size()) s[FIXED] = 4;
    if (s[SENSITIVITY] >= SensitivityScale::LEVELS) s[SENSITIVITY] = SensitivityScale::DEFAULT;
}
}

namespace PanPolicy {
struct Delta { int x = 0, y = 0; };
// 每次手势单独保存加速状态与亚像素余量，松开即结束，不产生惯性。
class Gesture {
    double gain = 1, distance = 0, residualX = 0, residualY = 0;
    double previousX = 0, previousY = 0;
public:
    void reset() { *this = Gesture{}; }
    Delta move(double dx, double dy, double seconds, int dpi, const uint32_t* s,
        bool precision, bool canX = true, bool canY = true) {
        const double logicalDistance = std::hypot(dx, dy) * 96 / (std::max)(1, dpi);
        // 消息合并或调度暂停时不能截短真实间隔，否则慢拖会被误认成快拖。
        const double speed = logicalDistance / (std::max)(seconds, .001);
        const bool reversed = dx * previousX + dy * previousY < 0;
        previousX = dx; previousY = dy;
        double factor = 1;
        if (precision) { gain = 1; distance = 0; factor = .25; }
        else if (s[PanOptions::MODE]) {
            gain = 1; distance = 0;
            factor = PanOptions::FACTORS[std::min<std::size_t>(s[PanOptions::FIXED], 11)];
        }
        else if (speed <= 90 || reversed) { gain = 1; distance = 0; }
        else {
            if (speed > 180) distance += logicalDistance;
            else distance = 0;
            const double velocity = std::clamp((speed - 90) / 610, 0.0, 1.0);
            const double sustained = speed > 180 ? std::clamp((distance - 120) / 480, 0.0, 1.0) : 0;
            const double maximum = 2 + (std::min)(s[PanOptions::SENSITIVITY], 19u) * .75;
            const double target = 1 + (maximum - 1) * (std::max)(velocity, sustained);
            gain += (target - gain) * (1 - std::exp(-(std::max)(.001, seconds) / .055));
            factor = gain;
        }
        residualX = canX ? residualX + dx * factor : 0;
        residualY = canY ? residualY + dy * factor : 0;
        const Delta result{static_cast<int>(residualX), static_cast<int>(residualY)};
        residualX -= result.x; residualY -= result.y;
        return result;
    }
};
inline double limit(double extent, double viewport) { return (std::max)(0.0, (extent - viewport) / 2); }
}
