#pragma once
#include "PsdLayers.h"
#include <future>
#include <filesystem>

// 后台只读映射/解析图层记录，不解码像素、不占用首帧路径。只持有当前路径的能力。
class PsdCapability {
    struct Result {std::wstring path;int layers=0;uint64_t revision=0;};
    std::wstring requested,readyPath;
    std::future<Result> task;
    int count=0;
    uint64_t revision=0;
public:
    static int inspect(const std::wstring& path) {
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(file==INVALID_HANDLE_VALUE)return 0;
        LARGE_INTEGER size{};HANDLE mapping=nullptr;void* view=nullptr;int layers=0;
        if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=256LL*1024*1024) {
            mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
            if(mapping)view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
            if(view) {
                const auto doc=PsdLayers::read({static_cast<const uint8_t*>(view),size_t(size.QuadPart)});
                if(doc.valid && (doc.mode==1||doc.mode==3||doc.mode==4||doc.mode==9))for(const auto& layer:doc.layers) {
                    if(layer.group||layer.adjustment||layer.sectionType==3||layer.right<=layer.left||layer.bottom<=layer.top)continue;
                    if(int64_t(layer.right)-layer.left>300000 || int64_t(layer.bottom)-layer.top>300000)continue;
                    const int base=doc.mode==4?4:doc.mode==1?1:3;
                    const uint64_t pixels=uint64_t(int64_t(layer.right)-layer.left)*(int64_t(layer.bottom)-layer.top);
                    if(pixels*(uint64_t(doc.depth/8)*(base+1)*4+8)+doc.resources.size()>(256ULL<<20))continue;
                    bool readable=true;
                    for(int id=0;id<base;++id)readable &= std::any_of(layer.channels.begin(),layer.channels.end(),[id](const auto& c){
                        return c.id==id && c.data.size()>=2 && c.data[0]==0 && c.data[1]<=3;
                    });
                    if(readable)++layers;
                }
            }
        }
        if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);CloseHandle(file);return layers;
    }
    void reset() {++revision;requested.clear();readyPath.clear();count=0;}
    bool available(const std::wstring& path) const {return !path.empty()&&path==requested&&path==readyPath&&count>0;}
    int layers() const {return count;}
    bool poll(const std::wstring& path) {
        bool changed=false;
        if(path!=requested){++revision;requested=path;readyPath.clear();count=0;changed=true;}
        if(task.valid()&&task.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto result=task.get();if(result.path==requested&&result.revision==revision){readyPath=result.path;count=result.layers;changed=true;}
        }
        if(!task.valid()&&readyPath!=requested) {
            auto ext=std::filesystem::path(requested).extension().wstring();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return wchar_t(towlower(c));});
            if(ext!=L".psd"&&ext!=L".psb"&&ext!=L".psdt"){readyPath=requested;return changed;}
            const auto value=requested;
            const auto token=revision;
            try{task=std::async(std::launch::async,[value,token]{return Result{value,inspect(value),token};});}
            catch(...){readyPath=requested;count=0;}
        }
        return changed;
    }
};
