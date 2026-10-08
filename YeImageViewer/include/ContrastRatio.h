#pragma once

#include <cmath>
#include <cstdint>

// 文字颜色够不够清楚，是能算出来的，不必靠眼睛在某一套主题下瞄一眼就算数。
// WCAG 2.1 的相对亮度和对比度公式：正文要 4.5:1，图标这类非文字内容要 3:1。
//
// 单独成头是为了能单测：设置页那边的颜色一改，测试立刻能说出它在自己那块底色上
// 还剩几比几。踩过一次——赞赏入口的蓝直接套用了信息面板那套强调色，而设置页的
// 卡片底比信息面板的面板底亮一截，同一个蓝摆过来只剩 4.48:1，看着「好像还行」，
// 其实已经低于正文该有的 4.5。
namespace ContrastRatio {

// 颜色按 0xAARRGGBB 存（和 ThemeColor 一致），alpha 不参与亮度计算。
constexpr uint8_t red(uint32_t color) { return static_cast<uint8_t>(color >> 16); }
constexpr uint8_t green(uint32_t color) { return static_cast<uint8_t>(color >> 8); }
constexpr uint8_t blue(uint32_t color) { return static_cast<uint8_t>(color); }

inline double channelLuminance(uint8_t value) {
    const double normalized = value / 255.0;
    return normalized <= 0.03928 ?
        normalized / 12.92 :
        std::pow((normalized + 0.055) / 1.055, 2.4);
}

inline double relativeLuminance(uint32_t color) {
    return 0.2126 * channelLuminance(red(color)) +
        0.7152 * channelLuminance(green(color)) +
        0.0722 * channelLuminance(blue(color));
}

inline double between(uint32_t foreground, uint32_t background) {
    const double a = relativeLuminance(foreground);
    const double b = relativeLuminance(background);
    const double lighter = a > b ? a : b;
    const double darker = a > b ? b : a;
    return (lighter + 0.05) / (darker + 0.05);
}

// 色相判定：说好是红的就得红得过另外两个通道，蓝的同理。
// 这一条防的是「改主题时顺手把它也调成跟背景一个色系」——那样红心就不是红心了。
constexpr bool isClearlyRed(uint32_t color) {
    return red(color) > green(color) + 60 && red(color) > blue(color) + 60;
}

constexpr bool isClearlyBlue(uint32_t color) {
    return blue(color) > red(color) + 60 && blue(color) > green(color) + 40;
}

} // namespace ContrastRatio
