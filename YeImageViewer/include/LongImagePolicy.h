#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace LongImagePolicy {
// 普通竖幅照片不冒充长图；旋转后的比例和实际纵向溢出同时满足才启用。
constexpr bool isLong(int width, int height, int viewportHeight) {
    return width > 0 && height > viewportHeight && viewportHeight > 0 &&
        static_cast<int64_t>(height) >= static_cast<int64_t>(width) * 3;
}
inline int verticalLimit(int height, int viewportHeight, int64_t zoom, int64_t base) {
    if (base <= 0) return 0;
    return std::max(0, static_cast<int>(std::llround(
        (static_cast<double>(height) * zoom / base - viewportHeight) / 2)));
}
inline int clampVertical(int slide, int height, int viewportHeight, int64_t zoom, int64_t base) {
    const int limit = verticalLimit(height, viewportHeight, zoom, base);
    return std::clamp(slide, -limit, limit);
}
}
