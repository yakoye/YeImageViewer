#pragma once
#include <windows.h>

// 仅用于已协调的桌面回归：不释放用户按下的键，不发输入到其他项目，短暂关联后立即解除。
inline bool activateRegressionWindow(HWND window){
    if(!window||((GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_SHIFT)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000))return false;
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    const DWORD current=GetCurrentThreadId(),foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),target=GetWindowThreadProcessId(window,nullptr);
    const bool attachForeground=foreground&&foreground!=current&&AttachThreadInput(current,foreground,TRUE);
    const bool attachTarget=target&&target!=current&&target!=foreground&&AttachThreadInput(current,target,TRUE);
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);
    if(attachTarget)AttachThreadInput(current,target,FALSE);if(attachForeground)AttachThreadInput(current,foreground,FALSE);
    return GetForegroundWindow()==window;
}
