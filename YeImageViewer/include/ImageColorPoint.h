#pragma once
#include "ImageViewTransform.h"
#include <optional>
#include <cmath>

namespace ImageColorPoint {
inline std::optional<ImageViewTransform::Point<int>> locate(int x,int y,int viewportWidth,int viewportHeight,
    int viewWidth,int viewHeight,int sourceWidth,int sourceHeight,double zoom,int slideX,int slideY,
    int rotation,bool horizontal,bool vertical) {
    if(x<0||y<0||x>=viewportWidth||y>=viewportHeight||viewWidth<=0||viewHeight<=0||sourceWidth<=0||sourceHeight<=0||!std::isfinite(zoom)||zoom<=0)return {};
    const bool rotated=rotation&1;
    const int dw=rotated?viewHeight:viewWidth,dh=rotated?viewWidth:viewHeight;
    const int left=slideX+int(std::round((viewportWidth-dw*zoom)/2));
    const int top=slideY+int(std::round((viewportHeight-dh*zoom)/2));
    const double px=(x-left)/zoom,py=(y-top)/zoom;
    if(px<0||py<0||px>=dw||py>=dh)return {};
    const auto point=ImageViewTransform::displayToSource(int(px),int(py),dw,dh,rotation,horizontal,vertical);
    return ImageViewTransform::Point<int>{int(int64_t(point.x)*sourceWidth/viewWidth),int(int64_t(point.y)*sourceHeight/viewHeight)};
}
}
