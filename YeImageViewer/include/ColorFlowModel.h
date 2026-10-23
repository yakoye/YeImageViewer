#pragma once
#include "PickerCore.h"
#include <vector>
#include <string>
#include <algorithm>

namespace ColorFlow {
inline constexpr int SWATCH_HEIGHT=48, LIST_HEIGHT=72, GAP=8;
struct Item {
    std::wstring key,group,title,detail,copy;
    ColorValue::Rgb color{};
    std::vector<ColorValue::Rgb> stops;
    bool favorite=false;
    int angle=90;bool radial=false;
    int destination=0;
};
struct Element {PickerCore::Rect rect;int item=-1;std::wstring heading;};
class Layout {
    std::vector<Element> elements_;
    int height_=0,columns_=1;
public:
    void rebuild(const std::vector<Item>& items,int width,bool list=false) {
        elements_.clear();height_=0;
        const int usable=std::max(1,width-14),gap=8,minimum=100;
        columns_=list?1:std::max(1,(usable+gap)/(minimum+gap));
        const int cell=std::max(1,(usable-gap*(columns_-1))/columns_);
        const int rowHeight=list?LIST_HEIGHT:SWATCH_HEIGHT,step=rowHeight+GAP;
        int y=6,column=0;std::wstring group;bool first=true;
        for(size_t i=0;i<items.size();++i) {
            const auto& value=items[i];
            if(first||value.group!=group) {
                if(column){y+=step;column=0;}
                if(!value.group.empty()){elements_.push_back({{0,y,usable,26},-1,value.group});y+=30;}
                group=value.group;first=false;
            }
            elements_.push_back({{column*(cell+gap),y,cell,rowHeight},int(i),{}});
            if(++column==columns_){column=0;y+=step;}
        }
        if(column)y+=step;
        height_=items.empty()?0:y;
    }
    const auto& elements()const{return elements_;}
    int height()const{return height_;}
    int columns()const{return columns_;}
    int maximum(int viewport)const{return std::max(0,height_-std::max(0,viewport));}
    size_t firstVisible(int scroll)const {
        return size_t(std::lower_bound(elements_.begin(),elements_.end(),scroll,
            [](const Element& e,int y){return e.rect.y+e.rect.height<=y;})-elements_.begin());
    }
    int at(int x,int y,int scroll)const {
        y+=scroll;
        for(size_t i=firstVisible(y);i<elements_.size()&&elements_[i].rect.y<=y;++i)
            if(elements_[i].item>=0&&elements_[i].rect.contains(x,y))return elements_[i].item;
        return -1;
    }
    int topOf(int item)const {for(const auto& e:elements_)if(e.item==item)return e.rect.y;return -1;}
    int clamp(int scroll,int viewport)const{return std::clamp(scroll,0,maximum(viewport));}
};
}
