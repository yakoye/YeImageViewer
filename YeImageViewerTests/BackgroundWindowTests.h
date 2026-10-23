#pragma once

// 真窗口合成探针。底图是自造条纹，不读取或依赖用户桌面内容；窗口和配置仅在临时运行区。
inline int runBackgroundWindowProbe(const std::filesystem::path& viewer,
    const std::filesystem::path& source) {
    std::cout << std::unitbuf;
    const int beforeFailed = failedTests, beforePassed = passedTests;
    const auto probe = viewer.parent_path() / L"background-probe.txt";
    auto pump = [](int milliseconds) {
        const auto end = GetTickCount64() + milliseconds;
        do { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        } Sleep(5); } while (GetTickCount64() < end);
    };
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"YeImageViewer.BackgroundRegression";
    wc.lpfnWndProc = [](HWND window, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps{}; const HDC dc = BeginPaint(window, &ps);
            RECT r{}; GetClientRect(window, &r); POINT origin{}; ClientToScreen(window, &origin);
            HBRUSH brushes[] = {CreateSolidBrush(RGB(240,60,20)), CreateSolidBrush(RGB(20,80,240))};
            if(GetWindowLongPtrW(window,GWLP_USERDATA)) {
                auto solid=CreateSolidBrush(RGB(230,230,30));FillRect(dc,&r,solid);DeleteObject(solid);
            } else for (int x = 0; x < r.right; ++x) {
                RECT stripe{x, 0, x+1, r.bottom}; FillRect(dc, &stripe, brushes[((x+origin.x)/8)&1]);
            }
            for (auto brush : brushes) DeleteObject(brush);
            EndPaint(window, &ps); return 0;
        }
        return DefWindowProcW(window, msg, w, l);
    };
    RegisterClassW(&wc);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    HWND behind = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName,
        L"背景合成回归底图", WS_POPUP, mi.rcWork.left, mi.rcWork.top,
        mi.rcWork.right-mi.rcWork.left, mi.rcWork.bottom-mi.rcWork.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if(!behind) {UnregisterClassW(wc.lpszClassName,wc.hInstance);return 2;}
    ShowWindow(behind, SW_SHOWNOACTIVATE); UpdateWindow(behind);
    wchar_t previous[32768]{};
    const auto hadPrevious = GetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", previous, 32768);
    SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", probe.c_str());
    PROCESS_INFORMATION process{}; STARTUPINFOW startup{sizeof(startup)};
    std::wstring command = L"\""+viewer.wstring()+L"\" \""+source.wstring()+L"\"";
    const bool started = CreateProcessW(viewer.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, viewer.parent_path().c_str(), &startup, &process) != FALSE;
    SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", hadPrevious ? previous : nullptr);
    HWND window = nullptr;
    if (started) {
        struct Query {DWORD pid; HWND hwnd=nullptr;} query{process.dwProcessId};
        for (int i=0;i<300&&!window;++i) {
            EnumWindows([](HWND h, LPARAM value)->BOOL {
                auto& q=*reinterpret_cast<Query*>(value); DWORD pid{};GetWindowThreadProcessId(h,&pid);
                wchar_t cls[64]{};GetClassNameW(h,cls,64);
                if(pid==q.pid&&std::wstring_view(cls)==L"D3D11WndClass"&&IsWindowVisible(h)) {q.hwnd=h;return FALSE;}
                return TRUE;
            },reinterpret_cast<LPARAM>(&query)); window=query.hwnd;pump(10);
        }
    }
    passOrFail("background probe opens an isolated real viewer", window != nullptr);
    if (window) {
        passOrFail("background probe activates its own viewer before measuring active acrylic",activateRegressionWindow(window));
        for(int i=0;i<300;++i) {auto s=readWindowProbe(probe);if(s.contains("loading")&&s.at("loading")==0)break;pump(10);}
        // 默认沉浸态与恢复后的普通窗口都检验；普通窗口给足留白，避免误采图像。
        for (int shape=0;shape<2;++shape) {
            if(shape) {
                SendMessageW(window, WM_COMMAND, 1009, 0); pump(200);
                SetWindowPos(window,HWND_TOPMOST,mi.rcWork.left+80,mi.rcWork.top+80,700,500,SWP_SHOWWINDOW);
            }
            for (UINT action : {1100u,1101u,1102u,1103u,1104u,1105u,1106u,1100u}) {
                SendMessageW(window, WM_COMMAND, action, 0); pump(350);
                const auto state=readWindowProbe(probe);
                passOrFail("real context menu has five background choices and three checkerboard tones",
                    state.contains("backgroundMenuValid")&&state.at("backgroundMenuValid")==1);
                const int mode= action<1104 ? int(action-1100):4;
                passOrFail("menu changes the actual background mode in both window shapes",
                    state.contains("backgroundMode")&&state.at("backgroundMode")==mode);
                if(action>=1104) passOrFail("checkerboard choice reaches persistent settings",
                    state.contains("checkerboardTone")&&state.at("checkerboardTone")==action-1104);
                POINT sample{24,130};
                if(action==1103&&state.contains("left")&&state.contains("top")&&state.contains("bottom"))
                    sample={int(state.at("left"))+4,int((state.at("top")+state.at("bottom"))/2)};
                ClientToScreen(window,&sample);
                HDC screen=GetDC(nullptr);
                int mismatches=0; double variation=0;COLORREF prev=CLR_INVALID;
                const int sampleCount=action==1103?32:64;
                for(int x=0;x<sampleCount;++x) {
                    const auto pixel=GetPixel(screen,sample.x+x,sample.y);
                    const auto backing=(((sample.x+x)/8)&1)?RGB(20,80,240):RGB(240,60,20);
                    // 这里采样的是图片外留白：五种选项都不应把留白涂成白/黑/棋盘格。
                    const COLORREF expected=RGB((GetRValue(backing)*102+127)/255,
                        (GetGValue(backing)*102+127)/255,(GetBValue(backing)*102+127)/255);
                    if(action!=1103&&(abs(int(GetRValue(pixel))-GetRValue(expected))>3||
                        abs(int(GetGValue(pixel))-GetGValue(expected))>3||abs(int(GetBValue(pixel))-GetBValue(expected))>3))++mismatches;
                    if(prev!=CLR_INVALID) variation+=abs(int(GetRValue(pixel))-GetRValue(prev))+abs(int(GetBValue(pixel))-GetBValue(prev));
                    prev=pixel;
                }
                ReleaseDC(nullptr,screen);
                if(action!=1103 && state.contains("left")&&state.contains("right")&&
                    state.contains("top")&&state.contains("bottom")) {
                    screen=GetDC(nullptr);
                    for(int area=0;area<3;++area) {
                        const double fraction=area==0?.125:area==1?.375:.75;
                        POINT p{int(state.at("left")+(state.at("right")-state.at("left"))*fraction),
                            int((state.at("top")+state.at("bottom"))/2)};
                        ClientToScreen(window,&p);
                        const auto actual=GetPixel(screen,p.x,p.y);
                        const auto back=((p.x/8)&1)?RGB(20,80,240):RGB(240,60,20);
                        const int mode=action<1104?int(action-1100):4;
                        const auto tone=action==1105?CheckerboardTone::Light:action==1106?CheckerboardTone::Dark:CheckerboardTone::Automatic;
                        const uint32_t theme=state.contains("themeDark")&&state.at("themeDark")==1?0xFF202020u:0xFFFFFFFFu;
                        POINT local=p;ScreenToClient(window,&local);
                        const auto pattern=BackgroundRenderer::canvasPixel(static_cast<BackgroundMode>(mode),false,local.x,local.y,theme,tone);
                        const auto patternColor=RGB((pattern>>16)&255,(pattern>>8)&255,pattern&255);
                        const auto background=mode==0?back:patternColor;
                        COLORREF expected=background;
                        if(area==1) expected=RGB((128*128+GetRValue(background)*127+127)/255,
                            (64*128+GetGValue(background)*127+127)/255,(32*128+GetBValue(background)*127+127)/255);
                        if(area==2) expected=RGB(18,171,239);
                        passOrFail("图片背景仅在透明及半透明图像像素处生效，不改变不透明像素",
                            abs(int(GetRValue(actual))-GetRValue(expected))<=3&&
                            abs(int(GetGValue(actual))-GetGValue(expected))<=3&&
                            abs(int(GetBValue(actual))-GetBValue(expected))<=3);
                    }
                    ReleaseDC(nullptr,screen);
                }
                if(action==1103) {
                    if(state.contains("backgroundRemoteSession")&&state.at("backgroundRemoteSession")==1)passOrFail("remote graphics session selects live compatibility blur",state.contains("blurCompatibility")&&state.at("blurCompatibility")==1);
                    passOrFail("system blur was activated rather than silently painted solid gray",
                        state.contains("frostedActive")&&state.at("frostedActive")==1);
                    passOrFail("frosted backdrop attenuates the high-frequency stripes",variation<1000);
                    const auto first=prev;
                    SetWindowLongPtrW(behind,GWLP_USERDATA,1);InvalidateRect(behind,nullptr,FALSE);pump(350);
                    screen=GetDC(nullptr);const auto changed=GetPixel(screen,sample.x+sampleCount-1,sample.y);ReleaseDC(nullptr,screen);
                    std::cout<<"DIAG blur first="<<first<<" changed="<<changed<<" foreground="<<GetForegroundWindow()<<" viewer="<<window<<"\n";
                    passOrFail("blur responds to live backing content rather than a static gray fill",
                        abs(int(GetRValue(first))-GetRValue(changed))+abs(int(GetGValue(first))-GetGValue(changed))+
                        abs(int(GetBValue(first))-GetBValue(changed))>60);
                    SetWindowLongPtrW(behind,GWLP_USERDATA,0);InvalidateRect(behind,nullptr,FALSE);pump(100);
                    std::cout<<"DIAG blur variation="<<variation<<"\n";
                } else passOrFail("选择任一图片背景后图片外仍为原透明灰",mismatches==0);
                std::cout<<"DIAG shape="<<shape<<" command="<<action<<" mismatches="<<mismatches<<"\n";
            }
        }
        // 结束在明确深色棋盘格，关闭后脚本重启同一临时副本检查磁盘持久化。
        SendMessageW(window,WM_COMMAND,1106,0);pump(100);
        PostMessageW(window,WM_CLOSE,0,0);
    }
    if(started) {
        const bool exited=WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;
        passOrFail("background probe viewer exits normally and saves its settings",exited);
        if(!exited)TerminateProcess(process.hProcess,2);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
    // 用同一 EXE/同一临时 DB 真正重启，不只检查内存中的设置。
    if(window) {
        SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",probe.c_str());
        std::filesystem::remove(probe);
        command=L"\""+viewer.wstring()+L"\" \""+source.wstring()+L"\"";
        process={};const bool restarted=CreateProcessW(viewer.c_str(),command.data(),nullptr,nullptr,FALSE,0,
            nullptr,viewer.parent_path().c_str(),&startup,&process)!=FALSE;
        SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",hadPrevious?previous:nullptr);
        std::unordered_map<std::string,double> restored;
        if(restarted) {
            for(int i=0;i<300;++i) {restored=readWindowProbe(probe);if(restored.contains("loading")&&restored.at("loading")==0)break;pump(10);}
            passOrFail("restart restores checkerboard mode and manually selected dark tone",
                restored.contains("backgroundMode")&&restored.at("backgroundMode")==4&&
                restored.contains("checkerboardTone")&&restored.at("checkerboardTone")==2);
            EnumWindows([](HWND h,LPARAM value)->BOOL {DWORD pid{};GetWindowThreadProcessId(h,&pid);
                if(pid==DWORD(value)&&GetWindow(h,GW_OWNER)==nullptr)PostMessageW(h,WM_CLOSE,0,0);return TRUE;
            },LPARAM(process.dwProcessId));
            const bool exited=WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;
            passOrFail("restarted viewer closes cleanly",exited);if(!exited)TerminateProcess(process.hProcess,2);
            CloseHandle(process.hThread);CloseHandle(process.hProcess);
        } else passOrFail("viewer restarts for settings persistence verification",false);
    }
    DestroyWindow(behind);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::cout<<passedTests-beforePassed<<" passed, "<<failedTests-beforeFailed<<" failed\n";
    return failedTests==beforeFailed?0:2;
}
