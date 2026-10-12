#pragma once
#include <algorithm>
#include <cmath>

namespace AnimationControls {
struct Rect { int x, y, width, height; };
inline Rect bar(int width, int dpi) {
    const int w = (std::min)(width - 16, static_cast<int>(440 * dpi / 96.0));
    return {(width - w) / 2, 8 * dpi / 96, w, 72 * dpi / 96};
}
inline int frameAt(int x, int left, int width, int count) {
    if (count <= 1 || width <= 1) return 0;
    return static_cast<int>(std::llround((std::clamp)(double(x - left) / (width - 1), 0.0, 1.0) * (count - 1)));
}
}
