#pragma once
#include <unordered_map>

inline std::unordered_map<std::string, double> readWindowProbe(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::string line;
    std::unordered_map<std::string, double> result;
    while (std::getline(file, line))
    {
        const auto equal = line.find('=');
        if (equal == std::string::npos)
            continue;
        try
        {
            result[line.substr(0, equal)] = std::stod(line.substr(equal + 1));
        }
        catch (...)
        {
        }
    }
    return result;
}

// 真进程、真 WM_COMMAND/客户区鼠标消息。只操作临时副本；同步点击点的鼠标位置，结束恢复。
inline int runCornerCloseProbe(const std::filesystem::path &viewer, const std::filesystem::path &source,
                               bool scaleProbe = false, bool staleHover = false)
{
    if (!std::filesystem::is_regular_file(viewer) || !std::filesystem::is_regular_file(source))
        return 2;
    std::wstring command = L"\"" + viewer.wstring() + L"\" \"" + source.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto probe = viewer.parent_path() / L"transition-probe.txt";
    wchar_t previous[32768]{};
    const auto existed = GetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", previous, 32768);
    if (scaleProbe)
        SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", probe.c_str());
    const bool started = CreateProcessW(viewer.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        viewer.parent_path().c_str(), &startup, &process) != FALSE;
    if (scaleProbe)
        SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE", existed ? previous : nullptr);
    if (!started)
        return 2;
    WaitForInputIdle(process.hProcess, 5000);
    struct Target
    {
        DWORD pid;
        HWND hwnd;
    };
    Target target{process.dwProcessId, nullptr};
    for (int attempt = 0; attempt < 300 && !target.hwnd; ++attempt)
    {
        EnumWindows(
            [](HWND hwnd, LPARAM context) -> BOOL {
                auto *found = reinterpret_cast<Target *>(context);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (pid == found->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd))
                {
                    found->hwnd = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&target));
        Sleep(10);
    }
    bool okay = false;
    POINT originalCursor{};
    const bool haveOriginalCursor = GetCursorPos(&originalCursor) != FALSE;
    std::string stage = "window-not-found";
    if (target.hwnd)
    {
        stage = "await-image";
        for (int attempt = 0; attempt < 300; ++attempt)
        {
            wchar_t title[1024]{};
            GetWindowTextW(target.hwnd, title, 1024);
            if (std::wstring_view(title).find(source.filename().wstring()) != std::wstring_view::npos &&
                std::wstring_view(title).find(L"1x1 ") == std::wstring_view::npos &&
                std::wstring_view(title).find(L'%') != std::wstring_view::npos)
                break;
            Sleep(10);
        }
        // 默认启动可能已沉浸；先恢复有边框，避免把“退出沉浸”误当“进入普通全屏”。
        if (!scaleProbe && !(GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION))
        {
            PostMessageW(target.hwnd, WM_COMMAND, 1009, 0);
            for (int attempt = 0; attempt < 200 && !(GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION);
                 ++attempt)
                Sleep(10);
        }
        wchar_t loadedTitle[1024]{};
        GetWindowTextW(target.hwnd, loadedTitle, 1024);
        if (scaleProbe && std::wstring_view(loadedTitle).find(L"2000x4000") == std::wstring_view::npos)
        {
            stage = "portrait-fixture-did-not-load";
        }
        else if (scaleProbe)
        {
            RECT client{};
            GetClientRect(target.hwnd, &client);
            // 从资源管理器双击文件打开后的默认沉浸态开始；不能先恢复再重新进入，
            // 那样会把启动后的适应窗口缩放换成默认沉浸布局，掩盖旧缺陷。
            for (int attempt = 0; attempt < 200 && (GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION);
                 ++attempt)
                Sleep(10);
            Sleep(250);
            GetClientRect(target.hwnd, &client);
            const auto entered = readWindowProbe(probe);
            // 精确复现双击后直接点留白；提前手动缩放会设置临时缩放标记，绕过旧缺陷。
            Sleep(400);
            const auto before = readWindowProbe(probe);
            GetClientRect(target.hwnd, &client);
            // 从真实显示矩形选留白，不写死“某个坐标一定不是图片”。
            int x = -1, y = -1;
            if (before.contains("left") && before.at("left") > 20)
            {
                x = 10;
                y = client.bottom / 2;
            }
            else if (before.contains("right") && before.at("right") < client.right - 20)
            {
                x = client.right - 10;
                y = client.bottom / 2;
            }
            else if (before.contains("top") && before.at("top") > 60)
            {
                x = client.right / 2;
                y = 20;
            }
            if (x >= 0 && !(GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION))
            {
                // 合成的客户区点击必须与真实鼠标位置一致，否则捕获产生的真实移动消息
                // 会把点留白判成一次跨窗口的大拖动。先同步，再故意注入过期悬停。
                POINT clickPoint{x, y};
                ClientToScreen(target.hwnd, &clickPoint);
                SetCursorPos(clickPoint.x, clickPoint.y);
                Sleep(120);
                if (staleHover)
                {
                    const auto button = OverlayLayout::panMultiplierRect(client.right, client.bottom,
                                                                          GetDpiForWindow(target.hwnd));
                    SendMessageW(target.hwnd, WM_MOUSEMOVE, 0,
                                 MAKELPARAM(button.x + button.width / 2, button.y + button.height / 2));
                }
                else
                    SendMessageW(target.hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                SendMessageW(target.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                SendMessageW(target.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                for (int attempt = 0; attempt < 200 && !(GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION);
                     ++attempt)
                    Sleep(10);
                Sleep(400);
                const auto after = readWindowProbe(probe);
                okay = (GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION) && before.contains("zoomExact") &&
                       after.contains("zoomExact") && entered.contains("zoomExact") && before.contains("shape") &&
                       before.at("shape") == 1 && after.contains("shape") && after.at("shape") == 0 &&
                       std::abs(entered.at("zoomExact") - before.at("zoomExact")) < 0.000001 &&
                       before.contains("viewSettled") && before.at("viewSettled") == 1 &&
                       after.contains("viewSettled") && after.at("viewSettled") == 1 &&
                       std::abs(before.at("zoomExact") - after.at("zoomExact")) < 0.000001 &&
                       before.at("right") - before.at("left") == after.at("right") - after.at("left") &&
                       before.at("bottom") - before.at("top") == after.at("bottom") - after.at("top");
                stage = "entered=" + std::to_string(entered.contains("zoomExact") ? entered.at("zoomExact") : -1) +
                        ",zoom=" + std::to_string(before.contains("zoomExact") ? before.at("zoomExact") : -1) +
                        "->" + std::to_string(after.contains("zoomExact") ? after.at("zoomExact") : -1) +
                        ",shape=" + std::to_string(before.contains("shape") ? before.at("shape") : -1) +
                        "->" + std::to_string(after.contains("shape") ? after.at("shape") : -1) +
                        ",stale-hover=" + std::to_string(staleHover);
                // 同一张图：首次A -> 普通A -> 双击沉浸A，连续往返，不能只测退出方向。
                const auto waitShape=[&](int shape){
                    for(int attempt=0;attempt<250;++attempt){const auto state=readWindowProbe(probe);
                        if(state.contains("shape")&&state.at("shape")==shape&&state.contains("viewSettled")&&state.at("viewSettled")==1)return state;Sleep(10);}
                    return readWindowProbe(probe);
                };
                for(int phase=0;phase<2&&okay;++phase){
                    if(phase){SendMessageW(target.hwnd,WM_KEYDOWN,VK_UP,0);SendMessageW(target.hwnd,WM_KEYUP,VK_UP,0);Sleep(400);}
                    const auto baseline=waitShape(0);const double expected=baseline.contains("zoomExact")?baseline.at("zoomExact"):-1;
                    if(phase)okay=expected!=before.at("zoomExact");
                    for(int trip=0;trip<5&&okay;++trip){
                        GetClientRect(target.hwnd,&client);POINT inside{client.right/2,client.bottom/2};ClientToScreen(target.hwnd,&inside);SetCursorPos(inside.x,inside.y);Sleep(40);
                        SendMessageW(target.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(client.right/2,client.bottom/2));
                        SendMessageW(target.hwnd,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(client.right/2,client.bottom/2));
                        SendMessageW(target.hwnd,WM_LBUTTONUP,0,MAKELPARAM(client.right/2,client.bottom/2));
                        const auto immersive=waitShape(1);okay=immersive.contains("shape")&&immersive.at("shape")==1&&immersive.contains("zoomExact")&&std::abs(immersive.at("zoomExact")-expected)<1e-6;
                        if(!okay)break;
                        GetClientRect(target.hwnd,&client);int bx=-1,by=client.bottom/2;
                        if(immersive.contains("left")&&immersive.at("left")>20)bx=10;
                        else if(immersive.contains("right")&&immersive.at("right")<client.right-20)bx=client.right-10;
                        if(bx<0){okay=false;stage+=" no-background-on-roundtrip";break;}
                        POINT background{bx,by};ClientToScreen(target.hwnd,&background);SetCursorPos(background.x,background.y);Sleep(40);
                        SendMessageW(target.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(bx,by));SendMessageW(target.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(bx,by));SendMessageW(target.hwnd,WM_LBUTTONUP,0,MAKELPARAM(bx,by));
                        const auto normal=waitShape(0);okay=normal.contains("shape")&&normal.at("shape")==0&&normal.contains("zoomExact")&&std::abs(normal.at("zoomExact")-expected)<1e-6;
                        stage+=" roundtrip="+std::to_string(phase)+"/"+std::to_string(trip)+":"+std::to_string(okay);
                    }
                }
            }
            else
                stage = "not-immersive-or-no-background";
        }
        else
        {
            // 1009 是现有菜单“切换全屏”，与“沉浸显示”不同，能复现旧点击条件的遗漏。
            PostMessageW(target.hwnd, WM_COMMAND, 1009, 0);
            for (int attempt = 0; attempt < 200 && (GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION);
                 ++attempt)
                Sleep(10);
            Sleep(120);
            RECT client{};
            GetClientRect(target.hwnd, &client);
            stage = "fullscreen-style=" + std::to_string(GetWindowLongPtrW(target.hwnd, GWL_STYLE)) +
                    ",client=" + std::to_string(client.right) + "x" + std::to_string(client.bottom);
            if (client.right > 0 && client.bottom > 0 && !(GetWindowLongPtrW(target.hwnd, GWL_STYLE) & WS_CAPTION))
            {
                const LPARAM point = MAKELPARAM(client.right - 1, 0);
                PostMessageW(target.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point);
                PostMessageW(target.hwnd, WM_LBUTTONUP, 0, point);
                okay = WaitForSingleObject(process.hProcess, 4000) == WAIT_OBJECT_0;
                stage += ",wait-close=" + std::to_string(okay);
            }
        }
    }
    if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 3);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (haveOriginalCursor)
        SetCursorPos(originalCursor.x, originalCursor.y);
    std::cout << (okay ? "PASS" : "FAIL")
              << (scaleProbe ? " 打开沉浸后点留白恢复边框不改变实际倍率/显示矩形 "
                             : " 普通全屏最右上角像素可点击关闭真实进程 ")
              << stage << "\n";
    return okay ? 0 : 2;
}
