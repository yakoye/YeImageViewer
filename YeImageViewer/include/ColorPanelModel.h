#pragma once
#include "CurrentColor.h"
#include <vector>

// 无窗口的编辑事务：连续拖拽/一次字段编辑只有一个撤销起点。
namespace ColorPanel {
using Rgb=ColorValue::Rgb;
class History {
    Rgb original{},value{},anchor{};
    std::vector<Rgb> previous,next;
    bool editing=false;
public:
    explicit History(Rgb color={}):original(color),value(color),anchor(color){}
    Rgb current()const{return value;}
    Rgb source()const{return original;}
    bool canUndo()const{return !previous.empty()||(editing&&anchor!=value);}
    bool canRedo()const{return !next.empty();}
    void reset(Rgb color){original=value=anchor=color;previous.clear();next.clear();editing=false;}
    void begin(){if(!editing){anchor=value;editing=true;}}
    void preview(Rgb color){begin();value=color;}
    void finish(){if(editing&&anchor!=value){previous.push_back(anchor);if(previous.size()>128)previous.erase(previous.begin());next.clear();}editing=false;}
    void change(Rgb color){finish();begin();preview(color);finish();}
    void restore(){change(original);}
    void undo(){finish();if(previous.empty())return;next.push_back(value);value=previous.back();previous.pop_back();}
    void redo(){finish();if(next.empty())return;previous.push_back(value);value=next.back();next.pop_back();}
};
inline constexpr std::array<double,8> HUE_OFFSETS{180,-30,30,-120,120,90,180,270};
inline Rgb suggested(Rgb color,int index){const auto hsv=PickerCore::hsv(color);return CurrentColor::fromHsv(hsv.h+HUE_OFFSETS.at(index),hsv.s,hsv.v);}
struct Layout {
    bool narrow=false;
    int width=810,height=600,top=12,body=576,scroll=0;
    PickerCore::Rect left,card,disk,saturation,value,original,current,recent,status;
    std::array<PickerCore::Rect,7> fields,copies;
    std::array<PickerCore::Rect,8> suggestions;
};
inline constexpr int WINDOW_WIDTH=680,WINDOW_HEIGHT=430,NAV_WIDTH=96;
// 用户要求固定小窗；坐标均为DIP，DPI/工作区缩小由窗口统一变换，不能单独裁切字段。
inline Layout compactLayout(){
    Layout l;l.width=WINDOW_WIDTH;l.height=WINDOW_HEIGHT;l.top=10;l.body=410;
    l.left={104,10,224,410};l.card={338,10,332,410};
    l.disk={116,65,200,200};l.saturation={116,294,200,16};l.value={116,337,200,16};
    l.original={116,383,94,26};l.current={218,383,98,26};
    for(int i=0;i<7;++i){l.fields[i]={416,116+i*28,180,24};l.copies[i]={604,116+i*28,44,24};}
    // 12DIP字体下为完整四个100%保留180DIP，Lab负值同样不挤掉复制按钮。
    l.suggestions[0]={402,28,19,19};l.suggestions[1]={402,65,19,19};l.suggestions[2]={426,65,19,19};
    l.suggestions[3]={567,28,19,19};l.suggestions[4]={591,28,19,19};
    for(int i=0;i<3;++i)l.suggestions[5+i]={567+i*24,65,19,19};
    l.recent={350,324,308,58};l.status={350,308,308,16};return l;
}
inline PickerCore::Rect recentSwatch(const Layout& l,int index){return {l.recent.x+(index%14)*22,l.recent.y+(index/14)*20,18,16};}
inline Layout layout(int width,int height,int scroll=0){
    Layout l;l.width=std::max(320,width);l.height=std::max(200,height);l.narrow=l.width<800;
    l.top=l.narrow?52:12;
    l.body=std::max(576,l.height-l.top-12);l.scroll=std::clamp(scroll,0,std::max(0,l.body+l.top+12-l.height));
    const int y=l.top-l.scroll;
    const int cardWidth=l.narrow?l.width-32:360;
    l.card={l.width-cardWidth-16,y,cardWidth,l.body};
    l.left={16,y,l.narrow?l.width-32:l.card.x-32,l.body};
    const int diameter=std::max(120,std::min(l.left.width-48,l.body-(l.narrow?308:280)));
    l.disk={l.left.x+(l.left.width-diameter)/2,y+84,diameter,diameter};
    l.saturation={l.disk.x,l.disk.y+diameter+35,diameter,22};
    l.value={l.disk.x,l.saturation.y+44,diameter,22};
    l.original={l.left.x+24,y+l.body-(l.narrow?98:70),(l.left.width-60)/2,38};
    l.current={l.original.x+l.original.width+12,l.original.y,l.original.width,38};
    const int x=l.card.x+20,copyX=l.card.x+l.card.width-50;
    for(int row=0;row<7;++row){l.fields[row]={x+84,y+159+row*46,copyX-x-94,32};l.copies[row]={copyX,y+159+row*46,30,32};}
    // 互补1、类似2、对比2、中差3；每组留出中文/英文标签空间。
    const int half=(l.card.width-40)/2;
    const int label=cardWidth<360?48:64,size=cardWidth<360?18:26,gap=size+6;
    l.suggestions[0]={x+label,y+27,size,size};
    for(int i=0;i<2;++i){l.suggestions[1+i]={x+label+i*gap,y+76,size,size};l.suggestions[3+i]={x+half+label+i*gap,y+27,size,size};}
    for(int i=0;i<3;++i)l.suggestions[5+i]={x+half+label+i*gap,y+76,size,size};
    return l;
}
inline std::optional<PickerCore::Cylindrical> diskPoint(const Layout& l,int x,int y,double value){
    const double radius=l.disk.width/2.0,dx=x-(l.disk.x+radius),dy=y-(l.disk.y+radius),distance=std::hypot(dx,dy);
    if(distance>radius)return {};
    double hue=std::fmod(std::atan2(dy,dx)*180/std::numbers::pi+450,360.);
    return PickerCore::Cylindrical{hue,std::clamp(distance/radius,0.0,1.0),value};
}
}
