#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ImageInterpolation {

inline uint8_t roundedByte(float value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

inline uint32_t sampleBilinearBgra(
    const uint8_t* data,
    int width,
    int height,
    size_t stride,
    int channels,
    float x,
    float y) {
    if (!data || width <= 0 || height <= 0 ||
        (channels != 1 && channels != 3 && channels != 4)) {
        return 0;
    }

    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = x - x0;
    const float fy = y - y0;
    const float weights[4]{
        (1.0f - fx) * (1.0f - fy),
        fx * (1.0f - fy),
        (1.0f - fx) * fy,
        fx * fy
    };
    const uint8_t* pixels[4]{
        data + static_cast<size_t>(y0) * stride + static_cast<size_t>(x0) * channels,
        data + static_cast<size_t>(y0) * stride + static_cast<size_t>(x1) * channels,
        data + static_cast<size_t>(y1) * stride + static_cast<size_t>(x0) * channels,
        data + static_cast<size_t>(y1) * stride + static_cast<size_t>(x1) * channels
    };

    if (channels == 1) {
        float gray = 0.0f;
        for (int i = 0; i < 4; ++i) gray += pixels[i][0] * weights[i];
        const uint32_t value = roundedByte(gray);
        return value | (value << 8) | (value << 16) | 0xFF000000u;
    }

    if (channels == 3) {
        float color[3]{};
        for (int i = 0; i < 4; ++i) {
            for (int channel = 0; channel < 3; ++channel)
                color[channel] += pixels[i][channel] * weights[i];
        }
        return roundedByte(color[0]) |
            (static_cast<uint32_t>(roundedByte(color[1])) << 8) |
            (static_cast<uint32_t>(roundedByte(color[2])) << 16) |
            0xFF000000u;
    }

    // Interpolate premultiplied color so transparent pixels cannot introduce
    // dark or colored fringes around icons and text.
    float alpha = 0.0f;
    float premultiplied[3]{};
    for (int i = 0; i < 4; ++i) {
        const float weightedAlpha = pixels[i][3] * weights[i];
        alpha += weightedAlpha;
        for (int channel = 0; channel < 3; ++channel)
            premultiplied[channel] += pixels[i][channel] * weightedAlpha;
    }

    uint32_t output = static_cast<uint32_t>(roundedByte(alpha)) << 24;
    if (alpha > 0.0f) {
        output |= roundedByte(premultiplied[0] / alpha);
        output |= static_cast<uint32_t>(roundedByte(premultiplied[1] / alpha)) << 8;
        output |= static_cast<uint32_t>(roundedByte(premultiplied[2] / alpha)) << 16;
    }
    return output;
}

}
