#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace TransformSavePrompt {
struct Rect {int x=0,y=0,width=0,height=0;bool contains(int px,int py)const{return px>=x&&py>=y&&px<x+width&&py<y+height;}};
struct Layout {Rect panel,label;std::array<Rect,3> buttons;};
inline Layout layout(int width,int height,int dpi,Rect image={}){
    const auto s=[&](int n){return std::max(1,int((int64_t(n)*std::max(1,dpi)+48)/96));};
    Layout out;if(width<3||height<3)return out;
    const int margin=std::min({s(12),width/20,height/20}),w=std::min(s(284),width-margin*2),h=std::min(s(64),height-margin*2);
    const auto overlap=[&](const Rect& a){return int64_t(std::max(0,std::min(a.x+a.width,image.x+image.width)-std::max(a.x,image.x)))*
        std::max(0,std::min(a.y+a.height,image.y+image.height)-std::max(a.y,image.y));};
    // 同等遮挡时优先右下角；有留白时优先不覆盖图片，绝不为提示改图像倍率。
    const std::array<Rect,4> places{{{width-margin-w,height-margin-h,w,h},{margin,height-margin-h,w,h},
        {width-margin-w,margin,w,h},{margin,margin,w,h}}};
    out.panel=places[0];for(const auto& place:places)if(overlap(place)<overlap(out.panel))out.panel=place;
    const int x=out.panel.x,y=out.panel.y,padding=std::min(s(8),(w-3)/8),gap=std::min(s(6),(w-padding*2-3)/2);
    const int available=w-padding*2-gap*2,first=std::max(1,available*100/256),second=std::max(1,available*110/256),third=available-first-second;
    const int row=std::min(s(30),h/2),bh=std::max(1,std::min(s(26),h-row));
    out.label={x+padding,y,w-padding*2,row};out.buttons={{{x+padding,y+row,first,bh},{x+padding+first+gap,y+row,second,bh},{x+padding+first+second+gap*2,y+row,third,bh}}};return out;
}
class Press {
    int button_=-2;
public:
    bool held()const{return button_!=-2;}
    int button()const{return button_;}
    bool begin(const Layout& area,int x,int y){cancel();if(!area.panel.contains(x,y))return false;button_=-1;for(int i=0;i<3;++i)if(area.buttons[i].contains(x,y))button_=i;return true;}
    int release(const Layout& area,int x,int y){const int button=button_;cancel();return button>=0&&area.buttons[button].contains(x,y)?button:-1;}
    void cancel(){button_=-2;}
};
}
