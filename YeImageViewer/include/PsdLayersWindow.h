#pragma once
#include "ColorManager.h"
#include "PsdLayers.h"
#include <atomic>
#include <thread>

class PsdLayersWindow
{
    inline static std::atomic<bool> working{false};
    HWND window = nullptr, list = nullptr, status = nullptr;
    HFONT font = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    const uint8_t *view = nullptr;
    PsdLayers::Document document;
    PsdCompositeDecoder::Image image;
    int dpi = 96;
    RECT previewRect{};
    std::wstring path;
    bool testMode = false, testResult = false;
    int s(int value) const
    {
        return MulDiv(value, dpi, 96);
    }
    void layout()
    {
        RECT client{};
        GetClientRect(window, &client);
        const int listWidth = std::min(s(240), std::max(s(120), int(client.right) / 3));
        MoveWindow(list, s(8), s(8), listWidth, std::max(1, int(client.bottom) - s(16)), TRUE);
        MoveWindow(status, listWidth + s(24), s(8), std::max(1, int(client.right) - listWidth - s(32)), s(82),
                   TRUE);
        previewRect = {listWidth + s(24), s(98), client.right - s(8), client.bottom - s(8)};
        InvalidateRect(window, nullptr, FALSE);
    }
    void select()
    {
        const auto index = SendMessageW(list, LB_GETCURSEL, 0, 0);
        image = {};
        if (index == LB_ERR || static_cast<size_t>(index) >= document.layers.size())
            return;
        const auto &layer = document.layers[index];
        image = PsdLayers::preview(document, static_cast<size_t>(index));
        std::wstring message = layer.name + L" · " + std::to_wstring(int64_t(layer.right) - layer.left) + L" × " +
                               std::to_wstring(int64_t(layer.bottom) - layer.top) + L" · " +
                               std::to_wstring(layer.opacity * 100 / 255) + L"%";
        message +=
            layer.visible ? trW(L" · 可见", L" · visible", L" · 可見") : trW(L" · 隐藏", L" · hidden", L" · 隱藏");
        message += L"\r\n";
        message += image.bgra.empty() ? trW(L"无可预览的栅格像素（组、非栅格或超出限制）",
                                            L"No raster preview (group, non-raster, or size limit)",
                                            L"無可預覽的點陣像素（群組、非點陣或超出限制）")
                                      : trW(L"单层原始栅格 · 不重建蒙版、混合和效果",
                                            L"Raw layer raster · masks, blending and effects not rebuilt",
                                            L"單層原始點陣 · 不重建遮色片、混合和效果");
        SetWindowTextW(status, message.c_str());
        InvalidateRect(window, nullptr, FALSE);
        if (!image.bgra.empty())
        {
            cv::Mat mat(image.height, image.width, CV_8UC4, image.bgra.data());
            ColorManager::applyToMat(mat, image.icc, PsdCompositeDecoder::srgbProfile());
        }
    }
    void paint()
    {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        if (!image.bgra.empty() && previewRect.right > previewRect.left && previewRect.bottom > previewRect.top)
        {
            const int availW = previewRect.right - previewRect.left, availH = previewRect.bottom - previewRect.top;
            const double scale = std::min({1.0, double(availW) / image.width, double(availH) / image.height});
            const int w = std::max(1, int(image.width * scale)), h = std::max(1, int(image.height * scale));
            cv::Mat source(image.height, image.width, CV_8UC4, image.bgra.data()), canvas;
            cv::resize(source, canvas, {w, h}, 0, 0, cv::INTER_AREA);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto &pixel = canvas.at<cv::Vec4b>(y, x);
                    const int background = ((x / s(10) + y / s(10)) % 2) ? 210 : 245;
                    for (int c = 0; c < 3; ++c)
                        pixel[c] = static_cast<uint8_t>(
                            (pixel[c] * pixel[3] + background * (255 - pixel[3]) + 127) / 255);
                    pixel[3] = 255;
                }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            StretchDIBits(dc, previewRect.left + (availW - w) / 2, previewRect.top + (availH - h) / 2, w, h, 0, 0,
                          w, h, canvas.data, &info, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(window, &ps);
    }
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM key, LPARAM param)
    {
        auto *self = reinterpret_cast<PsdLayersWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<PsdLayersWindow *>(reinterpret_cast<CREATESTRUCTW *>(param)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(hwnd, message, key, param);
        if (message == WM_CREATE)
        {
            self->dpi = GetDpiForWindow(hwnd);
            self->font = CreateFontW(-self->s(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, L"Segoe UI");
            self->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                         WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
                                             LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                         0, 0, 1, 1, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
            self->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr,
                                           nullptr, nullptr);
            SendMessageW(self->list, WM_SETFONT, reinterpret_cast<WPARAM>(self->font), TRUE);
            SendMessageW(self->status, WM_SETFONT, reinterpret_cast<WPARAM>(self->font), TRUE);
            for (const auto &layer : self->document.layers)
            {
                std::wstring label = (layer.group ? L"▸ " : layer.visible ? L"● " : L"○ ") + layer.name;
                SendMessageW(self->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(self->list, LB_SETHORIZONTALEXTENT, self->s(800), 0);
            if (!self->document.layers.empty())
            {
                SendMessageW(self->list, LB_SETCURSEL, 0, 0);
                self->select();
            }
            else
                SetWindowTextW(self->status, trW(L"没有可读取的图层（仅保存了合成图或格式不支持）",
                                                 L"No readable layers (flattened or unsupported document)",
                                                 L"沒有可讀取的圖層（僅儲存了合成圖或格式不支援）"));
            self->layout();
            return 0;
        }
        if (message == WM_COMMAND && HIWORD(key) == LBN_SELCHANGE)
        {
            try
            {
                self->select();
            }
            catch (...)
            {
                self->image = {};
            }
            return 0;
        }
        if (message == WM_SIZE)
        {
            self->layout();
            return 0;
        }
        if (message == WM_DPICHANGED)
        {
            self->dpi = HIWORD(key);
            if (self->font)
                DeleteObject(self->font);
            self->font = CreateFontW(-self->s(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                     CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SendMessageW(self->list, WM_SETFONT, reinterpret_cast<WPARAM>(self->font), TRUE);
            SendMessageW(self->status, WM_SETFONT, reinterpret_cast<WPARAM>(self->font), TRUE);
            auto *r = reinterpret_cast<RECT *>(param);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
            self->layout();
            return 0;
        }
        if (message == WM_PAINT)
        {
            try
            {
                self->paint();
            }
            catch (...)
            {
            }
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, key, param);
    }
    void run()
    {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
            uint64_t(size.QuadPart) <= (4ULL << 30))
        {
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping)
                view = static_cast<const uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
            if (view)
                document = PsdLayers::read({view, static_cast<size_t>(size.QuadPart)});
        }
        if (!document.valid)
        {
            MessageBoxW(nullptr,
                        trW(L"无法读取 PSD/PSB 图层索引，原图未作任何修改。",
                            L"Cannot read PSD/PSB layers. The source was not modified.",
                            L"無法讀取 PSD/PSB 圖層索引，原圖未作任何修改。"),
                        L"YeImageViewer", MB_OK | MB_ICONINFORMATION);
            return;
        }
        WNDCLASSW cls{};
        cls.lpfnWndProc = proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"YeImageViewer.PsdLayers";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        dpi = GetDpiForSystem();
        window =
            CreateWindowExW(0, cls.lpszClassName,
                            trW(L"PSD 图层预览 · 只读", L"PSD layer preview · read-only", L"PSD 圖層預覽 · 唯讀"),
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, s(820), s(560),
                            nullptr, nullptr, cls.hInstance, this);
        if (window)
        {
            if (testMode)
            {
                testResult =
                    SendMessageW(list, LB_GETCOUNT, 0, 0) == static_cast<LRESULT>(document.layers.size()) &&
                    !image.bgra.empty();
                if (document.layers.size() > 1)
                {
                    const auto first = image.bgra;
                    SendMessageW(list, LB_SETCURSEL, 1, 0);
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(101, LBN_SELCHANGE),
                                 reinterpret_cast<LPARAM>(list));
                    testResult = testResult && !image.bgra.empty() && image.bgra != first;
                }
                RECT client{}, r{};
                GetClientRect(window, &client);
                GetWindowRect(status, &r);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
                testResult = testResult && r.right <= client.right && r.bottom <= client.bottom;
                DestroyWindow(window);
            }
            else
            {
                ShowWindow(window, SW_SHOW);
                MSG msg{};
                while (GetMessageW(&msg, nullptr, 0, 0) > 0)
                {
                    if (!IsDialogMessageW(window, &msg))
                    {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }
            }
        }
    }

  public:
    static bool selfTest(std::wstring source)
    {
        PsdLayersWindow panel;
        panel.path = std::move(source);
        panel.testMode = true;
        panel.run();
        return panel.testResult;
    }
    ~PsdLayersWindow()
    {
        if (view)
            UnmapViewOfFile(view);
        if (mapping)
            CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        if (font)
            DeleteObject(font);
    }
    static void open(std::wstring path)
    {
        if (working.exchange(true))
            return;
        try
        {
            std::thread([path = std::move(path)] {
                try
                {
                    PsdLayersWindow panel;
                    panel.path = path;
                    panel.run();
                }
                catch (...)
                {
                }
                working = false;
            }).detach();
        }
        catch (...)
        {
            working = false;
        }
    }
};
