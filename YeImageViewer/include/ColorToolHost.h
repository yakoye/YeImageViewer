#pragma once
#include "CurrentColor.h"
#include "ColorCatalog.h"
#include <windows.h>
#include <filesystem>
#include <vector>
#include <cwctype>

// 同一程序目录/当前登录会话的独立颜色工作台。只传有界RGB/缩略像素，不传任意命令。
namespace ColorToolHost {
inline constexpr wchar_t PROPERTY[]=L"YeImageViewer.ColorHost.Identity";
inline constexpr ULONG_PTR PACKET=0x5945434Fu;
enum class Operation:uint32_t {Show,Adopt,Initialize};
struct Header {uint32_t version=1,operation=0,color=0,width=0,height=0,channels=0,dark=0,imageProvided=0,mode=0,language=0;};
inline std::filesystem::path executable(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return {};return path;}
inline uint64_t identity(){static const auto value=[] {uint64_t hash=1469598103934665603ULL;for(auto c:executable().lexically_normal().wstring()){hash^=towlower(c);hash*=1099511628211ULL;}return hash?hash:1;}();return value;}
inline bool matches(HWND hwnd){
    if(reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,PROPERTY))!=identity())return false;
    DWORD pid{};GetWindowThreadProcessId(hwnd,&pid);
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
    wchar_t path[32768]{};DWORD length=32768;const bool okay=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;CloseHandle(process);
    return okay&&_wcsicmp(std::filesystem::path(path).lexically_normal().c_str(),executable().lexically_normal().c_str())==0;
}
inline HWND find(){HWND result=nullptr;EnumWindows([](HWND hwnd,LPARAM data)->BOOL {
    if(matches(hwnd)){*reinterpret_cast<HWND*>(data)=hwnd;return FALSE;}return TRUE;
},reinterpret_cast<LPARAM>(&result));return result;}
inline bool send(HWND target,Operation operation,ColorValue::Rgb color,const cv::Mat& image={},bool dark=false,bool imageProvided=false,uint32_t mode=0,uint32_t language=0) {
    if(!matches(target))return false;
    Header header;header.operation=uint32_t(operation);header.color=CurrentColor::pack(color);header.dark=dark;header.imageProvided=imageProvided;header.mode=mode;header.language=language;
    if(!image.empty()&&image.depth()==CV_8U&&(image.channels()==3||image.channels()==4)&&image.cols<=512&&image.rows<=512){header.width=image.cols;header.height=image.rows;header.channels=image.channels();}
    const size_t row=size_t(header.width)*header.channels;std::vector<uint8_t> payload(sizeof(header)+row*header.height);memcpy(payload.data(),&header,sizeof(header));
    for(uint32_t y=0;y<header.height;++y)memcpy(payload.data()+sizeof(header)+y*row,image.ptr(y),row);
    DWORD pid{};GetWindowThreadProcessId(target,&pid);AllowSetForegroundWindow(pid);
    COPYDATASTRUCT packet{PACKET,DWORD(payload.size()),payload.data()};DWORD_PTR result{};
    return SendMessageTimeoutW(target,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&packet),SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)&&result==1;
}
struct Message {Operation operation;ColorValue::Rgb color;cv::Mat image;bool dark,imageProvided;uint32_t mode,language;};
inline std::optional<Message> receive(const COPYDATASTRUCT* data){
    if(!data||data->dwData!=PACKET||!data->lpData||data->cbData<sizeof(Header)||data->cbData>sizeof(Header)+512*512*4)return {};
    Header header{};memcpy(&header,data->lpData,sizeof(header));
    if(header.version!=1||header.operation>2||header.color>0xFFFFFF||header.dark>1||header.imageProvided>1||header.width>512||header.height>512||header.mode>2||header.language>2)return {};
    const bool empty=header.width==0&&header.height==0&&header.channels==0;
    if(!empty&&(!header.width||!header.height||(header.channels!=3&&header.channels!=4)))return {};
    const size_t bytes=size_t(header.width)*header.height*header.channels;if(data->cbData!=sizeof(Header)+bytes)return {};
    Message message{Operation(header.operation),CurrentColor::unpack(header.color),{},header.dark!=0,header.imageProvided!=0,header.mode,header.language};
    if(!empty)message.image=cv::Mat(int(header.height),int(header.width),CV_MAKETYPE(CV_8U,header.channels),static_cast<uint8_t*>(data->lpData)+sizeof(Header)).clone();
    return message;
}
class Guard {
    HANDLE mutex=nullptr;bool acquired=false;
public:
    Guard(){const auto name=L"Local\\YeImageViewer.ColorPanel."+std::to_wstring(identity());mutex=CreateMutexW(nullptr,FALSE,name.c_str());if(!mutex)return;
        for(int i=0;i<200;++i){const auto state=WaitForSingleObject(mutex,0);if(state==WAIT_OBJECT_0||state==WAIT_ABANDONED){acquired=true;break;}if(find())break;Sleep(10);}}
    ~Guard(){if(mutex){if(acquired)ReleaseMutex(mutex);CloseHandle(mutex);}}
    bool owns()const{return acquired;}
};
inline bool launch(bool adopt,bool dark,uint32_t mode,uint32_t language){
    const auto image=ColorCatalog::getCurrent();
    if(const HWND target=find())return send(target,adopt?Operation::Adopt:Operation::Show,CurrentColor::get(),image,dark,true,mode,language);
    const auto file=executable();std::wstring command=L"\""+file.wstring()+L"\" --color-panel --service";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,file.parent_path().c_str(),&startup,&process))return false;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    for(int i=0;i<200;++i){if(const HWND target=find())return send(target,adopt?Operation::Adopt:Operation::Initialize,CurrentColor::get(),image,dark,true,mode,language);Sleep(10);}
    return false;
}
}
