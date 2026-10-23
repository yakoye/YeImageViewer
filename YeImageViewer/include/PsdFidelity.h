#pragma once
#include <span>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace PsdFidelity {
struct Difference {bool valid=false;double mean=0;int maximum=0;bool differs()const{return !valid||maximum>2;}};
// 两端均已转换为sRGB的8位BGRA。与独立参考用例一致，比较8位Alpha和预乘RGB。
inline Difference compare(std::span<const uint8_t> original,std::span<const uint8_t> rendered){
    if(original.empty()||original.size()!=rendered.size()||original.size()%4)return {};
    Difference out;out.valid=true;double sum=0;
    for(size_t p=0;p<original.size();p+=4)for(int c=0;c<4;++c){
        const int a=c==3?original[p+3]:(int(original[p+c])*original[p+3]+127)/255;
        const int b=c==3?rendered[p+3]:(int(rendered[p+c])*rendered[p+3]+127)/255;
        const int delta=std::abs(a-b);sum+=delta;out.maximum=(std::max)(out.maximum,delta);
    }
    out.mean=sum/original.size();return out;
}
}
