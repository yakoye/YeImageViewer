#pragma once

#include "jarkUtils.h"

#include <optional>

// 重命名、编辑器名称和默认目标路径共用的原生输入框。保留原有窗口类名，
// 让中文 IME、取消恢复焦点以及真实窗口回归继续走同一条路径。
namespace TextInputDialog {

inline constexpr wchar_t RENAME_WINDOW_CLASS[] = L"YeImageViewerRenameWnd";
inline constexpr int RENAME_EDIT_ID = 1001;

struct RenameDialogState {
    std::wstring initialName;
    std::wstring title;
    std::wstring prompt;
    std::optional<std::wstring> result;
    HWND window = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    bool finished = false;
    int maxLength = 255;
};

inline int scaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

inline LRESULT CALLBACK RenameDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<RenameDialogState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<RenameDialogState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        const UINT dpi = GetDpiForWindow(window);
        NONCLIENTMETRICSW metrics{ .cbSize = sizeof(NONCLIENTMETRICSW) };
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        state->font = CreateFontIndirectW(&metrics.lfMessageFont);

        const auto makeControl = [&](const wchar_t* className, const wchar_t* text,
            DWORD style, int x, int y, int width, int height, int id) {
                HWND control = CreateWindowExW(0, className, text,
                    WS_CHILD | WS_VISIBLE | style,
                    scaleForDpi(x, dpi), scaleForDpi(y, dpi),
                    scaleForDpi(width, dpi), scaleForDpi(height, dpi),
                    window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    GetModuleHandleW(nullptr), nullptr);
                if (control && state->font)
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                return control;
            };

        makeControl(L"STATIC", state->prompt.c_str(), SS_LEFT,
            20, 16, 380, 22, -1);
        state->edit = makeControl(L"EDIT", state->initialName.c_str(),
            WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            20, 43, 380, 27, RENAME_EDIT_ID);
        makeControl(L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON,
            226, 88, 82, 30, IDOK);
        makeControl(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
            318, 88, 82, 30, IDCANCEL);
        if (!isChineseUI()) {
            SetDlgItemTextW(window, IDOK, L"OK");
            SetDlgItemTextW(window, IDCANCEL, L"Cancel");
        }
        else if (GlobalVar::settingParameter.UI_LANG == 2) {
            SetDlgItemTextW(window, IDOK, L"確定");
            SetDlgItemTextW(window, IDCANCEL, L"取消");
        }
        SendMessageW(state->edit, EM_SETLIMITTEXT, state->maxLength, 0);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == RENAME_EDIT_ID && HIWORD(wParam) == EN_CHANGE) {
            const HWND edit = reinterpret_cast<HWND>(lParam);
            const int length = GetWindowTextLengthW(edit);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(edit, value.data(), length + 1);
            value.resize(static_cast<size_t>(length));
            state->initialName = std::move(value);
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            const HWND edit = GetDlgItem(window, RENAME_EDIT_ID);
            const int length = GetWindowTextLengthW(edit);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(edit, value.data(), length + 1);
            value.resize(static_cast<size_t>(length));
            state->result = std::move(value);
            state->finished = true;
            ShowWindow(window, SW_HIDE);
            PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            state->finished = true;
            ShowWindow(window, SW_HIDE);
            PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        state->finished = true;
        ShowWindow(window, SW_HIDE);
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        return 0;

    case WM_DESTROY:
        state->window = nullptr;
        if (!state->finished) {
            state->finished = true;
            PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

inline std::optional<std::wstring> show(HWND owner, std::wstring initialName,
    std::wstring title, std::wstring prompt, int maxLength = 255) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{ .cbSize = sizeof(WNDCLASSEXW) };
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = RenameDialogProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_YEIMAGEVIEWER));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = RENAME_WINDOW_CLASS;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return std::nullopt;

    RenameDialogState state{ .initialName = std::move(initialName),
        .title = std::move(title), .prompt = std::move(prompt), .maxLength = maxLength };
    const UINT dpi = owner ? GetDpiForWindow(owner) : USER_DEFAULT_SCREEN_DPI;
    RECT outer{ 0, 0, scaleForDpi(420, dpi), scaleForDpi(138, dpi) };
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    const DWORD extendedStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    if (!AdjustWindowRectExForDpi(&outer, style, FALSE, extendedStyle, dpi))
        AdjustWindowRectEx(&outer, style, FALSE, extendedStyle);
    const int width = outer.right - outer.left;
    const int height = outer.bottom - outer.top;

    RECT ownerRect{};
    if (!owner || !GetWindowRect(owner, &ownerRect))
        ownerRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
    if (GetMonitorInfoW(MonitorFromRect(&ownerRect, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
        x = std::clamp(x, static_cast<int>(monitorInfo.rcWork.left),
            static_cast<int>(monitorInfo.rcWork.right) - width);
        y = std::clamp(y, static_cast<int>(monitorInfo.rcWork.top),
            static_cast<int>(monitorInfo.rcWork.bottom) - height);
    }

    HWND window = CreateWindowExW(extendedStyle, RENAME_WINDOW_CLASS, state.title.c_str(), style,
        x, y, width, height, owner, nullptr, instance, &state);
    if (!window) {
        if (state.font)
            DeleteObject(state.font);
        return std::nullopt;
    }

    const bool restoreOwner = owner && IsWindowEnabled(owner);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);
    SetFocus(state.edit);

    bool repostQuit = false;
    int quitCode = 0;
    MSG message{};
    while (!state.finished) {
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            repostQuit = status == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        const bool outsideMouseDown =
            message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN ||
            message.message == WM_MBUTTONDOWN || message.message == WM_XBUTTONDOWN ||
            message.message == WM_NCLBUTTONDOWN || message.message == WM_NCRBUTTONDOWN ||
            message.message == WM_NCMBUTTONDOWN || message.message == WM_NCXBUTTONDOWN;
        if (outsideMouseDown && message.hwnd != window &&
            !IsChild(window, message.hwnd)) {
            state.finished = true;
            ShowWindow(window, SW_HIDE);
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (IsWindow(window))
        DestroyWindow(window);
    if (state.font)
        DeleteObject(state.font);
    if (restoreOwner && IsWindow(owner)) {
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
        SetFocus(owner);
    }
    if (repostQuit)
        PostQuitMessage(quitCode);
    return state.result;
}

} // namespace TextInputDialog
