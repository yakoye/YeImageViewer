#pragma once
#include "PsdLayers.h"
#include <windows.h>
#include <memory>
#include <array>
#include <functional>

// PhotoCraft 只读 C ABI v1。惰性加载同目录 DLL，不参与普通图片首帧。
// 每个文档只在 PsdDock 串行后台任务中访问；不写原文件、不改变默认合成图路径。
namespace PhotocraftBackend {
using Cancel=uint32_t(__cdecl*)(void*);
inline uint32_t __cdecl checkCancel(void* context)noexcept {try{return context&&(*static_cast<const std::function<bool()>*>(context))()?1:0;}catch(...){return 1;}}
struct Info {uint32_t width,height,depth,mode,layers,channels,warningBytes,reserved;};
struct Row {uint64_t id;uint32_t section,flags;float opacity,fill;std::array<char,4> blend;uint32_t nameBytes;};
struct State {uint32_t visible,mask;float opacity,fill;std::array<char,4> blend;};
struct Pixels {uint8_t* data;size_t length;uint32_t width,height;};
static_assert(sizeof(Info)==32&&sizeof(Row)==32&&sizeof(State)==20&&sizeof(Pixels)==24);
class Api {
    HMODULE library=nullptr;
    template<class T> bool bind(T& out,const char* name){out=reinterpret_cast<T>(GetProcAddress(library,name));return out!=nullptr;}
public:
    uint32_t(__cdecl* version)()=nullptr;
    uint32_t(__cdecl* open)(const uint8_t*,size_t,void**,Cancel,void*)=nullptr;
    void(__cdecl* close)(void*)=nullptr;
    uint32_t(__cdecl* info)(const void*,Info*)=nullptr;
    uint32_t(__cdecl* row)(const void*,size_t,Row*,uint8_t*,size_t)=nullptr;
    uint32_t(__cdecl* warnings)(const void*,uint8_t*,size_t)=nullptr;
    uint32_t(__cdecl* channelName)(const void*,size_t,uint8_t*,size_t,size_t*)=nullptr;
    uint32_t(__cdecl* render)(const void*,const State*,size_t,int32_t,int32_t,Pixels*,Cancel,void*)=nullptr;
    uint32_t(__cdecl* thumbnail)(const void*,size_t,uint32_t,uint32_t,Pixels*,Cancel,void*)=nullptr;
    void(__cdecl* freePixels)(Pixels*)=nullptr;
    bool valid=false;
    Api(){
        std::wstring path(32768,L'\0');const DWORD size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(!size||size>=path.size())return;path.resize(size);path.resize(path.find_last_of(L"\\/")+1);path+=L"YeLayerBackend.dll";
        library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library)return;
        valid=bind(version,"yl_version")&&bind(open,"yl_open")&&bind(close,"yl_close")&&bind(info,"yl_info")&&
            bind(row,"yl_row")&&bind(warnings,"yl_warnings")&&bind(channelName,"yl_channel_name")&&bind(render,"yl_render")&&bind(freePixels,"yl_free_pixels")&&version()==1;
        bind(thumbnail,"yl_thumbnail"); // v1扩展；不把旧候选DLL加载失败冒充文档错误。
    }
    ~Api(){if(library)FreeLibrary(library);}
    Api(const Api&)=delete;Api& operator=(const Api&)=delete;
    static std::shared_ptr<Api> get(){static const auto value=std::make_shared<Api>();return value;}
};
inline std::wstring utf8(std::span<const uint8_t> bytes){
    if(bytes.empty())return {};
    const auto* data=reinterpret_cast<const char*>(bytes.data());
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,static_cast<int>(bytes.size()),nullptr,0);
    if(count<=0)throw std::runtime_error("invalid UTF-8 backend name");
    std::wstring out(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,static_cast<int>(bytes.size()),out.data(),count);return out;
}
class Document {
    std::shared_ptr<Api> api=Api::get();void* handle=nullptr;
public:
    Info metadata{};std::vector<Row> rows;std::vector<std::wstring> names,channelNames;std::string warning;
    uint32_t error=0;
    explicit Document(std::span<const uint8_t> bytes,const std::function<bool()>& cancel={}){
        if(!api->valid){error=5;return;}
        error=api->open(bytes.data(),bytes.size(),&handle,cancel?checkCancel:nullptr,const_cast<std::function<bool()>*>(&cancel));if(error||!handle)return;
        try{
            if(api->info(handle,&metadata)||metadata.layers>10000||metadata.warningBytes>(1u<<20))throw std::runtime_error("backend info");
            for(uint32_t i=0;i<metadata.layers;++i){
                Row row{};if(api->row(handle,i,&row,nullptr,0)||row.nameBytes>65536)throw std::runtime_error("backend row");
                std::vector<uint8_t> text(row.nameBytes);if(api->row(handle,i,&row,text.data(),text.size()))throw std::runtime_error("backend name");
                rows.push_back(row);names.push_back(utf8(text));
            }
            warning.resize(metadata.warningBytes);if(api->warnings(handle,reinterpret_cast<uint8_t*>(warning.data()),warning.size()))throw std::runtime_error("backend warnings");
            if(metadata.channels>56)throw std::runtime_error("backend channel count");
            for(size_t i=0;i<metadata.channels;++i){size_t length=0;
                if(api->channelName(handle,i,nullptr,0,&length)||length>65536)throw std::runtime_error("backend channel name");
                std::vector<uint8_t> text(length);if(api->channelName(handle,i,text.data(),text.size(),&length))throw std::runtime_error("backend channel name");
                channelNames.push_back(utf8(text));
            }
        }catch(...){api->close(handle);handle=nullptr;error=1;}
    }
    ~Document(){if(handle)api->close(handle);}
    Document(const Document&)=delete;Document& operator=(const Document&)=delete;
    bool valid()const{return handle&&metadata.width&&metadata.height;}
    bool hasThumbnails()const{return valid()&&api->thumbnail;}
    PsdCompositeDecoder::Image thumbnail(int index,bool mask=false,uint32_t side=32,const std::function<bool()>& cancel={}){
        PsdCompositeDecoder::Image result;if(!hasThumbnails()||index<0)return result;Pixels pixels{};
        error=api->thumbnail(handle,size_t(index),mask?1:0,side,&pixels,cancel?checkCancel:nullptr,const_cast<std::function<bool()>*>(&cancel));
        struct Release{Api* api;Pixels* pixels;~Release(){api->freePixels(pixels);}} release{api.get(),&pixels};
        if(error||side<1||side>128||!pixels.data||pixels.width!=side||pixels.height!=side||pixels.length!=size_t(side)*side*4)return result;
        result.width=result.height=side;result.hasAlpha=true;result.bgra.assign(pixels.data,pixels.data+pixels.length);result.icc=PsdCompositeDecoder::srgbProfile();return result;
    }
    std::vector<State> originalStates()const{
        std::vector<State> states;for(const auto& r:rows)states.push_back({r.flags&1,(r.flags>>2)&1,r.opacity,r.fill,r.blend});return states;
    }
    PsdLayers::Document tree(const PsdLayers::Document* raw=nullptr)const{
        PsdLayers::Document out;out.valid=valid();out.width=metadata.width;out.height=metadata.height;out.depth=metadata.depth;out.mode=metadata.mode;
        for(size_t i=0;i<rows.size();++i){const auto& r=rows[i];PsdLayers::Layer l;
            l.name=names[i];l.visible=(r.flags&1)!=0;l.masked=(r.flags&2)!=0;l.vectorMask=(r.flags&8)!=0;
            if(raw&&i<raw->layers.size()&&raw->layers.size()==rows.size()&&raw->layers[i].name==names[i]){
                const auto& original=raw->layers[i];l.left=original.left;l.top=original.top;l.right=original.right;l.bottom=original.bottom;
                l.channels=original.channels;l.maskData=original.maskData;
            }
            l.clipped=(r.flags&16)!=0;l.adjustment=(r.flags&64)!=0;l.effects=(r.flags&128)!=0;l.opacity=static_cast<uint8_t>(std::lround(r.opacity*255));
            l.fillOpacity=static_cast<uint8_t>(std::lround(r.fill*255));l.sectionType=r.section;l.group=r.section!=0;
            l.blend.assign(r.blend.data(),4);out.layers.push_back(std::move(l));
        }return out;
    }
    PsdCompositeDecoder::Image render(std::span<const State> states,int solo=-1,int channel=0,const std::function<bool()>& cancel={}){
        PsdCompositeDecoder::Image result;if(!valid())return result;Pixels pixels{};
        error=api->render(handle,states.data(),states.size(),solo,channel,&pixels,cancel?checkCancel:nullptr,const_cast<std::function<bool()>*>(&cancel));
        // 无论状态码如何，返回缓冲都由同一模块回收。C++异常也不得泄漏Rust分配。
        struct Release{Api* api;Pixels* data;~Release(){api->freePixels(data);}} release{api.get(),&pixels};
        if(error||!pixels.data||pixels.width!=metadata.width||pixels.height!=metadata.height||
            uint64_t(pixels.width)*pixels.height*4!=pixels.length||pixels.length>(64ULL<<20))return {};
        result.width=pixels.width;result.height=pixels.height;result.hasAlpha=true;
        result.bgra.assign(pixels.data,pixels.data+pixels.length);result.icc=PsdCompositeDecoder::srgbProfile();return result;
    }
};
}
