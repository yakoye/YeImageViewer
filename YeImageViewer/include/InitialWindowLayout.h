#pragma once

#include <algorithm>
#include <cmath>

namespace InitialWindowLayout {

inline constexpr int WINDOW_MAX_WIDTH_PERCENT = 90;
inline constexpr int WINDOW_HEIGHT_PERCENT = 90;
inline constexpr int WINDOW_ASPECT_WIDTH = 4;
inline constexpr int WINDOW_ASPECT_HEIGHT = 3;

struct Result {
    int clientWidth = 800;
    int clientHeight = 600;
    int renderedImageWidth = 0;
    int renderedImageHeight = 0;
    double scale = 1.0;
};

inline Result calculate(int imageWidth, int imageHeight, int workAreaWidth, int workAreaHeight) {
    if (imageWidth <= 0 || imageHeight <= 0 || workAreaWidth <= 0 || workAreaHeight <= 0)
        return {};

    const int maximumWidth = std::max(1, workAreaWidth * WINDOW_MAX_WIDTH_PERCENT / 100);
    int maximumHeight = std::max(1, workAreaHeight * WINDOW_HEIGHT_PERCENT / 100);
    int fixedWindowWidth = maximumHeight * WINDOW_ASPECT_WIDTH / WINDOW_ASPECT_HEIGHT;
    if (fixedWindowWidth > maximumWidth) {
        fixedWindowWidth = maximumWidth;
        maximumHeight = fixedWindowWidth * WINDOW_ASPECT_HEIGHT / WINDOW_ASPECT_WIDTH;
    }

    const double availableWidthScale = static_cast<double>(fixedWindowWidth) / imageWidth;
    const double availableHeightScale = static_cast<double>(maximumHeight) / imageHeight;
    const double scale = std::min({ 1.0, availableWidthScale, availableHeightScale });

    const int renderedWidth = std::max(1, static_cast<int>(std::lround(imageWidth * scale)));
    const int renderedHeight = std::max(1, static_cast<int>(std::lround(imageHeight * scale)));
    return {
        std::clamp(fixedWindowWidth, 1, workAreaWidth),
        std::clamp(maximumHeight, 1, workAreaHeight),
        renderedWidth,
        renderedHeight,
        scale,
    };
}

}
