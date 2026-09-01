#include "D3D11App.h"
#include "BackgroundPolicy.h"
#include "MonitorPlacement.h"
#include "FramePacingPolicy.h"

namespace {

enum class PreferredAppMode {
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3,
    Max = 4
};

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
using FlushMenuThemesFn = void (WINAPI*)();

struct MenuThemeApi {
    SetPreferredAppModeFn setPreferredAppMode = nullptr;
    FlushMenuThemesFn flushMenuThemes = nullptr;
};

const MenuThemeApi& GetMenuThemeApi() {
    static const MenuThemeApi api = []() {
        MenuThemeApi result;
        HMODULE module = GetModuleHandleW(L"uxtheme.dll");
        if (!module) {
            module = LoadLibraryW(L"uxtheme.dll");
        }
        if (!module) {
            return result;
        }

        result.setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(module, MAKEINTRESOURCEA(135)));
        result.flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(module, MAKEINTRESOURCEA(136)));
        return result;
    }();
    return api;
}

class PopupMenuThemeScope {
public:
    explicit PopupMenuThemeScope(bool useDarkTheme) {
        const auto& api = GetMenuThemeApi();
        if (!api.setPreferredAppMode || !api.flushMenuThemes) {
            return;
        }

        setPreferredAppMode_ = api.setPreferredAppMode;
        flushMenuThemes_ = api.flushMenuThemes;
        previousMode_ = setPreferredAppMode_(useDarkTheme ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
        flushMenuThemes_();
        isActive_ = true;
    }

    ~PopupMenuThemeScope() {
        if (!isActive_) {
            return;
        }

        setPreferredAppMode_(previousMode_);
        flushMenuThemes_();
    }

private:
    bool isActive_ = false;
    PreferredAppMode previousMode_ = PreferredAppMode::Default;
    SetPreferredAppModeFn setPreferredAppMode_ = nullptr;
    FlushMenuThemesFn flushMenuThemes_ = nullptr;
};

struct EnumeratedMonitor {
    HMONITOR handle = nullptr;
    MONITORINFOEXW info{};

    EnumeratedMonitor() {
        info.cbSize = sizeof(MONITORINFOEXW);
    }
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& monitors = *reinterpret_cast<std::vector<EnumeratedMonitor>*>(data);
    EnumeratedMonitor item;
    item.handle = monitor;
    if (GetMonitorInfoW(monitor, &item.info))
        monitors.emplace_back(std::move(item));
    return TRUE;
}

std::vector<EnumeratedMonitor> enumerateMonitors() {
    std::vector<EnumeratedMonitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

bool forcePrimaryMonitorForRegression() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(
        L"YEIMAGEVIEWER_TEST_PRIMARY_MONITOR", value, ARRAYSIZE(value)) > 0 &&
        value[0] == L'1';
}

MonitorPlacement::Rect toPlacementRect(const RECT& rect) {
    return { rect.left, rect.top, rect.right, rect.bottom };
}

RECT toWin32Rect(const MonitorPlacement::Rect& rect) {
    return { rect.left, rect.top, rect.right, rect.bottom };
}

}

D3D11App::D3D11App(bool openImageOnCursorMonitor) {
    loadSettings(openImageOnCursorMonitor);

    GlobalVar::isSystemDarkMode = jarkUtils::getSystemDarkMode();
    GlobalVar::isCurrentUIDarkMode = GlobalVar::settingParameter.UI_Mode == 0 ? GlobalVar::isSystemDarkMode : (GlobalVar::settingParameter.UI_Mode == 2);
    GlobalVar::currentTheme = GlobalVar::isCurrentUIDarkMode ? deepTheme : lightTheme;
}

D3D11App::~D3D11App() {
    this->DiscardDeviceResources();
}

template<class Interface>
void D3D11App::SafeRelease(Interface*& pInterfaceToRelease) {
    if (pInterfaceToRelease == nullptr)
        return;

    pInterfaceToRelease->Release();
    pInterfaceToRelease = nullptr;
}

void D3D11App::loadSettings(bool openImageOnCursorMonitor) {
    auto exePath = jarkUtils::getCurrentAppPath();
    size_t lastSlash = exePath.find_last_of(L'\\');
    if (lastSlash == std::wstring::npos) {
        GlobalVar::settingPath = L"YeImageViewer.db";
    }
    else {
        GlobalVar::settingPath = exePath.substr(0, lastSlash) + L"\\YeImageViewer.db";
    }
    GlobalVar::externalEditorsPath =
        ExternalEditorConfig::configPath(GlobalVar::settingPath);
    GlobalVar::externalEditors =
        ExternalEditorConfig::load(GlobalVar::externalEditorsPath);

    PWSTR appDataPath = nullptr;
    std::wstring oldSettingPath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appDataPath))) {
        oldSettingPath = std::wstring(appDataPath) + L"\\YeImageViewer.db";
        CoTaskMemFree(appDataPath);
        appDataPath = nullptr;
    }

    SettingParameter tmp;
    bool loaded = false;

    auto f = _wfopen(oldSettingPath.c_str(), L"rb");
    if (f) {
        auto readLen = fread(&tmp, 1, sizeof(SettingParameter), f);
        fclose(f);

        if (readLen == sizeof(SettingParameter) && !memcmp(GlobalVar::settingHeader.data(), tmp.header, GlobalVar::settingHeader.length())) {
            GlobalVar::settingParameter = tmp;
            loaded = true;
            DeleteFileW(oldSettingPath.c_str());
        }
    }

    if (!loaded) {
        f = _wfopen(GlobalVar::settingPath.c_str(), L"rb");
        if (f) {
            auto readLen = fread(&tmp, 1, sizeof(SettingParameter), f);
            fclose(f);

            if (readLen == sizeof(SettingParameter) && !memcmp(GlobalVar::settingHeader.data(), tmp.header, GlobalVar::settingHeader.length()))
                GlobalVar::settingParameter = tmp;
        }
    }

    const auto enumerated = enumerateMonitors();
    std::vector<MonitorPlacement::Monitor> monitors;
    monitors.reserve(enumerated.size());
    for (const auto& item : enumerated) {
        monitors.push_back({ item.info.szDevice, toPlacementRect(item.info.rcWork),
            (item.info.dwFlags & MONITORINFOF_PRIMARY) != 0 });
    }

    size_t cursorMonitorIndex = MonitorPlacement::NO_MONITOR;
    if (openImageOnCursorMonitor) {
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            const HMONITOR cursorMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
            for (size_t index = 0; index < enumerated.size(); ++index) {
                if (enumerated[index].handle == cursorMonitor) {
                    cursorMonitorIndex = index;
                    break;
                }
            }
        }
    }

    const auto selection = forcePrimaryMonitorForRegression() ?
        MonitorPlacement::select(monitors, false, {}) :
        MonitorPlacement::selectForImageOpen(monitors, cursorMonitorIndex,
            GlobalVar::settingParameter.rememberLastMonitor,
            GlobalVar::settingParameter.lastMonitorDevice);
    if (selection.index != MonitorPlacement::NO_MONITOR) {
        auto relativeRect = toPlacementRect(GlobalVar::settingParameter.monitorRelativeRect);
        if (!relativeRect.valid() && toPlacementRect(GlobalVar::settingParameter.rect).valid()) {
            relativeRect = MonitorPlacement::toRelative(
                toPlacementRect(GlobalVar::settingParameter.rect), monitors[selection.index].workArea);
        }
        GlobalVar::settingParameter.rect = toWin32Rect(
            MonitorPlacement::restore(relativeRect, monitors[selection.index].workArea));
    }
}

void D3D11App::saveSettings() const {
    WINDOWPLACEMENT wp{ .length = sizeof(WINDOWPLACEMENT) };

    if (GetWindowPlacement(m_hWnd, &wp)) {
        GlobalVar::settingParameter.showCmd = wp.showCmd == SW_MAXIMIZE ? SW_MAXIMIZE : SW_NORMAL;
        GlobalVar::settingParameter.rect = wp.rcNormalPosition;

        HMONITOR monitor = MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFOEXW);
        if (GetMonitorInfoW(monitor, &monitorInfo)) {
            GlobalVar::settingParameter.monitorRelativeRect = toWin32Rect(
                MonitorPlacement::toRelative(toPlacementRect(wp.rcNormalPosition),
                    toPlacementRect(monitorInfo.rcWork)));
            if (GlobalVar::settingParameter.rememberLastMonitor) {
                wcsncpy_s(GlobalVar::settingParameter.lastMonitorDevice,
                    monitorInfo.szDevice, _TRUNCATE);
            }
            else {
                GlobalVar::settingParameter.lastMonitorDevice[0] = L'\0';
            }
        }
    }

    memcpy(GlobalVar::settingParameter.header, GlobalVar::settingHeader.data(), GlobalVar::settingHeader.length());

    auto f = _wfopen(GlobalVar::settingPath.c_str(), L"wb");
    if (f) {
        fwrite(&GlobalVar::settingParameter, 1, sizeof(SettingParameter), f);
        fclose(f);
    }
}

HRESULT D3D11App::Initialize(HINSTANCE hInstance) {
    HRESULT hr = E_FAIL;
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = D3D11App::WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(void*);
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"D3D11WndClass";
    wcex.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDI_YEIMAGEVIEWER));
    RegisterClassExW(&wcex);

    RECT window_rect = GlobalVar::settingParameter.rect;
    if (window_rect.right <= window_rect.left || window_rect.bottom <= window_rect.top)
        window_rect = { 0, 0, 800, 600 };
    DWORD window_style = WS_OVERLAPPEDWINDOW;
    m_hWnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"D3D11WndClass", m_wndCaption.c_str(), window_style,
        window_rect.left, window_rect.top, window_rect.right - window_rect.left, window_rect.bottom - window_rect.top,
        0, 0, hInstance, this);
    hr = m_hWnd ? S_OK : E_FAIL;

    if (SUCCEEDED(hr)) {
        // 主窗口只响应单键快捷键，不需要输入法：把它从这个窗口摘掉，按键原样送达。
        // 不能用 ImmDisableIME —— 那是线程级且不可撤销的开关，会让重命名框也
        // 打不出中文。按窗口摘除只影响这一个窗口。
        ::ImmAssociateContext(m_hWnd, nullptr);

        CreateDeviceResources();
        ApplyWindowBackgroundMode();

        BOOL themeMode = GlobalVar::isCurrentUIDarkMode;
        DwmSetWindowAttribute(m_hWnd, 20, &themeMode, sizeof(BOOL));
        DragAcceptFiles(m_hWnd, TRUE);
    }
    return hr;
}

void D3D11App::ShowInitialWindow() {
    if (!m_hWnd)
        return;
    ShowWindow(m_hWnd, SW_NORMAL);
    UpdateWindow(m_hWnd);
}

void D3D11App::ApplyWindowBackgroundMode() {
    if (!m_hWnd) {
        m_isFrostedGlassActive = false;
        return;
    }

    // The client uses a premultiplied-alpha DirectComposition surface in both
    // framed and presentation modes. Keep DWM backdrop effects disabled: the
    // canvas itself supplies the uniform translucent black layer.
    const BOOL useAlpha = FALSE;
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;

    const HRESULT alphaResult = DwmSetWindowAttribute(
        m_hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
    const HRESULT backdropResult = DwmSetWindowAttribute(
        m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    m_isFrostedGlassActive = false;
    (void)alphaResult;
    (void)backdropResult;
    DwmFlush();
}

void D3D11App::SetPresentationBackdrop(bool enabled) {
    if (m_presentationBackdropRequested == enabled)
        return;
    m_presentationBackdropRequested = enabled;
    ApplyWindowBackgroundMode();
}

HRESULT D3D11App::CreateDeviceResources() {
    HRESULT hr = S_OK;

    // 创建 D3D11 设备
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_pD3DDevice,
        &m_featureLevel,
        &m_pD3DDeviceContext);

    // 获取 DXGI 工厂（通过设备链：Device → DXGIDevice → Adapter → Factory）
    IDXGIDevice* pDxgiDevice = nullptr;
    IDXGIAdapter* pDxgiAdapter = nullptr;
    IDXGIFactory* pDxgiFactory = nullptr;

    if (SUCCEEDED(hr))
        hr = m_pD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDxgiDevice);
    if (SUCCEEDED(hr))
        hr = pDxgiDevice->GetAdapter(&pDxgiAdapter);
    if (SUCCEEDED(hr))
        hr = pDxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pDxgiFactory);

    // Windows 10/11: use a windowless flip-model swap chain so the alpha
    // channel of the CPU canvas is composed into the HWND client area. The
    // system non-client frame remains independent and fully interactive.
    if (SUCCEEDED(hr)) {
        RECT rect{};
        GetClientRect(m_hWnd, &rect);

        IDXGIFactory2* factory2 = nullptr;
        hr = pDxgiFactory->QueryInterface(IID_PPV_ARGS(&factory2));
        if (SUCCEEDED(hr)) {
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
            swapChainDesc.Width = rect.right - rect.left;
            swapChainDesc.Height = rect.bottom - rect.top;
            swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.BufferCount = 2;
            swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

            IDXGISwapChain1* compositionSwapChain = nullptr;
            hr = factory2->CreateSwapChainForComposition(
                m_pD3DDevice, &swapChainDesc, nullptr, &compositionSwapChain);
            if (SUCCEEDED(hr)) {
                m_pSwapChain = compositionSwapChain;
                m_swapChainBufferCount = 2;
                hr = DCompositionCreateDevice(
                    pDxgiDevice, IID_PPV_ARGS(&m_pCompositionDevice));
            }
            if (SUCCEEDED(hr))
                hr = m_pCompositionDevice->CreateTargetForHwnd(
                    m_hWnd, TRUE, &m_pCompositionTarget);
            if (SUCCEEDED(hr))
                hr = m_pCompositionDevice->CreateVisual(&m_pCompositionVisual);
            if (SUCCEEDED(hr))
                hr = m_pCompositionVisual->SetContent(m_pSwapChain);
            if (SUCCEEDED(hr))
                hr = m_pCompositionTarget->SetRoot(m_pCompositionVisual);
            if (SUCCEEDED(hr))
                hr = m_pCompositionDevice->Commit();
            if (SUCCEEDED(hr))
                m_compositionAlphaActive = true;
        }
        SafeRelease(factory2);

        if (FAILED(hr)) {
            SafeRelease(m_pCompositionVisual);
            SafeRelease(m_pCompositionTarget);
            SafeRelease(m_pCompositionDevice);
            SafeRelease(m_pSwapChain);
            m_compositionAlphaActive = false;
            m_swapChainBufferCount = 1;
            SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE,
                GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) &
                ~static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP));

            DXGI_SWAP_CHAIN_DESC fallbackDesc{};
            fallbackDesc.BufferDesc.Width = rect.right - rect.left;
            fallbackDesc.BufferDesc.Height = rect.bottom - rect.top;
            fallbackDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            fallbackDesc.SampleDesc.Count = 1;
            fallbackDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            fallbackDesc.BufferCount = 1;
            fallbackDesc.OutputWindow = m_hWnd;
            fallbackDesc.Windowed = TRUE;
            fallbackDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = pDxgiFactory->CreateSwapChain(
                m_pD3DDevice, &fallbackDesc, &m_pSwapChain);
        }
    }

    SafeRelease(pDxgiDevice);
    SafeRelease(pDxgiAdapter);
    SafeRelease(pDxgiFactory);

    if (SUCCEEDED(hr))
        CreateWindowSizeDependentResources();

    return hr;
}

void D3D11App::CreateWindowSizeDependentResources() {
    if (!m_pD3DDevice || !m_pSwapChain)
        return;

    // 释放旧暂存纹理
    for (auto& staging : m_pStagingTextures)
        SafeRelease(staging);
    m_stagingIndex = 0;
    m_pD3DDeviceContext->Flush();

    RECT rect = { 0 };
    GetClientRect(m_hWnd, &rect);
    UINT width = rect.right - rect.left;
    UINT height = rect.bottom - rect.top;
    if (width == 0 || height == 0)
        return;

    // 重设交换链缓冲区
    HRESULT hr = m_pSwapChain->ResizeBuffers(
        m_swapChainBufferCount,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        0);
    assert(hr == S_OK);

    // 创建 CPU 可写暂存纹理
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texDesc.MiscFlags = 0;

    for (auto& staging : m_pStagingTextures) {
        hr = m_pD3DDevice->CreateTexture2D(&texDesc, nullptr, &staging);
        assert(hr == S_OK);
        if (FAILED(hr)) {
            for (auto& created : m_pStagingTextures)
                SafeRelease(created);
            return;
        }
    }

    m_stagingWidth = width;
    m_stagingHeight = height;
}

void D3D11App::PresentCanvas(const uint8_t* data, int width, int height, int stride) {
    if (!m_pStagingTextures[0] || !m_pSwapChain || !m_pD3DDeviceContext)
        return;

    // 尺寸不匹配时重建
    if ((UINT)width != m_stagingWidth || (UINT)height != m_stagingHeight) {
        CreateWindowSizeDependentResources();
        if (!m_pStagingTextures[0])
            return;
    }

    // 轮换暂存纹理：本帧写入的这张，GPU 上一次读的是另一张，避免 Map 等待
    ID3D11Texture2D* staging = m_pStagingTextures[m_stagingIndex];
    m_stagingIndex = (m_stagingIndex + 1) % STAGING_TEXTURE_COUNT;

    // Map 暂存纹理，写入 CPU 画布数据
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_pD3DDeviceContext->Map(staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (SUCCEEDED(hr)) {
        const int rowBytes = width * 4;
        if ((int)mapped.RowPitch == stride) {
            memcpy(mapped.pData, data, (size_t)rowBytes * height);
        } else {
            const uint8_t* src = data;
            uint8_t* dst = (uint8_t*)mapped.pData;
            for (int y = 0; y < height; y++) {
                memcpy(dst, src, rowBytes);
                src += stride;
                dst += mapped.RowPitch;
            }
        }
        m_pD3DDeviceContext->Unmap(staging, 0);

        // 将暂存纹理复制到交换链后缓冲
        ID3D11Texture2D* pBackBuffer = nullptr;
        hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (SUCCEEDED(hr)) {
            m_pD3DDeviceContext->CopyResource(pBackBuffer, staging);
            pBackBuffer->Release();
        }
    }

    m_pSwapChain->Present(FramePacingPolicy::PRESENT_SYNC_INTERVAL, 0);
}

void D3D11App::DiscardDeviceResources() {
    for (auto& staging : m_pStagingTextures)
        SafeRelease(staging);
    SafeRelease(m_pCompositionVisual);
    SafeRelease(m_pCompositionTarget);
    SafeRelease(m_pCompositionDevice);
    SafeRelease(m_pSwapChain);
    SafeRelease(m_pD3DDevice);
    SafeRelease(m_pD3DDeviceContext);
}

void D3D11App::Run() {
    while (m_fRunning) {
        MSG msg;
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
            DrawScene();
        }
    }
}

void D3D11App::OnDestroy() {
    saveSettings();
    m_fRunning = FALSE;
}


LRESULT D3D11App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
        D3D11App* pApp = (D3D11App*)pcs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pApp);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 300;
        return S_OK;
    }
    case WM_CONTEXTMENU: {
        if (lParam == -1) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int x = (rc.right - rc.left) / 2;
            int y = (rc.bottom - rc.top) / 2;
            ShowContextMenu(hwnd, x, y);
        }
        else {
            ShowContextMenu(hwnd, LOWORD(lParam), HIWORD(lParam));
        }
        return S_OK;
    }
    }

    static TRACKMOUSEEVENT tme = {
        .cbSize = sizeof(TRACKMOUSEEVENT),
        .dwFlags = TME_LEAVE,
    };

    D3D11App* pApp = reinterpret_cast<D3D11App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!pApp)
        return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message)
    {
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MAXIMIZE) {
            pApp->OnMaximizeRequested();
            return S_OK;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_XBUTTONDOWN:
        pApp->OnMouseDown(message, LOWORD(lParam), HIWORD(lParam), wParam);
        return S_OK;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
        pApp->OnMouseUp(message, LOWORD(lParam), HIWORD(lParam), wParam);
        return S_OK;

    case WM_MOUSEMOVE:
        if (!tme.hwndTrack) {
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        pApp->OnMouseMove(message, LOWORD(lParam), HIWORD(lParam));
        return S_OK;

    case WM_MOUSELEAVE:
        tme.hwndTrack = NULL;
        pApp->OnMouseLeave();
        break;

    case WM_MOUSEWHEEL:
        pApp->OnMouseWheel(LOWORD(wParam), HIWORD(wParam), LOWORD(lParam), HIWORD(lParam));
        return S_OK;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        pApp->OnKeyDown(wParam);
        return S_OK;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        pApp->OnKeyUp(wParam);
        return S_OK;

    case WM_DROPFILES:
        pApp->OnDropFiles(wParam);
        break;

    case WM_COMMAND:
        pApp->OnContextMenuCommand(wParam);
        break;

    case WM_SIZE:
        pApp->OnResize(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_SETTINGCHANGE:
        GlobalVar::isSystemDarkMode = jarkUtils::getSystemDarkMode();
        GlobalVar::isCurrentUIDarkMode = GlobalVar::settingParameter.UI_Mode == 0 ? GlobalVar::isSystemDarkMode : (GlobalVar::settingParameter.UI_Mode == 2);
        GlobalVar::isNeedUpdateTheme = true;
        break;

    case WM_DESTROY:
    {
        pApp->OnRequestExitOtherWindows();
        pApp->OnDestroy();
        PostQuitMessage(0);
        return S_OK;
    }
    break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}


HMENU D3D11App::CreateContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    MENUINFO mi = { sizeof(MENUINFO) };
    mi.fMask = MIM_STYLE;
    mi.dwStyle = MNS_NOCHECK;
    SetMenuInfo(hMenu, &mi);

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::openNewImage, getUIStringW(35));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::copyImageInfo, getUIStringW(25));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::copyImagePath, getUIStringW(26));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::copyImageData, getUIStringW(27));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::toggleExifDisplay, getUIStringW(28));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::openContainerFloder, getUIStringW(29));

    HMENU editMenu = CreatePopupMenu();
    for (std::size_t index = 0; index < GlobalVar::externalEditors.size(); ++index) {
        const auto& editor = GlobalVar::externalEditors[index];
        std::error_code editorError;
        const bool editorAvailable =
            std::filesystem::is_regular_file(editor.path, editorError);
        const std::wstring editorText = ExternalEditorConfig::menuLabel(editor,
            GlobalVar::settingParameter.UI_LANG == 0);
        AppendMenuW(editMenu,
            MF_STRING | (editorAvailable ? MF_ENABLED : MF_GRAYED),
            static_cast<UINT_PTR>(ContextMenu::editImageFirst) + index,
            editorText.c_str());
    }
    if (!GlobalVar::externalEditors.empty())
        AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, (UINT_PTR)ContextMenu::editImageChoose,
        getUIStringW(59));
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)editMenu, getUIStringW(57));

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::renameImage, getUIStringW(47));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::deleteImage, getUIStringW(30));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::openFileProperties, getUIStringW(36));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::printImage, getUIStringW(31));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    HMENU backgroundMenu = CreatePopupMenu();
    AppendMenuW(backgroundMenu, MF_STRING, (UINT_PTR)ContextMenu::backgroundTransparent, getUIStringW(43));
    AppendMenuW(backgroundMenu, MF_STRING, (UINT_PTR)ContextMenu::backgroundWhite, getUIStringW(44));
    AppendMenuW(backgroundMenu, MF_STRING, (UINT_PTR)ContextMenu::backgroundBlack, getUIStringW(45));
    AppendMenuW(backgroundMenu, MF_STRING, (UINT_PTR)ContextMenu::backgroundFrostedGlass, getUIStringW(46));
    const UINT selectedBackground = (UINT)ContextMenu::backgroundTransparent +
        (UINT)BackgroundRenderer::normalizeMode(GlobalVar::settingParameter.backgroundMode);
    CheckMenuRadioItem(backgroundMenu,
        (UINT)ContextMenu::backgroundTransparent,
        (UINT)ContextMenu::backgroundFrostedGlass,
        selectedBackground,
        MF_BYCOMMAND);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)backgroundMenu, getUIStringW(42));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::toggleFullScreen, getUIStringW(38));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::openSetting, getUIStringW(32));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::openHelp, getUIStringW(37));
    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::aboutSoftware, getUIStringW(33));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, (UINT_PTR)ContextMenu::exitSoftware, getUIStringW(34));

    return hMenu;
}

void D3D11App::ShowContextMenu(HWND hwnd, int x, int y) {
    HMENU hMenu = CreateContextMenu(hwnd);
    POINT pt = { x, y };
    ClientToScreen(hwnd, &pt);

    PopupMenuThemeScope popupMenuThemeScope(GlobalVar::isCurrentUIDarkMode);
    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    DWORD cmd = TrackPopupMenuEx(hMenu, flags, pt.x, pt.y, hwnd, NULL);

    if (cmd)
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);

    DestroyMenu(hMenu);
}
