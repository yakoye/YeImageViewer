#pragma once
#include "PsdLayers.h"
#include <functional>
#include <unordered_map>

// 临时浏览合成。保持文件坐标和原始可见性，不回写文档，不替换默认合成图解码器。
namespace PsdRecompose {
enum class Issue { None, Format, Bounds, Structure, Blend, Mask, Effects, Adjustment, Clipping, Decode, Cancelled };
struct Result {
    PsdCompositeDecoder::Image image;
    Issue issue = Issue::None;
    int layer = -1;
    explicit operator bool() const { return issue == Issue::None && !image.bgra.empty(); }
};
inline bool supportsBlend(std::string_view mode) {
    for (auto key : {"norm", "mul ", "scrn", "over", "dark", "lite", "diff", "smud", "hLit", "sLit"})
        if (mode == key) return true;
    return false;
}
inline double blend(double b, double s, std::string_view mode) {
    if (mode == "mul ") return b * s;
    if (mode == "scrn") return b + s - b * s;
    if (mode == "over") return b <= 0.5 ? 2*b*s : 1-2*(1-b)*(1-s);
    if (mode == "dark") return (std::min)(b,s);
    if (mode == "lite") return (std::max)(b,s);
    if (mode == "diff") return std::abs(b-s);
    if (mode == "smud") return b+s-2*b*s;
    if (mode == "hLit") return s <= 0.5 ? 2*b*s : 1-2*(1-b)*(1-s);
    if (mode == "sLit") {
        const double d = b <= 0.25 ? ((16*b-12)*b+4)*b : std::sqrt(b);
        return s <= 0.5 ? b-(1-2*s)*b*(1-b) : b+(2*s-1)*(d-b);
    }
    return s;
}
// 非预乘颜色、source-over；透明背景不能直接套不透明图的混合公式。
inline void pixel(uint8_t* dst, const uint8_t* src, double opacity, std::string_view mode) {
    const double as = src[3]/255.0 * opacity, ab = dst[3]/255.0;
    const double a = as + ab*(1-as);
    if (a <= 0) { std::fill_n(dst,4,0); return; }
    for (int c=0;c<3;++c) {
        const double b=dst[c]/255.0, s=src[c]/255.0;
        const double value=((1-as)*ab*b+as*((1-ab)*s+ab*blend(b,s,mode)))/a;
        dst[c]=static_cast<uint8_t>(std::lround(std::clamp(value,0.0,1.0)*255));
    }
    dst[3]=static_cast<uint8_t>(std::lround(a*255));
}
using Cache = std::unordered_map<size_t,PsdCompositeDecoder::Image>;
inline Result render(const PsdLayers::Document& doc, std::span<const uint8_t> visible,
    Cache& cache, const std::function<bool()>& cancelled = {}) noexcept {
    try {
        if (!doc.valid || doc.mode != 3 || doc.depth != 8) return {{},Issue::Format};
        const uint64_t pixels=uint64_t(doc.width)*doc.height;
        if (!pixels || pixels>16ULL*1024*1024 || visible.size()!=doc.layers.size()) return {{},Issue::Bounds};
        // 读取器已经把PSD物理顺序规范化为界面自上到下；目录结束标记不参与绘制。
        std::vector<std::vector<int>> children(doc.layers.size()+1);
        std::vector<int> stack{static_cast<int>(doc.layers.size())};
        for (int i=0;i<static_cast<int>(doc.layers.size());++i) {
            const auto& layer=doc.layers[i];
            if(layer.sectionType>3)return {{},Issue::Structure,i};
            if (layer.sectionType==3) {
                if (stack.size()==1) return {{},Issue::Structure,i};
                stack.pop_back(); continue;
            }
            children[stack.back()].push_back(i);
            if (layer.sectionType==1 || layer.sectionType==2) {
                if (stack.size()>=8) return {{},Issue::Bounds,i};
                stack.push_back(i);
            }
        }
        if (stack.size()!=1) return {{},Issue::Structure};
        Result result;
        result.image.width=doc.width; result.image.height=doc.height;
        result.image.hasAlpha=true; result.image.bgra.resize(static_cast<size_t>(pixels)*4);
        size_t cacheBytes=0;
        for (const auto& [index,image]:cache) cacheBytes+=image.bgra.size();
        const auto fail=[&](Issue issue,int layer){result.issue=issue;result.layer=layer;return false;};
        std::function<bool(int,std::vector<uint8_t>&)> draw;
        draw=[&](int parent,std::vector<uint8_t>& destination) {
            for (auto it=children[parent].rbegin();it!=children[parent].rend();++it) {
                const int index=*it; const auto& layer=doc.layers[index];
                if (!visible[index]) continue;
                if (cancelled && cancelled()) return fail(Issue::Cancelled,index);
                if (layer.effects || layer.vectorMask || layer.customBlendRanges || layer.fillOpacity!=255)
                    return fail(Issue::Effects,index);
                if (layer.adjustment) return fail(Issue::Adjustment,index);
                if (layer.clipped) return fail(Issue::Clipping,index);
                for(const auto& channel:layer.channels) {
                    if(channel.id < -2 || (channel.id==-2&&!layer.masked))return fail(Issue::Mask,index);
                    if(channel.id>=3)return fail(Issue::Format,index);
                }
                if (layer.group) {
                    if (layer.masked) return fail(Issue::Mask,index);
                    if (layer.blend=="pass" && layer.opacity==255) {
                        if (!draw(index,destination)) return false;
                    } else {
                        if (!supportsBlend(layer.blend)) return fail(Issue::Blend,index);
                        std::vector<uint8_t> isolated(destination.size());
                        if (!draw(index,isolated)) return false;
                        for (size_t p=0;p<pixels;++p)
                            pixel(destination.data()+p*4,isolated.data()+p*4,layer.opacity/255.0,layer.blend);
                    }
                    continue;
                }
                if (!supportsBlend(layer.blend)) return fail(Issue::Blend,index);
                auto found=cache.find(index);
                if (found==cache.end()) {
                    auto image=PsdLayers::preview(doc,index,256ULL<<20);
                    if (image.bgra.empty()) return fail(Issue::Decode,index);
                    if (cacheBytes+image.bgra.size()>128ULL<<20) {cache.clear();cacheBytes=0;}
                    cacheBytes+=image.bgra.size(); found=cache.emplace(index,std::move(image)).first;
                }
                const auto& image=found->second;
                if (result.image.icc.empty()) result.image.icc=image.icc;
                int32_t mt=0,ml=0,mb=0,mr=0; uint8_t outside=255,maskFlags=2;
                std::vector<uint8_t> mask;
                if (layer.masked) {
                    if(layer.maskData.size()!=18 && layer.maskData.size()!=20)return fail(Issue::Mask,index);
                    PsdLayers::Reader data(layer.maskData);
                    if (data.remaining()<18) return fail(Issue::Mask,index);
                    mt=PsdLayers::signed32(data); ml=PsdLayers::signed32(data);
                    mb=PsdLayers::signed32(data); mr=PsdLayers::signed32(data);
                    outside=static_cast<uint8_t>(data.integer(1)); maskFlags=static_cast<uint8_t>(data.integer(1));
                    // 相对坐标、羽化/密度参数与实际用户蒙版暂不猜测。
                    if ((maskFlags & ~6u)!=0 || mb<mt || mr<ml) return fail(Issue::Mask,index);
                    if (!(maskFlags&2)) {
                        const auto channel=std::find_if(layer.channels.begin(),layer.channels.end(),[](const auto& c){return c.id==-2;});
                        if (channel==layer.channels.end() || uint64_t(int64_t(mb)-mt)*(int64_t(mr)-ml)>64ULL*1024*1024)
                            return fail(Issue::Mask,index);
                        if (mr>ml && mb>mt) mask=PsdLayers::channelPixels(*channel,mr-ml,mb-mt,8,doc.psb,64ULL<<20);
                    }
                }
                const int x0=(std::max)(0,layer.left), y0=(std::max)(0,layer.top);
                const int x1=static_cast<int>((std::min)(int64_t(doc.width),int64_t(layer.right)));
                const int y1=static_cast<int>((std::min)(int64_t(doc.height),int64_t(layer.bottom)));
                for (int y=y0;y<y1;++y) {
                    if (cancelled && cancelled()) return fail(Issue::Cancelled,index);
                    for (int x=x0;x<x1;++x) {
                        auto src=image.bgra.data()+((int64_t(y)-layer.top)*image.width+(int64_t(x)-layer.left))*4;
                        double opacity=layer.opacity/255.0;
                        if (!(maskFlags&2)) {
                            int value=(x>=ml && x<mr && y>=mt && y<mb && !mask.empty()) ? mask[(int64_t(y)-mt)*(int64_t(mr)-ml)+x-ml] : outside;
                            if (maskFlags&4) value=255-value;
                            opacity*=value/255.0;
                        }
                        pixel(destination.data()+(size_t(y)*doc.width+x)*4,src,opacity,layer.blend);
                    }
                }
            }
            return true;
        };
        if (!draw(static_cast<int>(doc.layers.size()),result.image.bgra)) result.image={};
        return result;
    } catch (...) { return {{},Issue::Decode}; }
}
} // namespace PsdRecompose
