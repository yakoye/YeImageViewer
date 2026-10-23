#pragma once
#include "ColorCatalogData.h"
#include "ColorLibrary.h"
#include "CurrentColor.h"
#include <opencv2/imgproc.hpp>
#include <wincodec.h>
#include <wrl/client.h>
#include <sstream>
#include <mutex>
#pragma comment(lib,"windowscodecs.lib")

namespace ColorCatalog {
using Rgb=ColorValue::Rgb;
struct Entry {std::wstring group,name,keywords;Rgb color;bool traditional=false;};
inline const std::vector<Entry>& entries(){
    static const auto all=[] {std::vector<Entry> result;std::istringstream file(COLOR_CATALOG_TSV);std::string line;
        while(std::getline(file,line)){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::array<std::string,5> part;for(auto& p:part)std::getline(row,p,'\t');
            const auto color=ColorValue::parse(ColorLibrary::wide(part[3]));if(!color)throw std::runtime_error("catalog");
            result.push_back({ColorLibrary::wide(part[1]),ColorLibrary::wide(part[2]),ColorLibrary::wide(part[4]),*color,part[0]=="traditional"});}return result;}();return all;
}
inline bool contains(std::wstring haystack,std::wstring query){for(auto& c:haystack)c=towlower(c);for(auto& c:query)c=towlower(c);return haystack.find(query)!=std::wstring::npos;}
inline std::vector<Entry> filter(bool traditional,std::wstring group,std::wstring query){std::vector<Entry> result;for(const auto& e:entries())if(e.traditional==traditional&&(group.empty()||e.group==group)&&contains(e.name+L" "+e.group+L" "+e.keywords+L" "+ColorValue::hex(e.color),query))result.push_back(e);return result;}
struct Gradient {
    std::vector<Rgb> colors{{83,202,164},{0,116,217}};int angle=90;bool radial=false;
    Rgb at(double value)const {if(colors.size()<2||colors.size()>4)throw std::runtime_error("stops");value=std::clamp(value,0.,1.)*(colors.size()-1);const size_t index=(std::min)(size_t(value),colors.size()-2);const double t=value-index;
        const auto mix=[&](int a,int b){return uint8_t(std::lround(a+(b-a)*t));};return {mix(colors[index].r,colors[index+1].r),mix(colors[index].g,colors[index+1].g),mix(colors[index].b,colors[index+1].b)};}
    std::wstring css()const {std::wstring out=radial?L"radial-gradient(circle, ":L"linear-gradient("+std::to_wstring(angle)+L"deg, ";for(size_t i=0;i<colors.size();++i){if(i)out+=L", ";out+=ColorValue::hex(colors[i])+L" "+PickerCore::decimal2(100.*i/(colors.size()-1))+L"%";}return out+L")";}
    double position(double x,double y,int width,int height)const {if(radial)return std::hypot(x-width/2.,y-height/2.)/std::hypot(width/2.,height/2.);const auto a=angle*std::numbers::pi/180.;const double dx=sin(a),dy=-cos(a),span=std::abs(dx)*width+std::abs(dy)*height;return .5+((x-width/2.)*dx+(y-height/2.)*dy)/(std::max)(1.,span);}
};
inline const std::vector<Gradient>& gradients(){static const auto list=[] {std::vector<Gradient> result;std::istringstream input(COLOR_GRADIENT_TSV);std::string line;while(std::getline(input,line)){if(line.empty())continue;Gradient g;g.colors.clear();std::istringstream row(line);std::string value;while(std::getline(row,value,',')){const auto c=ColorValue::parse(ColorLibrary::wide(value));if(!c)throw std::runtime_error("gradient data");g.colors.push_back(*c);}if(g.colors.size()<2||g.colors.size()>4)throw std::runtime_error("gradient stops");result.push_back(g);}return result;}();return list;}
struct Dominant {Rgb color;double fraction=0;};
// 有界确定性加权median-cut；透明像素不算背景色，权重和占比均按不透明度。
inline std::vector<Dominant> extract(const cv::Mat& input,int count){
    if(input.empty()||input.depth()!=CV_8U||(input.channels()!=3&&input.channels()!=4)||count<2||count>12)throw std::runtime_error("image");
    cv::Mat image;const double scale=(std::min)(1.,256./(std::max)(input.cols,input.rows));cv::resize(input,image,{(std::max)(1,int(input.cols*scale)),(std::max)(1,int(input.rows*scale))},0,0,cv::INTER_AREA);
    struct Bin {double r=0,g=0,b=0,w=0;};std::vector<Bin> bins(32768);
    for(int y=0;y<image.rows;++y){const auto* p=image.ptr<uint8_t>(y);for(int x=0;x<image.cols;++x,p+=image.channels()){const double weight=image.channels()==4?p[3]/255.:1.;if(weight<16./255)continue;auto& b=bins[(p[2]>>3)*1024+(p[1]>>3)*32+(p[0]>>3)];b.r+=p[2]*weight;b.g+=p[1]*weight;b.b+=p[0]*weight;b.w+=weight;}}
    std::vector<Bin> values;double total=0;for(const auto& b:bins)if(b.w){values.push_back(b);total+=b.w;}if(values.empty())return {};
    std::vector<std::vector<Bin>> boxes{values};
    while(boxes.size()<size_t(count)){size_t best=boxes.size();double score=-1;int axis=0;
        for(size_t i=0;i<boxes.size();++i){if(boxes[i].size()<2)continue;double lo[3]{255,255,255},hi[3]{},weight=0;for(const auto& b:boxes[i]){const double c[]{b.r/b.w,b.g/b.w,b.b/b.w};for(int a=0;a<3;++a){lo[a]=(std::min)(lo[a],c[a]);hi[a]=(std::max)(hi[a],c[a]);}weight+=b.w;}for(int a=0;a<3;++a)if((hi[a]-lo[a])*weight>score){score=(hi[a]-lo[a])*weight;best=i;axis=a;}}
        if(best==boxes.size())break;auto& box=boxes[best];const auto channel=[axis](const Bin& b){return (axis==0?b.r:axis==1?b.g:b.b)/b.w;};std::stable_sort(box.begin(),box.end(),[&](const Bin& a,const Bin& b){return channel(a)<channel(b);});double sum=0;for(const auto& b:box)sum+=b.w;double partial=0;size_t split=1;for(;split<box.size()-1;++split){partial+=box[split-1].w;if(partial>=sum/2)break;}std::vector<Bin> next(box.begin()+split,box.end());box.erase(box.begin()+split,box.end());boxes.push_back(std::move(next));}
    std::vector<Dominant> result;for(const auto& box:boxes){Bin sum;for(const auto& b:box){sum.r+=b.r;sum.g+=b.g;sum.b+=b.b;sum.w+=b.w;}result.push_back({{uint8_t(std::lround(sum.r/sum.w)),uint8_t(std::lround(sum.g/sum.w)),uint8_t(std::lround(sum.b/sum.w))},sum.w/total});}
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.fraction>b.fraction;});return result;
}
inline cv::Mat readImage(const std::filesystem::path& file){
    using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICBitmapScaler> scaler;ComPtr<IWICFormatConverter> converter;
    const auto checked=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("image decode");};
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));checked(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));checked(decoder->GetFrame(0,&frame));
    UINT width=0,height=0;checked(frame->GetSize(&width,&height));if(!width||!height||uint64_t(width)*height>200000000)throw std::runtime_error("image budget");const double ratio=(std::min)(1.,512./(std::max)(width,height));
    checked(factory->CreateBitmapScaler(&scaler));checked(scaler->Initialize(frame.Get(),(std::max)(1U,UINT(width*ratio)),(std::max)(1U,UINT(height*ratio)),WICBitmapInterpolationModeFant));checked(factory->CreateFormatConverter(&converter));checked(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    UINT w=0,h=0;checked(converter->GetSize(&w,&h));cv::Mat pixels(h,w,CV_8UC4);checked(converter->CopyPixels(nullptr,UINT(pixels.step),UINT(pixels.total()*4),pixels.data));return pixels;
}
inline std::mutex currentMutex;inline cv::Mat currentImage;
inline void setCurrent(const cv::Mat& image){cv::Mat small;if(!image.empty()&&image.depth()==CV_8U&&(image.channels()==3||image.channels()==4)){const double factor=(std::min)(1.,512./(std::max)(image.cols,image.rows));cv::resize(image,small,{(std::max)(1,int(image.cols*factor)),(std::max)(1,int(image.rows*factor))},0,0,cv::INTER_AREA);}std::lock_guard lock(currentMutex);currentImage=small;}
inline cv::Mat getCurrent(){std::lock_guard lock(currentMutex);return currentImage.clone();}
}
