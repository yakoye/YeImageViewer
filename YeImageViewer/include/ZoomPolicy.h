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


// ── 四个缩放命令共用的规则 ────────────────────────────────────────────────
//
// 这几条规则以前散在 main.cpp 里，既没法单独测，又容易改漏一处（「适应图片」
// 就曾经只改了窗口没改缩放，画面四周露出一圈背景）。集中到这里，配套的用例
// 在 YeImageViewerTests 里。
//
// 命令的分工：
//   适应窗口  只改缩放，窗口不动。整张图缩进当前窗口。
//   实际大小  只改缩放，窗口不动。100%。
//   适应图片  窗口和缩放一起定，让客户区正好等于图片，四边零留白。
//   沉浸显示  只改窗口：无边框铺满工作区。

// 「适应窗口」：整张图缩进窗口，**只缩不放**。
// 图片比窗口小就停在 100%——把一张 100x100 的小图撑满 2000 像素宽的窗口只会
// 糊成一片，多数看图软件都是这个规矩。想放大有滚轮和「输入缩放倍率」。
inline int64_t fitWindowZoom(int sourceWidth, int sourceHeight,
    int windowWidth, int windowHeight, int64_t zoomBase) {
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        windowWidth <= 0 || windowHeight <= 0 || zoomBase <= 0)
        return zoomBase;
    const int64_t fitted = std::min(
        static_cast<int64_t>(windowWidth) * zoomBase / sourceWidth,
        static_cast<int64_t>(windowHeight) * zoomBase / sourceHeight);
    return std::clamp<int64_t>(fitted, 1, zoomBase);
}

// 旋转 90/270 度时宽高互换，缩放要按旋转后的样子算。
inline int64_t fitWindowZoomRotated(int imageWidth, int imageHeight, int rotation,
    int windowWidth, int windowHeight, int64_t zoomBase) {
    const bool upright = (rotation % 2) == 0;
    return fitWindowZoom(upright ? imageWidth : imageHeight,
        upright ? imageHeight : imageWidth, windowWidth, windowHeight, zoomBase);
}

// 预览图（系统缩略图）的像素尺寸和真图无关——一张 280x288 的 SVG，系统给的
// 缩略图是 995x1024。要让预览画在真图将要占据的那个矩形里，就得把「按真图算出
// 的缩放」折算到缩略图的像素上：缩略图宽 × 结果 == 真图宽 × 原缩放。
inline int64_t previewZoom(int64_t zoomForSource, int sourceWidth, int previewWidth) {
    if (sourceWidth <= 0 || previewWidth <= 0 || zoomForSource <= 0)
        return zoomForSource;
    const double scale = static_cast<double>(sourceWidth) / previewWidth;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(zoomForSource * scale)));
}

// 报给用户看的百分比。显示预览时 zoomCur 是相对缩略图像素的，要折回真图口径，
// 否则标题上会先显示 73% 再跳到 8%，而画面其实一动没动。
inline int reportedPercent(int64_t zoomCurrent, double previewToSource, int64_t zoomBase) {
    const int64_t effective = previewToSource > 0.0 ?
        static_cast<int64_t>(std::llround(zoomCurrent / previewToSource)) : zoomCurrent;
    return displayPercent(std::max<int64_t>(1, effective), zoomBase);
}


}
