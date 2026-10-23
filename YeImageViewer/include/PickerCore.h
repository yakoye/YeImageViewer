#pragma once
#include "ColorValue.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// 无窗口、无显示器、无共享可变状态。HUD 与后续工作台使用同一格式化器。
namespace PickerCore {
// 保留旧偏好的前四个索引，新增格式只追加。
enum class Format { Hex, Rgb, Hsl, Hsv, Cmyk, Hsi, Lab };
inline constexpr int FORMAT_COUNT = 7;
inline constexpr int HUD_WIDTH = 180;
inline constexpr int HUD_LINE = 17;
inline constexpr int HUD_PADDING = 8;
inline constexpr int HUD_PRIMARY_FONT = 13, HUD_HELP_FONT = 11;
inline constexpr int HUD_PRIMARY_WEIGHT = 600, HUD_PRIMARY_LINE = 21;
inline constexpr int HUD_TEXT_ROWS = 11; // 坐标、当前颜色、九条操作说明
constexpr bool hudRowCentered(size_t row){return row<2;}
inline std::wstring hudHelpText(std::wstring_view text){return L"    "+std::wstring(text);}
constexpr int hudHelpGap(int dpi,bool followingRows){return followingRows?int((int64_t(HUD_LINE)*std::max(1,dpi)+96)/192):0;}
constexpr int hudBaseline(int height,int ascent,int descent){return std::max(0,(height-ascent-descent)/2)+ascent;}
constexpr int nextFormat(int value) { return value >= 0 && value < FORMAT_COUNT ? (value + 1) % FORMAT_COUNT : 0; }
struct Cylindrical { double h = 0, s = 0, v = 0; };
inline Cylindrical hsv(ColorValue::Rgb c) {
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    double h = 0;
    if (d != 0) {
        h = hi == r ? 60 * std::fmod((g - b) / d, 6) : hi == g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4);
        if (h < 0) h += 360;
    }
    return {h, hi == 0 ? 0 : d / hi, hi};
}
inline Cylindrical hsl(ColorValue::Rgb c) {
    auto result = hsv(c);
    const double hi = std::max({c.r, c.g, c.b}) / 255.0, lo = std::min({c.r, c.g, c.b}) / 255.0;
    const double lightness = (hi + lo) / 2, delta = hi - lo;
    return {result.h, delta == 0 ? 0 : delta / (1 - std::abs(2 * lightness - 1)), lightness};
}
inline Cylindrical hsi(ColorValue::Rgb c) {
    const double r=c.r/255.0,g=c.g/255.0,b=c.b/255.0,sum=r+g+b;
    const double saturation=sum==0?0:1-3*std::min({r,g,b})/sum;
    double hue=saturation<=1e-14?0:std::atan2(std::sqrt(3.0)*(g-b),2*r-g-b)*180/std::acos(-1.0);
    if(hue<0)hue+=360;
    return {hue,saturation,sum/3};
}
struct Lab { double l=0,a=0,b=0; };
// sRGB逆传递函数 → XYZ → CIE Lab，参考白D65(0.95047,1,1.08883)。
// 不是CSS的D50 lab()，也不是通过显示器/印刷ICC测得的设备值。
inline Lab lab(ColorValue::Rgb c) {
    const auto linear=[](uint8_t byte){const double v=byte/255.0;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
    const double r=linear(c.r),g=linear(c.g),b=linear(c.b);
    const auto f=[](double v){return v>216.0/24389?std::cbrt(v):(24389.0/27*v+16)/116;};
    const double x=f((.4124564*r+.3575761*g+.1804375*b)/.95047);
    const double y=f(.2126729*r+.7151522*g+.0721750*b);
    const double z=f((.0193339*r+.1191920*g+.9503041*b)/1.08883);
    return {116*y-16,500*(x-y),200*(y-z)};
}
// 固定小数点、去尾零，负Lab分量和舍入为零的数不丢符号/不输出-0。
inline std::wstring decimal(double value,int places=2) {
    int scale=1;for(int i=0;i<places;++i)scale*=10;
    const int64_t scaled=std::llround(value*scale),magnitude=scaled<0?-scaled:scaled;
    std::wstring text=scaled<0?L"-":L"";
    text+=std::to_wstring(magnitude/scale);
    if(const int remainder=static_cast<int>(magnitude%scale)) {
        auto fraction=std::to_wstring(remainder+scale).substr(1);
        while(fraction.back()==L'0')fraction.pop_back();
        text+=L'.';text+=fraction;
    }
    return text;
}
inline std::wstring decimal2(double value) { return decimal(value); }
inline std::wstring format(ColorValue::Rgb c, Format f) {
    if (f == Format::Hex) return ColorValue::hex(c);
    if (f == Format::Rgb) return L"rgb(" + std::to_wstring(c.r) + L", " + std::to_wstring(c.g) + L", " + std::to_wstring(c.b) + L")";
    if(f==Format::Cmyk) {
        const double hi=std::max({c.r,c.g,c.b})/255.0;
        const auto ink=[&](uint8_t v){return decimal(hi==0?0:100*(1-v/255.0/hi),0)+L"%";};
        return L"cmyk("+ink(c.r)+L", "+ink(c.g)+L", "+ink(c.b)+L", "+decimal(100*(1-hi),0)+L"%)";
    }
    if(f==Format::Hsi) {
        const auto v=hsi(c);
        return L"hsi("+decimal2(std::fmod(std::round(v.h*100),36000)/100)+L", "+decimal2(v.s*100)+L"%, "+decimal2(v.v*100)+L"%)";
    }
    if(f==Format::Lab) {
        const auto v=lab(c);
        return L"Lab-D65("+decimal(v.l,3)+L", "+decimal(v.a,3)+L", "+decimal(v.b,3)+L")";
    }
    const auto v = f == Format::Hsl ? hsl(c) : hsv(c);
    return std::wstring(f == Format::Hsl ? L"hsl(" : L"hsv(") + decimal2(std::fmod(std::round(v.h*100),36000)/100) +
        L", " + decimal2(v.s*100) + L"%, " + decimal2(v.v*100) + L"%)";
}
inline std::wstring display(ColorValue::Rgb c,Format f) {
    static constexpr std::array<const wchar_t*,FORMAT_COUNT> names{L"HEX",L"RGB",L"HSL",L"HSV/HSB",L"CMYK",L"HSI",L"CIE-LAB"};
    const int index=static_cast<int>(f);
    if(index<0 || index>=FORMAT_COUNT)return {};
    const auto value=format(c,f);
    if(f==Format::Hex)return std::wstring(names[index])+L": "+value;
    const auto start=value.find(L'(');
    return std::wstring(names[index])+L": "+value.substr(start+1,value.size()-start-2);
}
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    constexpr bool contains(int px, int py) const { return ColorValue::screenPoint(px, py, x, y, width, height); }
    bool operator==(const Rect&) const = default;
};
// 只在真实监视器矩形内有效，不能把虚拟桌面包围框当作一块屏幕。
template<class Range> inline int monitorAt(const Range& monitors, int x, int y) {
    for (size_t i = 0; i < monitors.size(); ++i) if (monitors[i].contains(x, y)) return static_cast<int>(i);
    return -1;
}
inline Rect placeHud(Rect monitor, int x, int y, int width, int height, int gap, const Rect* previous = nullptr) {
    width = std::min(std::max(1, width), monitor.width);
    height = std::min(std::max(1, height), monitor.height);
    const int64_t right = int64_t(monitor.x) + monitor.width, bottom = int64_t(monitor.y) + monitor.height;
    int64_t hx = int64_t(x) + gap, hy = int64_t(y) + gap;
    // 一旦翻到左/上侧，在鼠标真正离开原区域前保持同侧，避免阈值处抖动。
    if (hx + width > right || (previous && previous->x + previous->width <= x && hx + width + gap > right)) hx = int64_t(x) - gap - width;
    if (hy + height > bottom || (previous && previous->y + previous->height <= y && hy + height + gap > bottom)) hy = int64_t(y) - gap - height;
    hx = std::clamp(hx, int64_t(monitor.x), right - width);
    hy = std::clamp(hy, int64_t(monitor.y), bottom - height);
    return {static_cast<int>(hx), static_cast<int>(hy), width, height};
}
inline constexpr std::array<int, 4> GRIDS{11, 15, 17, 21};
inline constexpr std::array<int, 6> ZOOMS{4, 6, 8, 10, 12, 16};
struct Settings {
    int format = 0, grid = 15, zoom = 8, relative = 0, showGrid = 1, crosshair = 1, showHints = 1;
    bool operator==(const Settings&) const = default;
    bool valid() const {
        return format >= 0 && format < FORMAT_COUNT && std::find(GRIDS.begin(), GRIDS.end(), grid) != GRIDS.end() &&
            std::find(ZOOMS.begin(), ZOOMS.end(), zoom) != ZOOMS.end() && (relative == 0 || relative == 1) &&
            (showGrid == 0 || showGrid == 1) && (crosshair == 0 || crosshair == 1) && (showHints == 0 || showHints == 1);
    }
};
}
