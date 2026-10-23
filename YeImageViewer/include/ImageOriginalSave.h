#pragma once
#include "ImageExport.h"
#include <objbase.h>
#include <fstream>
#include <string>
#include <string_view>

// 替换只接受调用方已经编码、解码校验的内容。所有路径都是同目录的唯一新路径。
namespace ImageOriginalSave {
struct Result {bool saved=false;DWORD error=0;std::filesystem::path backup,recovery;};
// 覆盖只接受完整单图的8位PNG/JPEG。动图、MPO、尾附视频和高位深仍走副本，
// 不能因为主解码能显示第一帧就静默丢弃其他内容。
inline bool simple8BitRaster(std::span<const uint8_t> data,bool png){
    if(png){
        const uint8_t signature[]{137,80,78,71,13,10,26,10};
        if(data.size()<33||!std::equal(std::begin(signature),std::end(signature),data.begin())||data[24]!=8)return false;
        size_t p=8;bool header=false;
        while(p+12<=data.size()){
            const size_t n=(uint32_t(data[p])<<24)|(uint32_t(data[p+1])<<16)|(uint32_t(data[p+2])<<8)|data[p+3];
            if(n>data.size()-p-12)return false;const std::string_view type(reinterpret_cast<const char*>(data.data()+p+4),4);
            if(!header){if(type!="IHDR"||n!=13)return false;header=true;}
            if(type=="acTL"||type=="fcTL"||type=="fdAT")return false;
            p+=n+12;if(type=="IEND")return n==0&&p==data.size();
        }return false;
    }
    if(data.size()<4||data[0]!=255||data[1]!=216)return false;
    size_t p=2;bool frame=false;
    while(p<data.size()){
        if(data[p++]!=255)return false;while(p<data.size()&&data[p]==255)++p;if(p>=data.size())return false;
        const uint8_t code=data[p++];if(code==217)return frame&&p==data.size();
        if(code==0||code==216)return false;if(code==1||(code>=208&&code<=215))continue;
        if(p+2>data.size())return false;const size_t n=size_t(data[p])*256+data[p+1];if(n<2||n>data.size()-p)return false;
        if(code>=192&&code<=207&&code!=196&&code!=200&&code!=204){
            if((code!=192&&code!=193&&code!=194)||n<8||data[p+2]!=8||(data[p+7]!=1&&data[p+7]!=3))return false;frame=true;
        }
        if(code==226&&n>=6&&data[p+2]=='M'&&data[p+3]=='P'&&data[p+4]=='F'&&data[p+5]==0)return false;
        p+=n;
        if(code==218){
            while(p<data.size()){
                if(data[p]!=255){++p;continue;}
                if(p+1>=data.size())return false;
                if(data[p+1]==0||(data[p+1]>=208&&data[p+1]<=215)){p+=2;continue;}break;
            }
        }
    }return false;
}
inline std::vector<uint8_t> read(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return {};
    const auto length=input.tellg();if(length<=0||length>256LL*1024*1024)return {};
    std::vector<uint8_t> bytes(size_t(length),0);input.seekg(0);if(!input.read(reinterpret_cast<char*>(bytes.data()),length))return {};return bytes;
}
inline std::wstring token(){GUID guid{};wchar_t text[40]{};if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,text,40))return {};return text;}
template<class Replace>
inline Result replaceWith(const std::filesystem::path& source,std::span<const uint8_t> expected,std::span<const uint8_t> encoded,Replace replace){
    Result result;const auto id=token();if(id.empty()||expected.empty()||encoded.empty()){result.error=ERROR_INVALID_DATA;return result;}
    const auto attributes=GetFileAttributesW(source.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_READONLY))){result.error=ERROR_ACCESS_DENIED;return result;}
    const auto temp=std::filesystem::path(source.wstring()+L".ye-edit-"+id+L".tmp");
    result.backup=std::filesystem::path(source.wstring()+L".before-edit-"+id+L".bak");
    if(GetFileAttributesW(result.backup.c_str())!=INVALID_FILE_ATTRIBUTES||!ImageExport::writeNew(temp,encoded)){result.error=ERROR_WRITE_FAULT;return result;}
    const auto current=read(source);
    if(current.size()!=expected.size()||!std::equal(current.begin(),current.end(),expected.begin())){
        DeleteFileW(temp.c_str());result.error=ERROR_FILE_INVALID;return result;
    }
    if(replace(source,temp,result.backup)){result.saved=true;return result;}
    result.error=GetLastError();result.recovery=temp;
    // ReplaceFile的1177错误可能已把原图移到备份。只向缺失路径恢复，绝不覆盖新出现的文件。
    if(GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(result.backup.c_str())!=INVALID_FILE_ATTRIBUTES)
        CopyFileW(result.backup.c_str(),source.c_str(),TRUE);
    return result;
}
inline Result replace(const std::filesystem::path& source,std::span<const uint8_t> expected,std::span<const uint8_t> encoded){
    return replaceWith(source,expected,encoded,[](const auto& original,const auto& temp,const auto& backup){return ReplaceFileW(original.c_str(),temp.c_str(),backup.c_str(),0,nullptr,nullptr)!=FALSE;});
}
}
