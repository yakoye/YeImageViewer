#pragma once
#include "OverlayLayout.h"

namespace PanPopoverLayout {
using Rect = OverlayLayout::Rect;
constexpr Rect panel(int width, int height, int dpi = 96) {
    const int scale = OverlayLayout::dpiScale(dpi);
    const int w = std::min(width, OverlayLayout::scaled(300, scale));
    const int h = std::min(height, OverlayLayout::scaled(280, scale));
    const auto hand = OverlayLayout::panMultiplierRect(width, height, dpi);
    const int x = std::clamp(hand.x, 0, std::max(0, width - w));
    const int y = std::max(0, hand.y - h - OverlayLayout::scaled(8, scale));
    return {x, y, w, h};
}
constexpr Rect automatic(const Rect& p) { return {p.x + p.width/20, p.y + p.height/28, p.width*9/10, p.height*3/28}; }
constexpr Rect sensitivityLabel(const Rect& p, int kind) { return {p.x+p.width/20,p.y+p.height*(kind==1?4:20)/28,p.width*9/10,p.height*3/28}; }
constexpr Rect inertiaToggle(const Rect& p) { return {p.x+p.width/20,p.y+p.height*17/28,p.width*9/10,p.height*3/28}; }
constexpr Rect fixedLabel(const Rect& p) { return {p.x+p.width/20,p.y+p.height*10/28,p.width*9/10,p.height*3/28}; }
constexpr Rect slider(const Rect& p, int kind = 0) {
    return {p.x+p.width/12, p.y+p.height*(kind==0?13:(kind==1?7:23))/28, p.width*5/6, p.height*3/28};
}
constexpr Rect sliderHit(const Rect& p, int kind = 0) {
    auto r = slider(p, kind);
    const int radius = std::max(3,r.height/5);
    r.x -= radius; r.width += radius*2+1;
    return r;
}
constexpr int levelAt(const Rect& p, int x, int kind = 0) {
    const auto r = slider(p, kind);
    const int maximum = kind == 0 ? 11 : 19;
    return std::clamp(((x-r.x)*maximum + r.width/2)/std::max(1,r.width), 0, maximum);
}
}
