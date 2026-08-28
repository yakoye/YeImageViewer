#pragma once

#include <cstddef>

namespace SlideshowPolicy {

inline constexpr int INTERVAL_MS = 3000;

constexpr bool canPlay(std::size_t imageCount) {
    return imageCount > 1;
}

constexpr bool shouldAdvance(bool playing, std::size_t imageCount, bool intervalElapsed) {
    return playing && canPlay(imageCount) && intervalElapsed;
}

}
