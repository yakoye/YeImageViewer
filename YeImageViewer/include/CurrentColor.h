#pragma once
#include "PickerCore.h"
#include <atomic>
#include <numbers>
#include <sstream>
#include <locale>

// 仅进程内共享，不默认保存颜色/截图；取消取色不提交。
namespace CurrentColor {
inline std::atomic<uint64_t> packed{0xFF4080C0};
inline uint32_t pack(ColorValue::Rgb c) {return (uint32_t(c.r)<<16)|(uint32_t(c.g)<<8)|c.b;}
inline ColorValue::Rgb unpack(uint32_t c) {return {uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};}
inline ColorValue::Rgb get() {return unpack(packed.load());}
inline void set(ColorValue::Rgb c) {packed=(255ULL<<24)|pack(c);}
inline void setImage(ColorValue::Rgb c,uint8_t alpha) {packed=(1ULL<<32)|(uint64_t(alpha)<<24)|pack(c);}
inline bool fromImage() {return (packed.load()>>32)!=0;}
inline int alpha() {return int((packed.load()>>24)&255);}
inline ColorValue::Rgb normalized(double r,double g,double b) {
    const auto byte=[](double v){return uint8_t(std::lround(std::clamp(v,0.0,1.0)*255));};
    return {byte(r),byte(g),byte(b)};
}
inline ColorValue::Rgb fromHsv(double h,double s,double v) {
    h=std::fmod(h,360.0);if(h<0)h+=360;
    const double c=v*s,x=c*(1-std::abs(std::fmod(h/60,2.0)-1)),m=v-c;
    if(h<60)return normalized(c+m,x+m,m);
    if(h<120)return normalized(x+m,c+m,m);
    if(h<180)return normalized(m,c+m,x+m);
    if(h<240)return normalized(m,x+m,c+m);
    if(h<300)return normalized(x+m,m,c+m);
    return normalized(c+m,m,x+m);
}
// 面板字段使用逗号/空格分隔的数值，百分比按0..100输入；不依赖系统小数分隔符。
inline std::optional<ColorValue::Rgb> parse(std::wstring text,PickerCore::Format format) {
    if(format==PickerCore::Format::Hex)return ColorValue::parse(text);
    for(auto& c:text)if(c==L','||c==L'%')c=L' ';
    std::wistringstream stream(text);stream.imbue(std::locale::classic());
    double a=0,b=0,c=0,d=0;
    if(!(stream>>a>>b>>c)||!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c))return {};
    if(format==PickerCore::Format::Cmyk && (!(stream>>d)||!std::isfinite(d)))return {};
    stream>>std::ws;if(!stream.eof())return {};
    const auto in=[](double v,double max){return v>=0 && v<=max;};
    if(format==PickerCore::Format::Rgb) {
        if(!in(a,255)||!in(b,255)||!in(c,255))return {};
        return normalized(a/255,b/255,c/255);
    }
    if(format==PickerCore::Format::Lab) {
        if(!in(a,100)||b < -128||b>127||c < -128||c>127)return {};
        const double fy=(a+16)/116,fx=fy+b/500,fz=fy-c/200;
        const auto inverse=[](double v){const double cube=v*v*v;return cube>216.0/24389?cube:(116*v-16)/(24389.0/27);};
        const double x=.95047*inverse(fx),y=inverse(fy),z=1.08883*inverse(fz);
        const auto encode=[](double v){return v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;};
        return normalized(encode(3.2404542*x-1.5371385*y-.4985314*z),
            encode(-.9692660*x+1.8760108*y+.0415560*z),encode(.0556434*x-.2040259*y+1.0572252*z));
    }
    if(format==PickerCore::Format::Cmyk) {
        if(!in(a,100)||!in(b,100)||!in(c,100)||!in(d,100))return {};
        return normalized((1-a/100)*(1-d/100),(1-b/100)*(1-d/100),(1-c/100)*(1-d/100));
    }
    if(!in(a,360)||!in(b,100)||!in(c,100))return {};
    b/=100;c/=100;
    if(format==PickerCore::Format::Hsv)return fromHsv(a,b,c);
    if(format==PickerCore::Format::Hsl) {
        const double v=c+b*std::min(c,1-c);
        return fromHsv(a,v==0?0:2*(1-c/v),v);
    }
    if(format==PickerCore::Format::Hsi) {
        a=std::fmod(a,360.0);double r,g,bl;
        const auto sector=[&](double h){return c*(1+b*std::cos(h*std::numbers::pi/180)/std::cos((60-h)*std::numbers::pi/180));};
        if(a<120){bl=c*(1-b);r=sector(a);g=3*c-r-bl;}
        else if(a<240){a-=120;r=c*(1-b);g=sector(a);bl=3*c-r-g;}
        else{a-=240;g=c*(1-b);bl=sector(a);r=3*c-g-bl;}
        return normalized(r,g,bl);
    }
    return {};
}
inline std::wstring field(ColorValue::Rgb c,PickerCore::Format format) {
    const auto text=PickerCore::display(c,format);return text.substr(text.find(L": ")+2);
}
}
