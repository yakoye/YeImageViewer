#pragma once

#include <algorithm>
#include <cmath>

namespace PresentationLayout {

inline constexpr int MAX_WIDTH_PERCENT = 90;
inline constexpr int MAX_LANDSCAPE_HEIGHT_NUMERATOR = 33;
inline constexpr int MAX_LANDSCAPE_HEIGHT_DENOMINATOR = 40;
inline constexpr int BOTTOM_RESERVED_DIP = 32;
inline constexpr int WINDOWED_MAX_PERCENT = 90;

struct Result {
    int renderedWidth = 0;
    int renderedHeight = 0;
    int imageLeft = 0;
    int imageTop = 0;
    int initialSlideX = 0;
    int initialSlideY = 0;
    int bottomReservedPixels = 0;
    double scale = 1.0;
    bool portrait = false;
    bool extendsBelowViewport = false;
};

struct WindowedResult {
    int clientWidth = 0;
    int clientHeight = 0;
    int initialSlideX = 0;
    int initialSlideY = 0;
};

inline Result calculate(int imageWidth, int imageHeight, int workAreaWidth,
    int workAreaHeight, int dpi, bool quarterTurn = false) {
    if (imageWidth <= 0 || imageHeight <= 0 || workAreaWidth <= 0 ||
        workAreaHeight <= 0 || dpi <= 0)
        return {};

    const int displayWidth = quarterTurn ? imageHeight : imageWidth;
    const int displayHeight = quarterTurn ? imageWidth : imageHeight;
    const bool portrait = displayHeight > displayWidth;
    const int maximumWidth = std::max(1, workAreaWidth * MAX_WIDTH_PERCENT / 100);
    const int maximumLandscapeHeight = std::max(1,
        workAreaHeight * MAX_LANDSCAPE_HEIGHT_NUMERATOR /
        MAX_LANDSCAPE_HEIGHT_DENOMINATOR);

    const double logicalOneToOneScale = static_cast<double>(dpi) / 96.0;
    const double widthScale = static_cast<double>(maximumWidth) / displayWidth;
    const double heightScale = static_cast<double>(maximumLandscapeHeight) / displayHeight;
    const double scale = portrait ?
        std::min(logicalOneToOneScale, widthScale) :
        std::min({ logicalOneToOneScale, widthScale, heightScale });

    const int renderedWidth = std::max(1,
        static_cast<int>(std::lround(displayWidth * scale)));
    const int renderedHeight = std::max(1,
        static_cast<int>(std::lround(displayHeight * scale)));
    const int bottomReservedPixels = std::max(0,
        static_cast<int>(std::lround(BOTTOM_RESERVED_DIP * logicalOneToOneScale)));
    const int contentHeight = std::max(1, workAreaHeight - bottomReservedPixels);
    const int imageLeft = (workAreaWidth - renderedWidth) / 2;
    const int imageTop = (contentHeight - renderedHeight) / 2;
    const int naturallyCenteredLeft = static_cast<int>(
        std::lround((workAreaWidth - renderedWidth) / 2.0));
    const int naturallyCenteredTop = static_cast<int>(
        std::lround((workAreaHeight - renderedHeight) / 2.0));

    return {
        renderedWidth,
        renderedHeight,
        imageLeft,
        imageTop,
        imageLeft - naturallyCenteredLeft,
        imageTop - naturallyCenteredTop,
        bottomReservedPixels,
        scale,
        portrait,
        imageTop + renderedHeight > workAreaHeight,
    };
}

inline WindowedResult calculateWindowed(const Result& presentation,
    int workAreaWidth, int workAreaHeight) {
    if (presentation.renderedWidth <= 0 || presentation.renderedHeight <= 0 ||
        workAreaWidth <= 0 || workAreaHeight <= 0)
        return {};

    const int maximumWidth = std::max(1, workAreaWidth * WINDOWED_MAX_PERCENT / 100);
    const int maximumHeight = std::max(1, workAreaHeight * WINDOWED_MAX_PERCENT / 100);
    const int clientWidth = std::min(presentation.renderedWidth, maximumWidth);
    const int clientHeight = std::min(presentation.renderedHeight, maximumHeight);
    const int imageLeft = (clientWidth - presentation.renderedWidth) / 2;
    const int imageTop = (clientHeight - presentation.renderedHeight) / 2;
    const int naturallyCenteredLeft = static_cast<int>(
        std::lround((clientWidth - presentation.renderedWidth) / 2.0));
    const int naturallyCenteredTop = static_cast<int>(
        std::lround((clientHeight - presentation.renderedHeight) / 2.0));

    return {
        clientWidth,
        clientHeight,
        imageLeft - naturallyCenteredLeft,
        imageTop - naturallyCenteredTop,
    };
}

}
