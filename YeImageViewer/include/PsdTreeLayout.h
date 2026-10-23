#pragma once
#include "PsdLayers.h"
#include "PickerCore.h"
#include <set>

namespace PsdTree {
inline constexpr int ROW_HEIGHT=34, THUMBNAIL=24, LIST_TOP=116, FOOTER=76;
struct Row {int index=0,depth=0;bool effective=true;};
inline std::vector<Row> rows(const PsdLayers::Document& document,const std::set<int>& collapsed,std::span<const uint8_t> visibility){
    struct Parent{bool collapsed=false,visible=true;};std::vector<Parent> parents{{}};std::vector<Row> result;
    for(int i=0;i<int(document.layers.size());++i){const auto& layer=document.layers[i];
        if(layer.sectionType==3){if(parents.size()>1)parents.pop_back();continue;}
        const bool own=i<int(visibility.size())?bool(visibility[i]):layer.visible;
        if(!parents.back().collapsed)result.push_back({i,int(parents.size())-1,parents.back().visible&&own});
        if(layer.group)parents.push_back({parents.back().collapsed||collapsed.contains(i),parents.back().visible&&own});
    }
    return result;
}
struct Geometry{PickerCore::Rect eye,disclosure,thumbnail,name,type,mask;};
inline Geometry geometry(int width,int y,int depth,bool group,bool masked=false){
    const int indent=std::min(depth,std::max(0,(width-(masked?196:160))/16))*16;
    const int base=36+indent,thumb=base+(group?18:0),name=thumb+30;
    const int text=name+(masked?30:0);
    return {{3,y,28,ROW_HEIGHT},{base,y,18,ROW_HEIGHT},{thumb,y+5,THUMBNAIL,THUMBNAIL},{text,y,std::max(1,width-text-34),ROW_HEIGHT},{width-30,y,26,ROW_HEIGHT},{name,y+5,masked?THUMBNAIL:0,THUMBNAIL}};
}
}
