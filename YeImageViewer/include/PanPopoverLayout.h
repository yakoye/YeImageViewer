#pragma once
#include "OverlayLayout.h"

namespace PanPopoverLayout {
using Rect = OverlayLayout::Rect;
constexpr Rect panel(int width, int height, int dpi = 96) {
    const int scale = OverlayLayout::dpiScale(dpi);
    const int w = std::min(width, OverlayLayout::scaled(300, scale));
    const int h = std::min(height, OverlayLayout::scaled(132, scale));
    const auto hand = OverlayLayout::panMultiplierRect(width, height, dpi);
    const int x = std::clamp(hand.x, 0, std::max(0, width - w));
    const int y = std::max(0, hand.y - h - OverlayLayout::scaled(8, scale));
    return {x, y, w, h};
}
constexpr Rect automatic(const Rect& p) { return {p.x + p.width/20, p.y + p.height/12, p.width*9/10, p.height/3}; }
constexpr Rect slider(const Rect& p) { return {p.x+p.width/12, p.y+p.height*7/12, p.width*5/6, p.height/3}; }
constexpr Rect sliderHit(const Rect& p) {
    auto r = slider(p);
    const int radius = std::max(3,r.height/5);
    r.x -= radius; r.width += radius*2+1;
    return r;
}
constexpr int levelAt(const Rect& p, int x) {
    const auto r = slider(p);
    return std::clamp(((x-r.x)*11 + r.width/2)/std::max(1,r.width), 0, 11);
}
}
