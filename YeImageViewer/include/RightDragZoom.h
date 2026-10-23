#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

// 仅手势状态和倍率数学，不读取系统输入；松手/取消都不能漏出一次右键退出。
namespace RightDragZoom {
class Gesture {
    bool held_=false,dragged_=false,cancelled_=false;
    int x_=0,y_=0;int64_t zoom_=1;
public:
    int x()const{return x_;}int y()const{return y_;}int64_t initialZoom()const{return zoom_;}
    bool held()const{return held_;}
    void begin(int x,int y,int64_t zoom){held_=true;dragged_=cancelled_=false;x_=x;y_=y;zoom_=std::max<int64_t>(1,zoom);}
    std::optional<int64_t> move(int x,int y,int dpi,uint32_t sensitivity,bool reversed){
        if(!held_)return {};
        const double dx=double(x-x_)*96/std::max(1,dpi),dy=double(y-y_)*96/std::max(1,dpi);
        if(!dragged_&&std::max(std::abs(dx),std::abs(dy))<4)return {};
        dragged_=true;
        const double exponent=std::clamp((reversed?dy:-dy)*(.0025+.0006*std::min(19u,sensitivity)),-20.,20.);
        return int64_t(std::llround(std::clamp(zoom_*std::exp(exponent),655.36,6553600.)));
    }
    bool release(){const bool consumed=dragged_||cancelled_;held_=dragged_=cancelled_=false;return consumed;}
    void cancel(){if(held_){held_=false;cancelled_=true;}}
};
}
