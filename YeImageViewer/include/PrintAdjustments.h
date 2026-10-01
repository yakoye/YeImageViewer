#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// 打印前的逐像素处理：亮度/对比度、黑白抖动、反相。
//
// 单独拎出来是因为这类错只有纸出来才看得见——界面上不会报错，预览图又小，
// 偏一点根本看不出来。放在这里就能用单元测试钉住，而且不依赖 OpenCV，
// 调用方把 Mat 的数据指针和步长传进来即可（灰度转换留在 Printer 里用
// cv::cvtColor，那个要照着 OpenCV 的系数走，自己重写没有意义）。
namespace PrintAdjustments {

// 亮度和对比度都是 0~200 的整数，100 是「不增不减」。
// 对比度以 128 为中心缩放，亮度是一条伽马曲线。两者都取三次方，
// 让滑块两端的变化更明显——这是按眼睛调出来的，不是什么标准曲线。
inline double contrastFactor(uint32_t contrast) {
    const uint32_t clamped = std::min<uint32_t>(contrast, 200);
    return std::pow(clamped / 100.0, 3);
}

inline double brightnessExponent(uint32_t brightness) {
    const uint32_t clamped = std::clamp<uint32_t>(brightness, 1, 199);
    return std::pow(2.0 - clamped / 100.0, 3);
}

// 单个通道值过一遍亮度对比度。
inline uint8_t adjustSample(uint8_t sample, double contrast, double exponent) {
    // 先按对比度缩放，再夹到 0~255 才能做伽马。
    //
    // 这个夹必须在 pow 之前：对比度大于 1 时暗部会算成负数，
    // pow(负数, 非整数) 是 NaN，而 NaN 转 uchar 的结果是平台说了算的。
    // 夹完再算，每一种可达的参数组合结果都和以前一致（负数那一路以前
    // 也是落到 0），只是不再经过 NaN。
    const double scaled = std::clamp((sample - 128.0) * contrast + 128.0, 0.0, 255.0);
    const double curved = std::pow(scaled / 255.0, exponent) * 255.0;
    return static_cast<uint8_t>(std::clamp(std::lround(curved), 0L, 255L));
}

// 对一块 BGR/BGRA 像素就地做亮度对比度。alpha 通道不动。
inline void applyBrightnessContrast(uint8_t* pixels, int width, int height,
    int channels, size_t stride, uint32_t brightness, uint32_t contrast) {
    if (!pixels || width <= 0 || height <= 0 || channels < 3)
        return;

    const double contrastValue = contrastFactor(contrast);
    const double exponent = brightnessExponent(brightness);

    // 查表：一张 A4 300dpi 的图有八百多万像素，逐个算 pow 太慢，
    // 而输入只有 256 种可能。
    uint8_t lookup[256];
    for (int sample = 0; sample < 256; ++sample)
        lookup[sample] = adjustSample(static_cast<uint8_t>(sample), contrastValue, exponent);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = pixels + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            uint8_t* pixel = row + static_cast<size_t>(x) * channels;
            pixel[0] = lookup[pixel[0]];
            pixel[1] = lookup[pixel[1]];
            pixel[2] = lookup[pixel[2]];
        }
    }
}

// 误差扩散抖动（Floyd–Steinberg），就地处理单通道灰度。
// 黑白打印机只有「有墨」和「没墨」，抖动是用点的疏密换出灰阶的。
inline void floydSteinbergDither(uint8_t* gray, int width, int height, size_t stride) {
    if (!gray || width <= 0 || height <= 0)
        return;

    const auto at = [gray, stride](int x, int y) -> uint8_t& {
        return gray[static_cast<size_t>(y) * stride + static_cast<size_t>(x)];
    };
    const auto spread = [](uint8_t& target, int error, int numerator) {
        const int updated = target + error * numerator / 16;
        target = static_cast<uint8_t>(std::clamp(updated, 0, 255));
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t oldValue = at(x, y);
            const uint8_t newValue = oldValue < 128 ? 0 : 255;
            at(x, y) = newValue;

            const int error = static_cast<int>(oldValue) - static_cast<int>(newValue);
            if (x + 1 < width)
                spread(at(x + 1, y), error, 7);
            if (y + 1 < height) {
                if (x > 0)
                    spread(at(x - 1, y + 1), error, 3);
                spread(at(x, y + 1), error, 5);
                if (x + 1 < width)
                    spread(at(x + 1, y + 1), error, 1);
            }
        }
    }
}

// 反相，只动 B/G/R，alpha 保持原样（反掉透明度会把图变成一块纯色）。
inline void invertColors(uint8_t* pixels, int width, int height, int channels, size_t stride) {
    if (!pixels || width <= 0 || height <= 0 || channels < 3)
        return;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = pixels + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            uint8_t* pixel = row + static_cast<size_t>(x) * channels;
            pixel[0] = static_cast<uint8_t>(255 - pixel[0]);
            pixel[1] = static_cast<uint8_t>(255 - pixel[1]);
            pixel[2] = static_cast<uint8_t>(255 - pixel[2]);
        }
    }
}

} // namespace PrintAdjustments
