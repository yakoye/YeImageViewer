#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// 直方图的计算与绘制布局。
//
// 拆成独立头是为了能脱离 Win32 和 OpenCV 单测：Bins 只是四组 256 个计数，
// 取样和归一化都是纯算术。绘制侧只负责把归一化后的高度画出来。
namespace ImageHistogram {

    inline constexpr int HISTOGRAM_BINS = 256;

    struct Bins {
        std::array<uint32_t, HISTOGRAM_BINS> blue{};
        std::array<uint32_t, HISTOGRAM_BINS> green{};
        std::array<uint32_t, HISTOGRAM_BINS> red{};
        std::array<uint32_t, HISTOGRAM_BINS> luma{};
        uint64_t sampled = 0;   // 实际统计了多少像素

        bool empty() const { return sampled == 0; }
    };

    // 大图全像素统计没必要：直方图是形状信息，抽样足够。
    // 这里按目标采样数算出步长，10000×10000 的图也只摸约 25 万个点。
    inline constexpr uint64_t TARGET_SAMPLES = 250'000;

    inline int sampleStride(int width, int height) {
        const uint64_t total = static_cast<uint64_t>(std::max(0, width)) *
            static_cast<uint64_t>(std::max(0, height));
        if (total <= TARGET_SAMPLES)
            return 1;
        // 行列各取 stride，所以按平方根算
        int stride = 1;
        while (total / (static_cast<uint64_t>(stride) * stride) > TARGET_SAMPLES)
            ++stride;
        return stride;
    }

    // BT.601 亮度权重，取整避免浮点。红 299 / 绿 587 / 蓝 114，合计 1000。
    constexpr int lumaOf(int blue, int green, int red) {
        return (red * 299 + green * 587 + blue * 114) / 1000;
    }

    inline void accumulate(Bins& bins, int blue, int green, int red) {
        const auto clampBin = [](int v) { return std::clamp(v, 0, HISTOGRAM_BINS - 1); };
        ++bins.blue[clampBin(blue)];
        ++bins.green[clampBin(green)];
        ++bins.red[clampBin(red)];
        ++bins.luma[clampBin(lumaOf(blue, green, red))];
        ++bins.sampled;
    }

    // 归一化到 0~height 的柱高。
    //
    // 用峰值归一化会被单一色块压平：纯色背景占大半画面时，那一个 bin 高到
    // 其余全贴底，直方图等于什么都没显示。这里裁掉最高的几个 bin 再取峰值，
    // 让剩下的形状展开——看形状才是直方图的用途。
    inline std::vector<int> normalize(const std::array<uint32_t, HISTOGRAM_BINS>& channel,
        int height, int outlierBins = 3) {
        std::vector<int> heights(HISTOGRAM_BINS, 0);
        if (height <= 0)
            return heights;

        std::vector<uint32_t> sorted(channel.begin(), channel.end());
        std::ranges::sort(sorted, std::greater<uint32_t>{});
        const int skip = std::clamp(outlierBins, 0, HISTOGRAM_BINS - 1);
        uint32_t peak = sorted[skip];
        // 裁完全是 0（比如纯色图只有一个 bin 非零）就退回真实峰值，
        // 否则下面会整条除以 0 而全画成满格。
        if (peak == 0)
            peak = sorted[0];
        if (peak == 0)
            return heights;

        for (int i = 0; i < HISTOGRAM_BINS; ++i) {
            const uint64_t value = std::min<uint64_t>(channel[i], peak);
            heights[i] = static_cast<int>(value * height / peak);
        }
        return heights;
    }

    // 从任意行式像素缓冲统计直方图。
    //
    // 做成模板而不是直接收 cv::Mat：单元测试工程不链接 OpenCV，
    // 收一个「给行号返回该行首地址」的回调就能喂合成缓冲，
    // 于是上线跑的和测试跑的是同一段循环，而不是两份各写一遍的近似实现。
    //
    // channels < 3 时按灰度处理，三个通道填同一个值。
    template <typename RowAt>
    Bins accumulateFrom(int width, int height, int channels, RowAt rowAt) {
        Bins bins;
        if (width <= 0 || height <= 0 || channels < 1)
            return bins;
        const int stride = sampleStride(width, height);
        for (int y = 0; y < height; y += stride) {
            const uint8_t* row = rowAt(y);
            if (!row)
                continue;
            for (int x = 0; x < width; x += stride) {
                const uint8_t* pixel = row + static_cast<std::size_t>(x) * channels;
                if (channels >= 3)
                    accumulate(bins, pixel[0], pixel[1], pixel[2]);
                else
                    accumulate(bins, pixel[0], pixel[0], pixel[0]);
            }
        }
        return bins;
    }

    // 面板里直方图区块的逻辑尺寸（未按 DPI 缩放）
    inline constexpr int LOGICAL_HEIGHT = 64;
    inline constexpr int LOGICAL_LABEL_HEIGHT = 18;
    inline constexpr int LOGICAL_BLOCK_HEIGHT = LOGICAL_HEIGHT + LOGICAL_LABEL_HEIGHT;

    // 把 256 个 bin 摊到实际像素宽度上：面板只有两三百像素宽，
    // 一个 bin 未必占满一列，取该列覆盖范围内的最大值，免得细峰被漏掉。
    inline std::vector<int> resampleToWidth(const std::vector<int>& heights, int width) {
        std::vector<int> columns(std::max(0, width), 0);
        if (width <= 0 || heights.empty())
            return columns;
        for (int x = 0; x < width; ++x) {
            const int from = static_cast<int>(static_cast<int64_t>(x) * HISTOGRAM_BINS / width);
            int to = static_cast<int>(static_cast<int64_t>(x + 1) * HISTOGRAM_BINS / width);
            to = std::clamp(to, from + 1, HISTOGRAM_BINS);
            int highest = 0;
            for (int i = from; i < to; ++i)
                highest = std::max(highest, heights[i]);
            columns[x] = highest;
        }
        return columns;
    }

}
