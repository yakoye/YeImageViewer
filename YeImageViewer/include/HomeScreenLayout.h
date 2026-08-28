#pragma once

#include <array>

namespace HomeScreenLayout {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int WIDTH = 500;
inline constexpr int HEIGHT = 350;
inline constexpr Rect LOGO{ 224, 8, 52, 52 };
inline constexpr Rect TITLE{ 40, 58, 420, 32 };
inline constexpr Rect SUBTITLE{ 40, 86, 420, 22 };
inline constexpr Rect OPEN_CARD{ 36, 116, 428, 58 };
inline constexpr std::array<Rect, 3> GUIDE_CARDS{
    Rect{ 36, 188, 136, 100 },
    Rect{ 182, 188, 136, 100 },
    Rect{ 328, 188, 136, 100 },
};
inline constexpr Rect FOOTER{ 36, 302, 428, 30 };

constexpr bool isInside(const Rect& rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
        rect.x + rect.width <= WIDTH && rect.y + rect.height <= HEIGHT;
}

constexpr bool overlaps(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width &&
        left.y < right.y + right.height && right.y < left.y + left.height;
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

static_assert(isInside(LOGO) && isInside(TITLE) && isInside(SUBTITLE));
static_assert(isInside(OPEN_CARD) && isInside(FOOTER));
static_assert(hasSeparatedGuideCards());

}
