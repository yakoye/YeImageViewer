#define UNICODE
#define _UNICODE

#include <windows.h>

namespace {

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        DestroyWindow(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT client{};
        GetClientRect(window, &client);
        DrawTextW(dc,
            L"Transparent window demo  |  Esc or right-click to close",
            -1, &client, DT_CENTER | DT_TOP | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    constexpr wchar_t className[] = L"YeImageViewerTransparentWindowDemo";
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass))
        return 1;

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{ sizeof(monitorInfo) };
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return 2;

    const RECT& area = monitorInfo.rcWork;
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        className,
        L"Transparent window demo",
        WS_POPUP,
        area.left,
        area.top,
        area.right - area.left,
        area.bottom - area.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window)
        return 3;

    // 0 is invisible, 255 is opaque. 88 keeps the desktop clearly visible.
    SetLayeredWindowAttributes(window, 0, 88, LWA_ALPHA);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
