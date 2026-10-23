#pragma once
#include "PickerHandoff.h"
#include <windows.h>
#include <atomic>

// 两秒临时鼠标钩子：只吃掉用于打开颜色面板的一对右键，不记坐标、不常驻。
namespace PickerHandoff {
inline thread_local Gesture* gesture=nullptr;
inline LRESULT CALLBACK mouseHook(int code,WPARAM message,LPARAM data){
    if(code==HC_ACTION&&gesture){
        const auto now=GetTickCount64();
        if(message==WM_RBUTTONDOWN&&gesture->down(now))return 1;
        if(message==WM_RBUTTONUP&&gesture->up(now))return 1;
        if(message==WM_LBUTTONDOWN||message==WM_MBUTTONDOWN||message==WM_MOUSEWHEEL)gesture->cancel();
    }
    return CallNextHookEx(nullptr,code,message,data);
}
inline bool wait(const std::atomic<bool>& stop){
    MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
    if(stop)return false;
    Gesture state(GetTickCount64());gesture=&state;
    const HHOOK hook=SetWindowsHookExW(WH_MOUSE_LL,mouseHook,GetModuleHandleW(nullptr),0);
    if(!hook){gesture=nullptr;return false;}
    while(!stop&&state.active(GetTickCount64())){
        MsgWaitForMultipleObjectsEx(0,nullptr,40,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){state.cancel();break;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
    }
    UnhookWindowsHookEx(hook);gesture=nullptr;return !stop&&state.chosen();
}
}
