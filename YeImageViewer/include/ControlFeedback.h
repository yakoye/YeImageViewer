#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>

// 自绘控件共用鼠标进入/离开状态；不接管原生按钮的捕获、键盘或点击语义。
namespace ControlFeedback {
inline constexpr wchar_t HOT_PROPERTY[]=L"YeImageViewer.ControlHot";
inline bool hot(HWND window){return window&&IsWindowEnabled(window)&&GetPropW(window,HOT_PROPERTY)!=nullptr;}
inline void setHot(HWND window,bool value){
    value=value&&IsWindowEnabled(window);
    if((GetPropW(window,HOT_PROPERTY)!=nullptr)==value)return;
    if(value)SetPropW(window,HOT_PROPERTY,reinterpret_cast<HANDLE>(1));else RemovePropW(window,HOT_PROPERTY);
    InvalidateRect(window,nullptr,FALSE);
}
inline LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM key,LPARAM param,UINT_PTR id,DWORD_PTR){
    if(message==WM_MOUSEMOVE){RECT r{};GetClientRect(window,&r);POINT p{short(LOWORD(param)),short(HIWORD(param))};
        setHot(window,PtInRect(&r,p)!=FALSE);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);}
    if(message==WM_MOUSELEAVE||message==WM_CANCELMODE||message==WM_CAPTURECHANGED||
        (message==WM_ENABLE&&!key)||(message==WM_SHOWWINDOW&&!key))setHot(window,false);
    if(message==WM_NCDESTROY){RemovePropW(window,HOT_PROPERTY);RemoveWindowSubclass(window,procedure,id);}
    return DefSubclassProc(window,message,key,param);
}
inline void attach(HWND window){SetWindowSubclass(window,procedure,0x59454642,0);}
inline COLORREF accent(bool dark){return dark?RGB(134,202,255):RGB(0,95,175);}
inline COLORREF background(bool dark,bool selected,bool hovered,bool pressed,bool disabled){
    if(disabled){hovered=false;pressed=false;}
    if(selected)return dark?(pressed?RGB(70,108,148):hovered?RGB(60,94,132):RGB(48,76,108)):
        (pressed?RGB(169,204,237):hovered?RGB(194,220,245):RGB(215,232,249));
    return dark?(pressed?RGB(72,88,108):hovered?RGB(57,68,83):RGB(43,47,54)):
        (pressed?RGB(187,212,237):hovered?RGB(224,237,250):RGB(247,249,252));
}
// 色块只增加双色轮廓，内部真实颜色不随悬停变亮，避免误导取色。
inline void outline(HDC dc,RECT r,bool pressed,int thickness){
    thickness=std::max(1,thickness);const auto outer=CreateSolidBrush(pressed?RGB(255,255,255):RGB(0,0,0));
    const auto inner=CreateSolidBrush(pressed?RGB(0,0,0):RGB(255,255,255));
    for(int i=0;i<thickness;++i){FrameRect(dc,&r,outer);InflateRect(&r,-1,-1);}
    for(int i=0;i<thickness;++i){FrameRect(dc,&r,inner);InflateRect(&r,-1,-1);}
    DeleteObject(outer);DeleteObject(inner);
}
}
