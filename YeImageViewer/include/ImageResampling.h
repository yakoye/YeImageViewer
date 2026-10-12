#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ImageResampling {
// 精确覆盖像素面积。先累加预乘颜色再还原，透明边缘不能把不可见的颜色混进来。
inline uint32_t areaBgra(const uint8_t* data, int width, int height, std::size_t step,
    int channels, double left, double top, double right, double bottom) {
    if (!data || width <= 0 || height <= 0 || (channels != 1 && channels != 3 && channels != 4)) return 0;
    left = (std::clamp)(left, 0.0, double(width));
    right = (std::clamp)(right, left, double(width));
    top = (std::clamp)(top, 0.0, double(height));
    bottom = (std::clamp)(bottom, top, double(height));
    double colors[3]{}, alpha = 0, weightSum = 0;
    for (int y = static_cast<int>(top); y < static_cast<int>(std::ceil(bottom)); ++y) {
        const double wy = (std::min)(bottom, double(y + 1)) - (std::max)(top, double(y));
        const auto* row = data + static_cast<std::size_t>(y) * step;
        for (int x = static_cast<int>(left); x < static_cast<int>(std::ceil(right)); ++x) {
            const double weight = wy * ((std::min)(right, double(x + 1)) - (std::max)(left, double(x)));
            const auto* pixel = row + static_cast<std::size_t>(x) * channels;
            const double a = channels == 4 ? pixel[3] : 255;
            for (int c = 0; c < 3; ++c) colors[c] += pixel[channels == 1 ? 0 : c] * a * weight;
            alpha += a * weight;
            weightSum += weight;
        }
    }
    if (alpha <= 0 || weightSum <= 0) return 0;
    uint32_t result = static_cast<uint32_t>(std::lround(alpha / weightSum)) << 24;
    for (int c = 0; c < 3; ++c)
        result |= static_cast<uint32_t>((std::clamp)(std::lround(colors[c] / alpha), 0L, 255L)) << (c * 8);
    return result;
}
}
