#pragma once

#include "jarkUtils.h"

// 轻量基类：为 Setting/Printer 提供 Win32 窗口骨架
// 使用 GDI StretchDIBits 渲染 cv::Mat
class MatWindow {
protected:
    HWND m_hwnd = nullptr;
    std::wstring m_className;
    std::wstring m_title;
    // 逻辑尺寸：子类的 cv::Mat 画布按它绘制，与窗口的物理像素尺寸无关。
    int m_width = 0;
    int m_height = 0;
    int m_x = 0;
    int m_y = 0;
    int m_dpi = USER_DEFAULT_SCREEN_DPI;

    int toPhysical(int logical) const {
        return MulDiv(logical, m_dpi, USER_DEFAULT_SCREEN_DPI);
    }

    // 鼠标消息给的是客户区物理坐标，而命中测试用的是画布的逻辑坐标，必须换算回去。
    int toLogical(int physical) const {
        return MulDiv(physical, USER_DEFAULT_SCREEN_DPI, m_dpi > 0 ? m_dpi : USER_DEFAULT_SCREEN_DPI);
    }

    // 按 DPI 放大后可能超出屏幕：打印预览逻辑高 950，200% 缩放就是 1900 物理像素，
    // 不少显示器放不下。这里把缩放压到工作区装得下的最大值，宁可显示小一点也不让
    // 窗口超出屏幕；下限是 96，保证不会比按物理像素布局的旧行为更小。
    int dpiFittedToWorkArea(int dpi) const {
        if (m_width <= 0 || m_height <= 0)
            return dpi;
        HMONITOR monitor = m_hwnd ?
            MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST) :
            MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{ .cbSize = sizeof(MONITORINFO) };
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return dpi;
        // 留出标题栏和边框的余量，避免客户区刚好占满、外框却溢出工作区。
        const int availableWidth = (info.rcWork.right - info.rcWork.left) * 94 / 100;
        const int availableHeight = (info.rcWork.bottom - info.rcWork.top) * 94 / 100;
        if (availableWidth <= 0 || availableHeight <= 0)
            return dpi;
        const int limit = std::min(availableWidth * USER_DEFAULT_SCREEN_DPI / m_width,
            availableHeight * USER_DEFAULT_SCREEN_DPI / m_height);
        return std::clamp(dpi, USER_DEFAULT_SCREEN_DPI,
            std::max(USER_DEFAULT_SCREEN_DPI, limit));
    }

    // 按当前 DPI 把窗口调整到 m_width × m_height 的逻辑客户区。
    void applyDpiScaledSize(const RECT* suggested = nullptr) {
        if (!m_hwnd)
            return;
        if (suggested) {
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        const DWORD style = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(m_hwnd, GWL_EXSTYLE));
        RECT rc{ 0, 0, toPhysical(m_width), toPhysical(m_height) };
        if (!AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, static_cast<UINT>(m_dpi)))
            AdjustWindowRectEx(&rc, style, FALSE, exStyle);
        SetWindowPos(m_hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }

    bool createWindow(int width, int height, const wchar_t* className, const wchar_t* title) {
        m_width = width;
        m_height = height;
        m_className = className;
        m_title = title;

        HINSTANCE hInstance = GetModuleHandleW(NULL);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = staticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = className;
        wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_YEIMAGEVIEWER));

        RegisterClassExW(&wc);

        // 固定尺寸窗口
        DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

        // 进程声明了 PerMonitorHighDPIAware，Windows 不会替我们放大，窗口必须自己按
        // DPI 撑到应有的物理尺寸，否则高分屏上整个界面只有该有大小的 1/缩放倍数。
        m_dpi = static_cast<int>(GetDpiForSystem());
        if (m_dpi <= 0)
            m_dpi = USER_DEFAULT_SCREEN_DPI;
        m_dpi = dpiFittedToWorkArea(m_dpi);

        // 计算窗口尺寸以获得指定客户区大小
        RECT rc = { 0, 0, toPhysical(width), toPhysical(height) };
        if (!AdjustWindowRectExForDpi(&rc, style, FALSE, 0, static_cast<UINT>(m_dpi)))
            AdjustWindowRect(&rc, style, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            className,
            title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, hInstance, this  // 传递 this 指针
        );

        if (!m_hwnd)
            return false;

        // 设置/打印窗口靠单键快捷键和快捷键录制工作，文本输入也是自绘的、不走
        // IME 合成，所以把输入法从窗口摘掉，保证按键原样送达。
        ::ImmAssociateContext(m_hwnd, nullptr);

        // CW_USEDEFAULT 可能把窗口放到 DPI 与主显示器不同的屏上，按实际落点校正。
        const int actualDpi = static_cast<int>(GetDpiForWindow(m_hwnd));
        if (actualDpi > 0) {
            const int fittedDpi = dpiFittedToWorkArea(actualDpi);
            if (fittedDpi != m_dpi) {
                m_dpi = fittedDpi;
                applyDpiScaledSize();
            }
        }

        jarkUtils::disableWindowResize(m_hwnd);

        // 设置图标
        HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_YEIMAGEVIEWER));
        if (hIcon) {
            SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }

        applyDarkModeAttribute();

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        return true;
    }

    virtual void drawingUI() = 0;

    void runMessageLoop() {
        MSG msg;

        std::thread drawThread([this]() {
            while (isDrawThreadRuning) {
                if (isNeedRefreshUI) {
                    isNeedRefreshUI = false;
                    drawingUI();
                    isDrawDone = true;
                    if (m_hwnd)
                        PostMessageW(m_hwnd, WM_MATWINDOW_DRAW_DONE, 0, 0);
                }
                else {
                    Sleep(10);
                }
            }
            });

        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            idleTask();
        }

        isDrawThreadRuning = false;
        drawThread.join();
    }

    virtual void idleTask() {}

    void invalidate() const {
        if (m_hwnd)
            InvalidateRect(m_hwnd, NULL, FALSE);
    }

    void blitMat(HDC hdc, const cv::Mat& bgra) {
        if (bgra.empty())
            return;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bgra.cols;
        bmi.bmiHeader.biHeight = -bgra.rows;  // 负数表示自顶向下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        // 画布是逻辑尺寸，客户区按 DPI 放大过，这里拉伸贴满。HALFTONE 让放大后的
        // 文字边缘平滑，代价是比 1:1 贴图慢一些——这两个窗口重绘不频繁，可以接受。
        RECT client{};
        int targetWidth = bgra.cols;
        int targetHeight = bgra.rows;
        if (m_hwnd && GetClientRect(m_hwnd, &client)) {
            targetWidth = std::max(1, static_cast<int>(client.right - client.left));
            targetHeight = std::max(1, static_cast<int>(client.bottom - client.top));
        }
        if (targetWidth != bgra.cols || targetHeight != bgra.rows) {
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);
        }

        StretchDIBits(hdc,
            0, 0, targetWidth, targetHeight,
            0, 0, bgra.cols, bgra.rows,
            bgra.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    void applyDarkModeAttribute() {
        if (!m_hwnd)
            return;
        BOOL themeMode = GlobalVar::isCurrentUIDarkMode;
        DwmSetWindowAttribute(m_hwnd, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE, &themeMode, sizeof(BOOL));
    }

    void applyIconAndTitle(const wchar_t* title) {
        if (m_hwnd)
            SetWindowTextW(m_hwnd, title);
    }

    // 子类实现
    virtual void onPaint(HDC hdc) = 0;
    virtual void onLButtonDown() {}
    virtual void onLButtonUp() {}
    virtual void onRButtonUp() {}
    virtual void onMouseMove(WPARAM keyState) {}
    virtual void onMouseWheel(int delta) {}
    virtual void onKeyDown(WPARAM key) {}
    // Alt 组合键走的是 WM_SYSKEYDOWN 而不是 WM_KEYDOWN。返回 true 表示自己收下了，
    // 返回 false 交还系统——Alt+F4、Alt+Space 这些系统快捷键必须照常生效。
    virtual bool onSysKeyDown(WPARAM key) { return false; }
    virtual void onChar(WPARAM character) {}
    virtual void onClose() {
        if (m_hwnd)
            DestroyWindow(m_hwnd);
    }

    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        MatWindow* pThis = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            pThis = reinterpret_cast<MatWindow*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        else {
            pThis = reinterpret_cast<MatWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (pThis)
            return pThis->wndProc(msg, wParam, lParam);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // DPI 变化后的钩子。画布按物理像素绘制的子类必须在这里按新 DPI 重建画布，
    // 否则窗口尺寸变了而画布没变，内容会填不满客户区或被裁掉。
    virtual void onDpiChanged() {}

    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            onPaint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            m_x = toLogical(GET_X_LPARAM(lParam));
            m_y = toLogical(GET_Y_LPARAM(lParam));
            onLButtonDown();
            return 0;

        case WM_LBUTTONUP:
            m_x = toLogical(GET_X_LPARAM(lParam));
            m_y = toLogical(GET_Y_LPARAM(lParam));
            onLButtonUp();
            return 0;

        case WM_RBUTTONUP:
            onRButtonUp();
            return 0;

        case WM_MOUSEMOVE:
            m_x = toLogical(GET_X_LPARAM(lParam));
            m_y = toLogical(GET_Y_LPARAM(lParam));
            onMouseMove(wParam);
            return 0;

        case WM_DPICHANGED: {
            // 拖到不同缩放的显示器上时按建议矩形重设，画布尺寸不变、拉伸比例随之改变。
            const int updatedDpi = static_cast<int>(HIWORD(wParam));
            const int fittedDpi = updatedDpi > 0 ? dpiFittedToWorkArea(updatedDpi) : m_dpi;
            if (updatedDpi > 0)
                m_dpi = fittedDpi;
            // 建议矩形是按未压缩的 DPI 算的；一旦因为工作区放不下而压了缩放比，就得
            // 自己算尺寸，否则窗口仍会超出屏幕。
            applyDpiScaledSize(fittedDpi == updatedDpi ?
                reinterpret_cast<const RECT*>(lParam) : nullptr);
            // 窗口尺寸已按新 DPI 变了，画布必须跟着重建，否则内容填不满客户区。
            onDpiChanged();
            isNeedRefreshUI = true;
            invalidate();
            return 0;
        }

        case WM_MOUSEWHEEL:
            onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_KEYDOWN:
            onKeyDown(wParam);
            return 0;

        case WM_SYSKEYDOWN:
            if (onSysKeyDown(wParam))
                return 0;
            break;

        case WM_CHAR:
            onChar(wParam);
            return 0;

        case WM_CLOSE:
            onClose();
            return 0;

        case WM_DESTROY:
            m_hwnd = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_MATWINDOW_DRAW_REQUEST:
            isNeedRefreshUI = true;
            return 0;

        case WM_MATWINDOW_DRAW_DONE:
            if (isDrawDone) {
                isDrawDone = false;
                invalidate();
            }
            return 0;
        }

        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }

public:
    static constexpr UINT WM_MATWINDOW_DRAW_REQUEST = WM_APP + 1;
    static constexpr UINT WM_MATWINDOW_DRAW_DONE = WM_APP + 2;
    volatile bool requestExitFlag = false;
    volatile bool isNeedRefreshUI = true;
    volatile bool isDrawThreadRuning = true;
    volatile bool isDrawDone = false;

    virtual ~MatWindow() {
        m_hwnd = nullptr;
    }

    HWND getHwnd() const { return m_hwnd; }
};
