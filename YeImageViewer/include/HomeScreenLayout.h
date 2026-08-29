#pragma once

#include <array>
#include <cstdint>

namespace HomeScreenLayout {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int WIDTH = 500;
inline constexpr int HEIGHT = 350;
inline constexpr int BASE_DPI = 96;
inline constexpr Rect TITLE{ 40, 18, 420, 36 };
inline constexpr Rect SUBTITLE{ 40, 54, 420, 24 };
inline constexpr Rect OPEN_BUTTON{ 36, 92, 428, 66 };
inline constexpr std::array<Rect, 3> GUIDE_CARDS{
    Rect{ 36, 174, 136, 108 },
    Rect{ 182, 174, 136, 108 },
    Rect{ 328, 174, 136, 108 },
};
inline constexpr Rect FOOTER{ 36, 300, 428, 32 };

constexpr int scaleForDpi(int value, int dpi) {
    return dpi > 0 ? (value * dpi + BASE_DPI / 2) / BASE_DPI : value;
}

constexpr Rect nativeCanvas(int dpi) {
    return { 0, 0, scaleForDpi(WIDTH, dpi), scaleForDpi(HEIGHT, dpi) };
}

constexpr bool isInside(const Rect& rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
        rect.x + rect.width <= WIDTH && rect.y + rect.height <= HEIGHT;
}

constexpr bool overlaps(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width &&
        left.y < right.y + right.height && right.y < left.y + left.height;
}

constexpr bool contains(const Rect& rect, int x, int y) {
    return rect.x <= x && x < rect.x + rect.width &&
        rect.y <= y && y < rect.y + rect.height;
}

// Map a client-area click through the rendered home-screen rectangle. This
// keeps the primary action functional when Windows DPI or window fitting scales
// the 500 x 350 canvas.
constexpr bool hitOpenButton(const Rect& renderedHome, int clientX, int clientY) {
    if (renderedHome.width <= 0 || renderedHome.height <= 0 ||
        !contains(renderedHome, clientX, clientY))
        return false;
    const int sourceX = static_cast<int>(
        static_cast<int64_t>(clientX - renderedHome.x) * WIDTH / renderedHome.width);
    const int sourceY = static_cast<int>(
        static_cast<int64_t>(clientY - renderedHome.y) * HEIGHT / renderedHome.height);
    return contains(OPEN_BUTTON, sourceX, sourceY);
}

constexpr bool hasSeparatedGuideCards() {
    for (std::size_t index = 0; index < GUIDE_CARDS.size(); ++index) {
        if (!isInside(GUIDE_CARDS[index]))
            return false;
        for (std::size_t other = index + 1; other < GUIDE_CARDS.size(); ++other) {
            if (overlaps(GUIDE_CARDS[index], GUIDE_CARDS[other]))
                return false;
        }
    }
    return true;
}

inline constexpr bool USES_LEGACY_JARKVIEWER_DIAGRAM = false;
inline constexpr bool USES_RASTER_HERO_IMAGE = false;

static_assert(isInside(TITLE) && isInside(SUBTITLE));
static_assert(isInside(OPEN_BUTTON) && isInside(FOOTER));
static_assert(hasSeparatedGuideCards());

}
