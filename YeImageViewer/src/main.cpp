#include "jarkUtils.h"

#include "TextDrawer.h"
#include "ImageDatabase.h"
#include "ImageInterpolation.h"
#include "ImageViewTransform.h"
#include "SvgRenderer.h"
#include "Printer.h"
#include "Setting.h"
#include "RotationStore.h"
#include "InitialWindowLayout.h"
#include "PresentationLayout.h"
#include "BackgroundPolicy.h"
#include "ImageInfoPresentation.h"
#include "WindowTitlePresentation.h"
#include "WheelInput.h"
#include "RenamePolicy.h"
#include "SlideshowPolicy.h"
#include "ZoomPolicy.h"
#include "ZoomEditPolicy.h"
#include "ToolbarCommand.h"

#include "D3D11App.h"
#include <ppl.h>
#include <concrt.h>
#include <shellapi.h>
#include <optional>

#pragma comment(lib, "Shell32.lib")

/* TODO
1. 在鼠标光标位置缩放
1. 给系统提供缩略图缓存支持
1. 缩放策略加个线性插值
1. LunaSVG库支持度较差，考虑更换
1. 考虑加个按时间日期排序
1. 导出实况的视频
*/

std::wstring_view appName = L"YeImageViewer";
std::wstring_view appVersion = L"v1.36.27";
constinit int appVersionCode = 13627; // 主版本*10000 + 次版本*100 + 修订版本

std::wstring_view RepositoryLink = L"https://github.com/yakoye/YeImageViewer";

namespace {

constexpr wchar_t RENAME_WINDOW_CLASS[] = L"YeImageViewerRenameWnd";
constexpr int RENAME_EDIT_ID = 1001;

struct RenameDialogState {
    std::wstring initialName;
    std::wstring title;
    std::wstring prompt;
    std::optional<std::wstring> result;
    HWND window = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    bool finished = false;
};

int scaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

LRESULT CALLBACK RenameDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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
        if (GlobalVar::settingParameter.UI_LANG != 0) {
            SetDlgItemTextW(window, IDOK, L"OK");
            SetDlgItemTextW(window, IDCANCEL, L"Cancel");
        }
        SendMessageW(state->edit, EM_SETLIMITTEXT, 255, 0);
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

std::optional<std::wstring> showTextInputDialog(HWND owner, std::wstring initialName,
    std::wstring title, std::wstring prompt) {
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
        .title = std::move(title), .prompt = std::move(prompt) };
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

} // namespace


struct CurImageParameter {
    static constexpr int64_t ZOOM_BASE = (1 << 16); // 100%缩放
    inline static const auto ZOOM_LIST = ZoomPolicy::buildLevels(ZOOM_BASE);

    int64_t zoomTarget;     // 设定的缩放比例
    int64_t zoomCur;        // 动画播放过程的缩放比例，动画完毕后的值等于zoomTarget
    int curFrameIdx;        // 小于0则单张静态图像，否则为动画当前帧索引
    int curFrameIdxMax;     // 若是动画则为帧数量
    int curFrameDelay;      // 当前帧延迟
    Cood slideCur, slideTarget;
    std::shared_ptr<ImageAsset> imageAssetPtr;

    vector<int64_t> zoomList;
    int zoomIndex = 0;
    int zoomIndexFix = 0;
    int zoomIndex100percent = 0;
    bool isAnimationPause = false;
    bool flipHorizontal = false;
    bool flipVertical = false;
    int width = 0;
    int height = 0;
    int rotation = 0; // 旋转： 0正常， 1逆90度， 2：180度， 3顺90度

    CurImageParameter() {
        Init();
    }

    void Init(int winWidth = 0, int winHeight = 0, int initialRotation = 0, bool preventUpscale = false) {

        curFrameIdx = 0;
        curFrameDelay = 0;

        slideCur = 0;
        slideTarget = 0;
        rotation = initialRotation & 3;
        isAnimationPause = false;
        flipHorizontal = false;
        flipVertical = false;

        if (imageAssetPtr) {
            // 帧索引上界只取决于实际帧数：实况图播完会临时切成 Still，之后又会切回
            // Animated，若此处按 format 计算，切回后上界会残留错误值导致 frames 越界。
            curFrameIdxMax = imageAssetPtr->frames.empty() ?
                0 : (int)imageAssetPtr->frames.size() - 1;

            if (imageAssetPtr->format == ImageFormat::Animated && !imageAssetPtr->frames.empty()) {
                width = imageAssetPtr->frames[0].cols;
                height = imageAssetPtr->frames[0].rows;
            }
            else if (imageAssetPtr->svgRenderer) {
                width = std::max(1, (int)std::lround(imageAssetPtr->svgRenderer->width()));
                height = std::max(1, (int)std::lround(imageAssetPtr->svgRenderer->height()));
            }
            else {
                width = imageAssetPtr->primaryFrame.cols;
                height = imageAssetPtr->primaryFrame.rows;
            }

            //适应显示窗口宽高的缩放比例
            const int displayWidth = (rotation == 0 || rotation == 2) ? width : height;
            const int displayHeight = (rotation == 0 || rotation == 2) ? height : width;
            // 解码失败或空图时 displayWidth/displayHeight 可能为 0，直接相除会整数除零崩溃
            // 解码失败或空图时 displayWidth/displayHeight 可能为 0，直接相除会整数除零崩溃。
            // 仅在这一种情况下回落，其余路径保持原有取值，避免影响窗口尺寸未就绪时的缩放行为。
            int64_t zoomFitWindow = (displayWidth > 0 && displayHeight > 0) ?
                std::min(winWidth * ZOOM_BASE / displayWidth, winHeight * ZOOM_BASE / displayHeight) :
                ZOOM_BASE;
            zoomTarget = (displayHeight > winHeight || displayWidth > winWidth) ? zoomFitWindow :
                ((preventUpscale || GlobalVar::settingParameter.isOneToOnePreferred) ? ZOOM_BASE : zoomFitWindow);
            zoomCur = zoomTarget;

            zoomList = std::vector<int64_t>(ZOOM_LIST.begin(), ZOOM_LIST.end());
            if (!std::ranges::binary_search(ZOOM_LIST, zoomFitWindow) || 
                zoomFitWindow < ZOOM_LIST.front() || 
                zoomFitWindow > ZOOM_LIST.back())
                zoomList.emplace_back(zoomFitWindow);
            std::sort(zoomList.begin(), zoomList.end());
            auto it = std::find(zoomList.begin(), zoomList.end(), zoomTarget);
            zoomIndex = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : (int)(ZOOM_LIST.size() / 2);

            it = std::find(zoomList.begin(), zoomList.end(), zoomFitWindow);
            zoomIndexFix = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : zoomIndex;
            it = std::find(zoomList.begin(), zoomList.end(), ZOOM_BASE);
            zoomIndex100percent = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : zoomIndex;
        }
        else {
            curFrameIdxMax = 0;
            width = 0;
            height = 0;

            zoomList = std::vector<int64_t>(ZOOM_LIST.begin(), ZOOM_LIST.end());
            zoomIndex = (int)(ZOOM_LIST.size() / 2);
            zoomIndexFix = zoomIndex;
            zoomIndex100percent = zoomIndex;
            zoomTarget = ZOOM_BASE;
            zoomCur = ZOOM_BASE;
        }
    }

    void updateZoomList(int winWidth = 0, int winHeight = 0) {
        if (winHeight <= 0 || winWidth <= 0 || width <= 0 || height <= 0 || imageAssetPtr == nullptr)
            return;

        //适应显示窗口宽高的缩放比例
        int64_t zoomFitWindow = (rotation == 0 || rotation == 2)?
            std::min(winWidth * ZOOM_BASE / width, winHeight * ZOOM_BASE / height):
            std::min(winWidth * ZOOM_BASE / height, winHeight * ZOOM_BASE / width);

        zoomList = std::vector<int64_t>(ZOOM_LIST.begin(), ZOOM_LIST.end());
        if (!std::ranges::binary_search(ZOOM_LIST, zoomFitWindow) ||
            zoomFitWindow < ZOOM_LIST.front() ||
            zoomFitWindow > ZOOM_LIST.back())
            zoomList.emplace_back(zoomFitWindow);
        else {
            if (zoomIndex >= zoomList.size())
                zoomIndex = (int)zoomList.size() - 1;
        }
        std::sort(zoomList.begin(), zoomList.end());

        auto it = std::find(zoomList.begin(), zoomList.end(), zoomFitWindow);
        zoomIndexFix = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : zoomIndex;
        it = std::find(zoomList.begin(), zoomList.end(), ZOOM_BASE);
        zoomIndex100percent = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : zoomIndex;
    }

    void setZoom(int64_t zoom) {
        zoomTarget = zoomCur = std::max<int64_t>(1, zoom);
        zoomList = std::vector<int64_t>(ZOOM_LIST.begin(), ZOOM_LIST.end());
        if (!std::ranges::binary_search(ZOOM_LIST, zoomTarget))
            zoomList.emplace_back(zoomTarget);
        std::sort(zoomList.begin(), zoomList.end());

        auto it = std::find(zoomList.begin(), zoomList.end(), zoomTarget);
        zoomIndex = (it != zoomList.end()) ? (int)std::distance(zoomList.begin(), it) : 0;
        zoomIndexFix = zoomIndex;
        it = std::find(zoomList.begin(), zoomList.end(), ZOOM_BASE);
        zoomIndex100percent = (it != zoomList.end()) ?
            (int)std::distance(zoomList.begin(), it) : zoomIndex;
    }

    void selectZoomTarget(int64_t zoom) {
        zoomTarget = std::max<int64_t>(1, zoom);
        if (!std::ranges::binary_search(zoomList, zoomTarget)) {
            zoomList.emplace_back(zoomTarget);
            std::sort(zoomList.begin(), zoomList.end());
        }
        const auto it = std::find(zoomList.begin(), zoomList.end(), zoomTarget);
        zoomIndex = (it != zoomList.end()) ?
            static_cast<int>(std::distance(zoomList.begin(), it)) : zoomIndex;
    }

    void slideTargetRotateLeft() {
        slideTarget = { slideTarget.y, -slideTarget.x };
        slideCur = slideTarget;
    }

    void slideTargetRotateRight() {
        slideTarget = { -slideTarget.y, slideTarget.x };
        slideCur = slideTarget;
    }
};


class ExtraUIRes {
public:
    cv::Mat mainRes, leftArrow, rightArrow, leftRotate, rightRotate,
        flipHorizontal, flipVertical, fitWindow, actualSize, fullscreen,
        favorite, copy, deleteImage, setting, zoomOut, zoomIn,
        play, pause,
        presentationClose, animationBarPlaying, animationBarPausing;

    static cv::Mat loadSvgIcon(int resourceId, int size = OverlayLayout::BASE_ICON_SIZE) {
        const auto rc = jarkUtils::GetResource(resourceId, L"SVG");
        if (!rc.ptr || rc.size == 0)
            return {};
        const auto renderer = SvgRenderer::create(std::span<const uint8_t>(
            static_cast<const uint8_t*>(rc.ptr), rc.size));
        if (!renderer)
            return {};
        auto bitmap = renderer->renderToBitmap(size, size);
        if (bitmap.empty())
            return {};
        return cv::Mat(bitmap.height, bitmap.width, CV_8UC4, bitmap.bgra.data()).clone();
    }

    ExtraUIRes() {
        rcFileInfo rc;

        rc = jarkUtils::GetResource(IDB_PNG_MAIN_RES, L"PNG");
        mainRes = cv::imdecode(cv::Mat(1, (int)rc.size, CV_8UC1, (uint8_t*)rc.ptr), cv::IMREAD_UNCHANGED);

        leftArrow = loadSvgIcon(IDR_SVG_PREVIOUS_ICON, OverlayLayout::BASE_ICON_SIZE);
        rightArrow = loadSvgIcon(IDR_SVG_NEXT_ICON, OverlayLayout::BASE_ICON_SIZE);
        leftRotate = loadSvgIcon(IDR_SVG_ROTATE_LEFT_ICON, OverlayLayout::BASE_ICON_SIZE);
        rightRotate = loadSvgIcon(IDR_SVG_ROTATE_RIGHT_ICON, OverlayLayout::BASE_ICON_SIZE);
        flipHorizontal = loadSvgIcon(IDR_SVG_FLIP_HORIZONTAL_ICON, OverlayLayout::BASE_ICON_SIZE);
        flipVertical = loadSvgIcon(IDR_SVG_FLIP_VERTICAL_ICON, OverlayLayout::BASE_ICON_SIZE);
        fitWindow = loadSvgIcon(IDR_SVG_FIT_WINDOW_ICON, OverlayLayout::BASE_ICON_SIZE);
        actualSize = loadSvgIcon(IDR_SVG_ACTUAL_SIZE_ICON, OverlayLayout::BASE_ICON_SIZE);
        fullscreen = loadSvgIcon(IDR_SVG_FULLSCREEN_ICON, OverlayLayout::BASE_ICON_SIZE);
        favorite = loadSvgIcon(IDR_SVG_FAVORITE_ICON, OverlayLayout::BASE_ICON_SIZE);
        copy = loadSvgIcon(IDR_SVG_COPY_ICON, OverlayLayout::BASE_ICON_SIZE);
        deleteImage = loadSvgIcon(IDR_SVG_DELETE_ICON, OverlayLayout::BASE_ICON_SIZE);
        setting = loadSvgIcon(IDR_SVG_SETTINGS_ICON, OverlayLayout::BASE_ICON_SIZE);
        zoomOut = loadSvgIcon(IDR_SVG_ZOOM_OUT_ICON, OverlayLayout::BASE_ICON_SIZE);
        zoomIn = loadSvgIcon(IDR_SVG_ZOOM_IN_ICON, OverlayLayout::BASE_ICON_SIZE);
        play = loadSvgIcon(IDR_SVG_PLAY_ICON, OverlayLayout::BASE_ICON_SIZE);
        pause = loadSvgIcon(IDR_SVG_PAUSE_ICON, OverlayLayout::BASE_ICON_SIZE);
        presentationClose = loadSvgIcon(IDR_SVG_CLOSE_ICON, OverlayLayout::PRESENTATION_CLOSE_SIZE);

        animationBarPlaying = mainRes({ 0, 100, 200, 50 });
        animationBarPausing = mainRes({ 0, 150, 200, 50 });
    }
    ~ExtraUIRes() {}
};

class YeImageViewerApp : public D3D11App {
public:
    static inline bool isLowZoom = false;

    OperateQueue operateQueue;

    CursorPos cursorPos = CursorPos::centerArea;
    CursorPos cursorPosLast = CursorPos::centerArea;
    ShowExtraUI extraUIFlag = ShowExtraUI::none;
    bool mouseIsPressing = false;
    bool ctrlIsPressing = false;
    bool smoothShift = false;
    bool slideshowPlaying = false;
    bool showExif = false;
    bool zoomTextEditing = false;
    bool zoomEditReplaceSelection = false;
    std::string zoomEditText;
    Cood mousePos, mousePressPos;
    ImageDatabase imgDB;
    RotationStore rotationStore;
    std::unordered_set<std::wstring> favoritePaths;
    bool presentationMode = false;
    bool framedWindowAnchored = false;
    bool presentationClickCandidate = false;
    bool presentationCloseClickCandidate = false;
    DWORD presentationWindowedStyle = 0;
    DWORD presentationWindowedExtendedStyle = 0;
    Cood presentationPressPos;
    PresentationLayout::Result presentationLayout;

    int curFileIdx = -1;         // 文件在路径列表的索引
    vector<wstring> imgFileList; // 工作目录下所有图像文件路径

    TextDrawer textDrawer;       // 给Mat绘制文字
    const ImageAsset* imageInfoAssetCache = nullptr;
    uint32_t imageInfoLanguageCache = UINT32_MAX;
    ImageInfoPresentation::Model imageInfoModelCache;
    cv::Rect imageInfoPanelRect;
    cv::Rect imageInfoCloseRect;
    cv::Rect imageInfoCopyRect;
    int imageInfoScrollOffset = 0;
    int imageInfoContentHeight = 0;
    int imageInfoViewportHeight = 0;
    CurImageParameter curPar;
    ExtraUIRes extraUIRes;
    std::chrono::steady_clock::time_point lastClickTimestamp{}, lastWinResizeTimestamp{},
        slideshowNextAt{}, zoomIndicatorStartedAt{};

    explicit YeImageViewerApp(bool openImageOnCursorMonitor = false)
        : D3D11App(openImageOnCursorMonitor) {
        m_wndCaption = std::format(L"{} {}", appName, appVersion);
        auto rotationPath = std::filesystem::path(GlobalVar::settingPath);
        rotationPath.replace_filename(L"YeImageViewer.rotations.db");
        rotationStore.setStoragePath(std::move(rotationPath));
        rotationStore.load();

        textDrawer.setSize(TextRenderingPolicy::LOGICAL_FONT_SIZE);
    }

    ~YeImageViewerApp() {
    }

    bool hasCurrentImagePath() const {
        return curFileIdx >= 0 && curFileIdx < static_cast<int>(imgFileList.size()) &&
            imgFileList[curFileIdx] != m_wndCaption;
    }

    void openImageFromDialog() {
        std::wstring filePath = jarkUtils::SelectFile(m_hWnd);
        if (!filePath.empty()) {
            initOpenFile(filePath);
            operateQueue.push({ ActionENUM::refresh });
        }
        // A modal file dialog can consume the key-up message.
        ctrlIsPressing = false;
    }

    bool launchCurrentImageInExternalEditor(
        const ExternalEditorConfig::Entry& editor) {
        if (!hasCurrentImagePath())
            return false;

        std::error_code error;
        if (editor.path.empty() ||
            !std::filesystem::is_regular_file(editor.path, error)) {
            operateQueue.push({ ActionENUM::setting, 0 });
            return false;
        }

        const std::wstring parameters = ExternalEditorConfig::quoteImageArgument(
            imgFileList[curFileIdx]);
        const std::wstring workingDirectory =
            std::filesystem::path(editor.path).parent_path().wstring();
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(m_hWnd, L"open",
            editor.path.c_str(), parameters.c_str(),
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(), SW_SHOWNORMAL));
        if (result <= 32) {
            const std::wstring message = std::format(L"{}: {}", getUIStringW(60), result);
            MessageBoxW(m_hWnd, message.c_str(), getUIStringW(14), MB_OK | MB_ICONERROR);
            return false;
        }
        return true;
    }

    void chooseExternalEditorAndOpenCurrentImage() {
        const std::wstring selected = jarkUtils::SelectExecutable(m_hWnd);
        if (selected.empty())
            return;

        const std::wstring automaticName = ExternalEditorConfig::defaultName(selected);
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        auto requestedName = showTextInputDialog(m_hWnd, automaticName,
            chinese ? L"设置编辑应用" : L"Configure editor",
            chinese ? L"显示名称（留空则使用程序名称）：" :
                L"Display name (leave blank to use the application name):");
        if (!requestedName)
            return;
        const std::wstring displayName = ExternalEditorConfig::resolvedName(
            *requestedName, selected);
        ExternalEditorConfig::Entry editor{ displayName, selected };
        if (!ExternalEditorConfig::add(GlobalVar::externalEditors, editor)) {
            MessageBoxW(m_hWnd,
                chinese ? L"最多可以设置 10 个外部图片编辑器。" :
                    L"You can configure up to 10 external image editors.",
                getUIStringW(15), MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (!ExternalEditorConfig::save(GlobalVar::externalEditorsPath,
            GlobalVar::externalEditors)) {
            MessageBoxW(m_hWnd,
                chinese ? L"无法保存外部编辑器设置。" :
                    L"Unable to save the external editor settings.",
                getUIStringW(14), MB_OK | MB_ICONERROR);
            return;
        }
        launchCurrentImageInExternalEditor(editor);
    }

    void scheduleSlideshowNext() {
        slideshowNextAt = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(SlideshowPolicy::INTERVAL_MS);
    }

    void stopSlideshow() {
        slideshowPlaying = false;
        slideshowNextAt = {};
    }

    void showZoomIndicator() {
        zoomIndicatorStartedAt = std::chrono::steady_clock::now();
    }

    void beginZoomTextEdit() {
        zoomTextEditing = true;
        zoomEditReplaceSelection = true;
        zoomEditText = std::to_string(
            ZoomPolicy::displayPercent(curPar.zoomCur, CurImageParameter::ZOOM_BASE));
        extraUIFlag = ShowExtraUI::bottomToolbar;
        operateQueue.push({ ActionENUM::refresh });
    }

    void cancelZoomTextEdit() {
        zoomTextEditing = false;
        zoomEditReplaceSelection = false;
        zoomEditText.clear();
        operateQueue.push({ ActionENUM::refresh });
    }

    void commitZoomTextEdit() {
        const auto percent = ZoomEditPolicy::parsePercent(zoomEditText);
        zoomTextEditing = false;
        zoomEditReplaceSelection = false;
        zoomEditText.clear();
        if (percent)
            operateQueue.push({ ActionENUM::zoomPercent, *percent });
        else
            operateQueue.push({ ActionENUM::refresh });
    }

    const wchar_t* renameValidationMessage(RenamePolicy::ValidationError error) const {
        switch (error) {
        case RenamePolicy::ValidationError::Empty: return getUIStringW(50);
        case RenamePolicy::ValidationError::InvalidCharacter: return getUIStringW(51);
        case RenamePolicy::ValidationError::TrailingDotOrSpace: return getUIStringW(52);
        case RenamePolicy::ValidationError::ReservedName: return getUIStringW(53);
        case RenamePolicy::ValidationError::TooLong: return getUIStringW(54);
        default: return getUIStringW(14);
        }
    }

    void renameCurrentImage() {
        if (!hasCurrentImagePath())
            return;

        const std::filesystem::path source(imgFileList[curFileIdx]);
        std::error_code sourceError;
        if (!std::filesystem::is_regular_file(source, sourceError))
            return;

        std::wstring candidate = source.stem().wstring();
        while (true) {
            auto requestedName = showTextInputDialog(m_hWnd, candidate,
                getUIStringW(48), getUIStringW(49));
            if (!requestedName)
                return;

            candidate = RenamePolicy::trim(*requestedName);
            const auto renameResult = RenamePolicy::renameFile(source, candidate);
            if (renameResult.error == RenamePolicy::OperationError::InvalidName) {
                MessageBoxW(m_hWnd, renameValidationMessage(renameResult.validation), getUIStringW(48),
                    MB_OK | MB_ICONWARNING);
                continue;
            }
            if (renameResult.error == RenamePolicy::OperationError::NoChange)
                return;
            if (renameResult.error == RenamePolicy::OperationError::AlreadyExists) {
                MessageBoxW(m_hWnd, getUIStringW(55), getUIStringW(48),
                    MB_OK | MB_ICONWARNING);
                continue;
            }
            if (renameResult.error == RenamePolicy::OperationError::SystemError) {
                const auto message = std::format(
                    L"{} 0x{:08X}", getUIStringW(56), renameResult.systemError);
                MessageBoxW(m_hWnd, message.c_str(), getUIStringW(48), MB_OK | MB_ICONERROR);
                continue;
            }

            const auto sourceText = source.wstring();
            const auto targetText = renameResult.target.wstring();
            const int savedRotation = rotationStore.get(sourceText);
            rotationStore.erase(sourceText);
            rotationStore.set(targetText, savedRotation);
            rotationStore.save();

            if (favoritePaths.erase(sourceText) > 0)
                favoritePaths.insert(targetText);

            initOpenFile(targetText);
            operateQueue.push({ ActionENUM::refresh });
            return;
        }
    }

    void initCurrentImageParameters() {
        const bool isRealImage = hasCurrentImagePath();
        const int savedRotation = isRealImage ? rotationStore.get(imgFileList[curFileIdx]) : 0;
        // The functional home page is native-DPI text. Never enlarge its
        // already rasterized glyphs when the user resizes the window.
        curPar.Init(winWidth, winHeight, savedRotation, true);
        if (presentationMode)
            applyPresentationImageLayout();
        else if (framedWindowAnchored)
            applyAnchoredWindowImageLayout();
    }

    void persistCurrentRotation() {
        if (!hasCurrentImagePath())
            return;
        rotationStore.set(imgFileList[curFileIdx], curPar.rotation);
        rotationStore.save();
    }

    void applyDefaultWindowSize() {
        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
            return;
        const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        const auto layout = InitialWindowLayout::calculate(
            1, 1, workWidth, workHeight);

        RECT outerRect{ 0, 0, layout.clientWidth, layout.clientHeight };
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        const auto extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        if (!AdjustWindowRectExForDpi(&outerRect, style, FALSE, extendedStyle, GetDpiForWindow(m_hWnd)))
            AdjustWindowRectEx(&outerRect, style, FALSE, extendedStyle);

        const int outerWidth = outerRect.right - outerRect.left;
        const int outerHeight = outerRect.bottom - outerRect.top;
        const int x = monitorInfo.rcWork.left + (workWidth - outerWidth) / 2;
        const int y = monitorInfo.rcWork.top + (workHeight - outerHeight) / 2;
        SetWindowPos(m_hWnd, nullptr, x, y, outerWidth, outerHeight,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void applyHomeWindowSize() {
        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST),
            &monitorInfo)) {
            return;
        }
        const UINT dpi = GetDpiForWindow(m_hWnd);
        const auto homeCanvas = HomeScreenLayout::nativeCanvas(
            static_cast<int>(dpi));
        RECT outerRect{ 0, 0,
            homeCanvas.width, homeCanvas.height };
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        const auto extendedStyle = static_cast<DWORD>(
            GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        if (!AdjustWindowRectExForDpi(&outerRect, style, FALSE, extendedStyle, dpi))
            AdjustWindowRectEx(&outerRect, style, FALSE, extendedStyle);
        const int outerWidth = outerRect.right - outerRect.left;
        const int outerHeight = outerRect.bottom - outerRect.top;
        const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        SetWindowPos(m_hWnd, nullptr,
            monitorInfo.rcWork.left + (workWidth - outerWidth) / 2,
            monitorInfo.rcWork.top + (workHeight - outerHeight) / 2,
            outerWidth, outerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    PresentationLayout::Result calculatePresentationLayout() const {
        if (!hasCurrentImagePath() || curPar.width <= 0 || curPar.height <= 0)
            return {};
        return PresentationLayout::calculate(
            curPar.width, curPar.height, winWidth, winHeight,
            static_cast<int>(GetDpiForWindow(m_hWnd)),
            curPar.rotation == 1 || curPar.rotation == 3);
    }

    void applyPresentationImageLayout() {
        if (!presentationMode)
            return;
        presentationLayout = calculatePresentationLayout();
        if (presentationLayout.renderedWidth <= 0 || presentationLayout.renderedHeight <= 0)
            return;

        curPar.setZoom(static_cast<int64_t>(std::lround(
            presentationLayout.scale * curPar.ZOOM_BASE)));
        curPar.slideCur = curPar.slideTarget = {
            presentationLayout.initialSlideX,
            presentationLayout.initialSlideY,
        };
    }

    void applyAnchoredWindowImageLayout() {
        if (!framedWindowAnchored || !hasCurrentImagePath())
            return;

        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
            return;
        const auto layout = PresentationLayout::calculate(
            curPar.width, curPar.height,
            monitorInfo.rcWork.right - monitorInfo.rcWork.left,
            monitorInfo.rcWork.bottom - monitorInfo.rcWork.top,
            static_cast<int>(GetDpiForWindow(m_hWnd)),
            curPar.rotation == 1 || curPar.rotation == 3);
        if (layout.renderedWidth <= 0 || layout.renderedHeight <= 0)
            return;

        curPar.setZoom(static_cast<int64_t>(std::lround(
            layout.scale * curPar.ZOOM_BASE)));
        curPar.slideCur = curPar.slideTarget = {
            0,
            PresentationLayout::topAlignedSlide(layout.renderedHeight, winHeight),
        };
    }

    RECT currentImageRect() const {
        if (curPar.width <= 0 || curPar.height <= 0)
            return {};
        const int displayWidth = (curPar.rotation == 0 || curPar.rotation == 2) ?
            curPar.width : curPar.height;
        const int displayHeight = (curPar.rotation == 0 || curPar.rotation == 2) ?
            curPar.height : curPar.width;
        const int renderedWidth = static_cast<int>(std::lround(
            static_cast<double>(displayWidth) * curPar.zoomCur / curPar.ZOOM_BASE));
        const int renderedHeight = static_cast<int>(std::lround(
            static_cast<double>(displayHeight) * curPar.zoomCur / curPar.ZOOM_BASE));
        const int left = curPar.slideCur.x + static_cast<int>(std::lround(
            (winWidth - renderedWidth) / 2.0));
        const int top = curPar.slideCur.y + static_cast<int>(std::lround(
            (winHeight - renderedHeight) / 2.0));
        return { left, top, left + renderedWidth, top + renderedHeight };
    }

    bool isPointInsideCurrentImage(int x, int y) const {
        const auto rect = currentImageRect();
        return rect.left <= x && x < rect.right && rect.top <= y && y < rect.bottom;
    }

    bool isHomeOpenButtonAt(int x, int y) const {
        if (hasCurrentImagePath() || curPar.rotation != 0)
            return false;
        const auto rect = currentImageRect();
        return HomeScreenLayout::hitOpenButton(
            { rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top }, x, y);
    }

    void enterPresentationMode() {
        if (presentationMode || !hasCurrentImagePath())
            return;

        presentationWindowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        presentationWindowedExtendedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        framedWindowAnchored = false;
        presentationMode = true;
        imageInfoScrollOffset = 0;

        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
            presentationMode = false;
            return;
        }

        SetWindowLongPtrW(m_hWnd, GWL_STYLE,
            presentationWindowedStyle & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE,
            presentationWindowedExtendedStyle &
            ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetPresentationBackdrop(true);
        SetWindowPos(m_hWnd, HWND_TOP,
            monitorInfo.rcWork.left, monitorInfo.rcWork.top,
            monitorInfo.rcWork.right - monitorInfo.rcWork.left,
            monitorInfo.rcWork.bottom - monitorInfo.rcWork.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
        applyPresentationImageLayout();
        operateQueue.push({ ActionENUM::refresh });
    }

    void exitPresentationMode() {
        if (!presentationMode)
            return;

        MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
            return;
        const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        // Recalculate from the image that is visible at the moment of exit.
        // Browsing in presentation must not keep the entry image's frame size.
        const auto currentPresentationLayout = calculatePresentationLayout();
        const auto windowed = PresentationLayout::calculateWindowed(
            currentPresentationLayout, workWidth, workHeight);

        presentationMode = false;
        imageInfoScrollOffset = 0;
        framedWindowAnchored = true;
        presentationClickCandidate = false;
        presentationCloseClickCandidate = false;
        mouseIsPressing = false;
        cursorPosLast = cursorPos = CursorPos::centerArea;
        extraUIFlag = ShowExtraUI::none;
        ReleaseCapture();
        SetPresentationBackdrop(false);
        SetWindowLongPtrW(m_hWnd, GWL_STYLE, presentationWindowedStyle);
        SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE, presentationWindowedExtendedStyle);

        if (windowed.clientWidth <= 0 || windowed.clientHeight <= 0) {
            applyDefaultWindowSize();
            return;
        }

        RECT outerRect{ 0, 0, windowed.clientWidth, windowed.clientHeight };
        if (!AdjustWindowRectExForDpi(&outerRect, presentationWindowedStyle, FALSE,
            presentationWindowedExtendedStyle, GetDpiForWindow(m_hWnd))) {
            AdjustWindowRectEx(&outerRect, presentationWindowedStyle, FALSE,
                presentationWindowedExtendedStyle);
        }
        const int outerWidth = outerRect.right - outerRect.left;
        const int outerHeight = outerRect.bottom - outerRect.top;
        const int x = monitorInfo.rcWork.left + (workWidth - outerWidth) / 2;
        const int y = monitorInfo.rcWork.top + (workHeight - outerHeight) / 2;
        SetWindowPos(m_hWnd, HWND_TOP, x, y, outerWidth, outerHeight,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        EnableWindow(m_hWnd, TRUE);
        BringWindowToTop(m_hWnd);
        SetActiveWindow(m_hWnd);
        SetForegroundWindow(m_hWnd);
        SetFocus(m_hWnd);

        curPar.slideCur = curPar.slideTarget = {
            windowed.initialSlideX,
            windowed.initialSlideY,
        };
        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];
        drawCanvas(srcImg, mainCanvas);
        drawExifInfo(mainCanvas);
        drawExtraUI(mainCanvas);
        updateMainCanvas();
        operateQueue.push({ ActionENUM::refresh });
    }

    HRESULT InitWindow(HINSTANCE hInstance) {
        if (!SUCCEEDED(D3D11App::Initialize(hInstance)))
            return S_FALSE;

        if (m_pD3DDevice == nullptr)
            return S_FALSE;

        applyDefaultWindowSize();
        imgDB.setColorManagementWindow(m_hWnd);

        return S_OK;
    }

    void initOpenFile(wstring filePath) {
        namespace fs = std::filesystem;

        stopSlideshow();
        curFileIdx = -1;
        imgFileList.clear();
        imgDB.clear();

        if (filePath.empty()) {
            imgFileList.emplace_back(m_wndCaption);
            curFileIdx = 0;
            imgDB.put(m_wndCaption, { ImageFormat::Still,
                imgDB.getHomeMat(GetDpiForWindow(m_hWnd)), {}, {}, getUIString(32) });
            curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[curFileIdx]);
            initCurrentImageParameters();
            applyHomeWindowSize();
            return;
        }

        fs::path fullPath = fs::absolute(filePath);
        wstring openFileName = fullPath.filename().wstring();

        auto workDir = fullPath.parent_path();
        if (fs::exists(workDir)) {
            std::vector<std::wstring> fileNameList;
            for (const auto& entry : fs::directory_iterator(workDir)) {
                if (!entry.is_regular_file())continue;

                std::wstring ext = entry.path().extension().wstring();
                if (ext.length() < 2)continue;
                
                ext = ext.substr(1);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

                if (ImageDatabase::supportExt.contains(ext) || ImageDatabase::supportRaw.contains(ext)) {
                    fileNameList.emplace_back(entry.path().filename().wstring());
                }
            }

            // 自然排序 数字感知排序
            // StrCmpLogicalW 要求以 '\0' 结尾的字符串，wstring_view::data() 不保证这点，
            // 这里直接按 wstring 比较，用 c_str() 传入。
            std::sort(fileNameList.begin(), fileNameList.end(), [](const std::wstring& a, const std::wstring& b) -> bool {
                return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });

            for (auto& fileName : fileNameList) {
                auto fullpath = (workDir / fileName).wstring();
                imgFileList.emplace_back(std::move(fullpath));
                if (curFileIdx == -1 && openFileName == fileName) {
                    curFileIdx = (int)imgFileList.size() - 1;
                }
            }
        }
        else {
            curFileIdx = -1;
        }

        if (curFileIdx < 0) {
            if (filePath.empty()) { //直接打开软件，没有传入参数
                imgFileList.emplace_back(m_wndCaption);
                curFileIdx = 0;
                imgDB.put(m_wndCaption, { ImageFormat::Still,
                    imgDB.getHomeMat(GetDpiForWindow(m_hWnd)), {}, {}, getUIString(32) });
            }
            else { // 打开的文件不支持，默认加到尾部
                imgFileList.emplace_back(fullPath.wstring());
                curFileIdx = (int)imgFileList.size() - 1;

                auto dotPos = filePath.rfind(L'.');
                auto ext = wstring((dotPos != std::wstring::npos && dotPos < filePath.size() - 1) ?
                    filePath.substr(dotPos + 1) : filePath);
                for (auto& c : ext)	c = std::tolower(c);

                if (!ImageDatabase::videoExt.contains(ext)) // 非视频文件直接提示错误。若是视频文件则尝试当做动态照片处理(仅解码前 MAX_VIDEO_FRAMES 帧)
                    imgDB.put(fullPath.wstring(), { ImageFormat::Still, imgDB.getErrorTipsMat(), {}, {}, getUIString(33) });
            }
        }

        curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
        initCurrentImageParameters();
    }

    inline void handleAnimationControl(int x, int y) {
        // 按钮ID  0:上一帧  1:暂停/继续  2:下一帧  3:保存该帧
        int buttonIdx = (x + 100 - winWidth / 2) / 50;
        if (buttonIdx < 0 || 3 < buttonIdx || curPar.imageAssetPtr->format != ImageFormat::Animated)
            return;

        if (curPar.isAnimationPause) {
            switch (buttonIdx) {
            case 0: {
                if (--curPar.curFrameIdx < 0)
                    curPar.curFrameIdx = curPar.curFrameIdxMax;
                operateQueue.push({ ActionENUM::refresh });
            }break;
            case 1: {
                curPar.isAnimationPause = !curPar.isAnimationPause;
                operateQueue.push({ ActionENUM::refresh });
            }break;
            case 2: {
                if (++curPar.curFrameIdx > curPar.curFrameIdxMax)
                    curPar.curFrameIdx = 0;
                operateQueue.push({ ActionENUM::refresh });
            }break;
            case 3: {
                auto [filePath, isJPG] = jarkUtils::saveImageDialogW(getUIStringW(4));
                if (filePath.length() <= 2)
                    break;

                cv::Mat img;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    img = curPar.imageAssetPtr->primaryFrame;
                else
                    img = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                std::vector<uchar> buffer;
                if (cv::imencode(isJPG ? ".jpg" : ".png", img, buffer)) {
                    std::ofstream file(filePath, std::ios::binary);
                    if (file.is_open()) {
                        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                        file.close();
                    }
                }
            }break;
            }
        }
        else {
            if (buttonIdx == 1) {
                curPar.isAnimationPause = !curPar.isAnimationPause;
                operateQueue.push({ ActionENUM::refresh });
            }
        }
    }

    void OnMouseDown(WPARAM btnState, int x, int y, WPARAM wParam) override {
        switch ((uint64_t)btnState)
        {
        case WM_LBUTTONDOWN: {//左键
            if (presentationMode &&
                OverlayLayout::presentationCloseRect(winWidth, winHeight).contains(x, y)) {
                presentationClickCandidate = false;
                presentationCloseClickCandidate = true;
                mouseIsPressing = false;
                SetCapture(m_hWnd);
                return;
            }

            if (isHomeOpenButtonAt(x, y)) {
                presentationClickCandidate = false;
                mouseIsPressing = false;
                openImageFromDialog();
                return;
            }

            if (showExif && imageInfoPanelRect.contains(cv::Point{ x, y })) {
                presentationClickCandidate = false;
                mouseIsPressing = false;
                if (imageInfoCloseRect.contains(cv::Point{ x, y })) {
                    showExif = false;
                    imageInfoScrollOffset = 0;
                    operateQueue.push({ ActionENUM::refresh });
                }
                else if (imageInfoCopyRect.contains(cv::Point{ x, y })) {
                    jarkUtils::copyToClipboard(
                        jarkUtils::utf8ToWstring(curPar.imageAssetPtr->exifInfo));
                }
                return;
            }

            const bool zoomTextClicked = extraUIFlag == ShowExtraUI::bottomToolbar &&
                OverlayLayout::zoomTextRect(winWidth, winHeight).contains(x, y);
            if (zoomTextEditing && !zoomTextClicked)
                commitZoomTextEdit();
            if (zoomTextClicked) {
                if (!zoomTextEditing)
                    beginZoomTextEdit();
                mouseIsPressing = false;
                return;
            }

            if (presentationMode && cursorPos == CursorPos::centerArea) {
                const bool insideImage = isPointInsideCurrentImage(x, y);
                presentationClickCandidate = !insideImage;
                presentationPressPos = { x, y };
                mousePressPos = { x, y };
                mouseIsPressing = insideImage;
                return;
            }

            if (cursorPos == CursorPos::centerArea) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = duration_cast<std::chrono::milliseconds>(now - lastClickTimestamp).count();
                lastClickTimestamp = now;

                if (10 < elapsed && elapsed < 300) { // 10 ~ 300 ms
                    jarkUtils::ToggleFullScreen(m_hWnd);
                }
                else {
                    mouseIsPressing = true;
                }
            }

            mousePressPos = { x, y };

            const auto toolbarCommand = ToolbarCommand::resolve(
                OverlayLayout::hitTest(winWidth, winHeight, x, y));
            switch (toolbarCommand) {
            case ToolbarCommand::Command::PreviousImage: operateQueue.push({ ActionENUM::preImg }); break;
            case ToolbarCommand::Command::PlayPause: operateQueue.push({ ActionENUM::toggleSlideshow }); break;
            case ToolbarCommand::Command::NextImage: operateQueue.push({ ActionENUM::nextImg }); break;
            case ToolbarCommand::Command::RotateLeft: operateQueue.push({ ActionENUM::rotateLeft }); break;
            case ToolbarCommand::Command::RotateRight: operateQueue.push({ ActionENUM::rotateRight }); break;
            case ToolbarCommand::Command::FlipHorizontal: operateQueue.push({ ActionENUM::flipHorizontal }); break;
            case ToolbarCommand::Command::FlipVertical: operateQueue.push({ ActionENUM::flipVertical }); break;
            case ToolbarCommand::Command::ZoomFit: operateQueue.push({ ActionENUM::zoomFit }); break;
            case ToolbarCommand::Command::ZoomActual: operateQueue.push({ ActionENUM::zoomActual }); break;
            case ToolbarCommand::Command::Fullscreen: operateQueue.push({ ActionENUM::toggleFullScreen }); break;
            case ToolbarCommand::Command::Settings: operateQueue.push({ ActionENUM::setting, 0 }); break;
            case ToolbarCommand::Command::ZoomOut: operateQueue.push({ ActionENUM::zoomOut }); break;
            case ToolbarCommand::Command::ZoomIn: operateQueue.push({ ActionENUM::zoomIn }); break;
            case ToolbarCommand::Command::EditZoom:
            case ToolbarCommand::Command::None:
                if (cursorPos == CursorPos::leftEdge)
                    operateQueue.push({ ActionENUM::preImg });
                else if (cursorPos == CursorPos::rightEdge)
                    operateQueue.push({ ActionENUM::nextImg });
                else if (cursorPos == CursorPos::centerTop)
                    handleAnimationControl(x, y);
                break;
            }
            return;
        }

        case WM_RBUTTONDOWN: {//右键
            return;
        }

        case WM_MBUTTONDOWN: {//中键
            operateQueue.push({ ActionENUM::toggleExif });
            return;
        }

        case WM_XBUTTONDOWN: {//侧键
            WORD xButton = GET_XBUTTON_WPARAM(wParam);
            if (xButton == XBUTTON1) {
                operateQueue.push({ ActionENUM::nextImg });
            }
            else if (xButton == XBUTTON2) {
                operateQueue.push({ ActionENUM::preImg });
            }
            return;
        }

        default: {
            JARK_LOG("{} KeyValue: 0x{:04x}", __FUNCTION__, (uint64_t)btnState);
        }break;
        }
    }

    void OnMouseUp(WPARAM btnState, int x, int y, WPARAM wParam) override {
        switch ((uint64_t)btnState)
        {
        case WM_LBUTTONUP: {//左键
            if (presentationCloseClickCandidate) {
                const bool shouldClose = presentationMode &&
                    OverlayLayout::presentationCloseRect(winWidth, winHeight).contains(x, y);
                presentationCloseClickCandidate = false;
                mouseIsPressing = false;
                ReleaseCapture();
                if (shouldClose)
                    operateQueue.push({ ActionENUM::requestExit });
                return;
            }
            if (presentationMode && presentationClickCandidate) {
                const int deltaX = x - presentationPressPos.x;
                const int deltaY = y - presentationPressPos.y;
                const bool isClick = deltaX * deltaX + deltaY * deltaY <= 25 &&
                    !isPointInsideCurrentImage(x, y);
                presentationClickCandidate = false;
                mouseIsPressing = false;
                if (isClick) {
                    exitPresentationMode();
                    return;
                }
            }
            mouseIsPressing = false;
            operateQueue.push({ ActionENUM::refresh });
            return;
        }

        case WM_RBUTTONUP: {//右键
            if (GlobalVar::settingParameter.rightClickAction == 0) {
                PostMessageW(m_hWnd, WM_CONTEXTMENU, 0, MAKELPARAM(x, y));
            }
            else {
                operateQueue.push({ ActionENUM::requestExit });
            }
            return;
        }

        case WM_MBUTTONUP: {//中键
            return;
        }

        case WM_XBUTTONUP: {//侧键
            //WORD xButton = GET_XBUTTON_WPARAM(wParam);
            //if (xButton == XBUTTON1){}
            //else if (xButton == XBUTTON2){}
            return;
        }

        default: {
            JARK_LOG("{} KeyValue: 0x{:04x}", __FUNCTION__, (uint64_t)btnState);
        }break;
        }
    }

    void OnMouseMove(WPARAM btnState, int x, int y) override {
        mousePos = { x, y };
        SetCursor(LoadCursorW(nullptr, isHomeOpenButtonAt(x, y) ? IDC_HAND : IDC_ARROW));

        if (presentationClickCandidate) {
            const int deltaX = x - presentationPressPos.x;
            const int deltaY = y - presentationPressPos.y;
            if (deltaX * deltaX + deltaY * deltaY > 25)
                presentationClickCandidate = false;
        }

        if (mouseIsPressing) {
            cursorPos = CursorPos::centerArea;
        }
        else {
            switch (OverlayLayout::hitTest(winWidth, winHeight, x, y)) {
            case OverlayLayout::Hit::EdgePreviousImage:
                cursorPos = CursorPos::leftEdge;
                break;
            case OverlayLayout::Hit::EdgeNextImage:
                cursorPos = CursorPos::rightEdge;
                break;
            case OverlayLayout::Hit::ToolbarPreviousImage:
                cursorPos = CursorPos::toolbarPrevious;
                break;
            case OverlayLayout::Hit::ToolbarPlayPause:
                cursorPos = CursorPos::toolbarPlayPause;
                break;
            case OverlayLayout::Hit::ToolbarNextImage:
                cursorPos = CursorPos::toolbarNext;
                break;
            case OverlayLayout::Hit::RotateLeft:
                cursorPos = CursorPos::toolbarRotateLeft;
                break;
            case OverlayLayout::Hit::RotateRight:
                cursorPos = CursorPos::toolbarRotateRight;
                break;
            case OverlayLayout::Hit::FlipHorizontal:
                cursorPos = CursorPos::toolbarFlipHorizontal;
                break;
            case OverlayLayout::Hit::FlipVertical:
                cursorPos = CursorPos::toolbarFlipVertical;
                break;
            case OverlayLayout::Hit::ZoomFit:
                cursorPos = CursorPos::toolbarZoomFit;
                break;
            case OverlayLayout::Hit::ZoomActual:
                cursorPos = CursorPos::toolbarZoomActual;
                break;
            case OverlayLayout::Hit::Fullscreen:
                cursorPos = CursorPos::toolbarFullscreen;
                break;
            case OverlayLayout::Hit::Favorite:
                cursorPos = CursorPos::toolbarFavorite;
                break;
            case OverlayLayout::Hit::CopyImage:
                cursorPos = CursorPos::toolbarCopy;
                break;
            case OverlayLayout::Hit::DeleteImage:
                cursorPos = CursorPos::toolbarDelete;
                break;
            case OverlayLayout::Hit::Settings:
                cursorPos = CursorPos::toolbarSetting;
                break;
            case OverlayLayout::Hit::ZoomOut:
                cursorPos = CursorPos::toolbarZoomOut;
                break;
            case OverlayLayout::Hit::ZoomText:
                cursorPos = CursorPos::toolbarZoomText;
                break;
            case OverlayLayout::Hit::ZoomIn:
                cursorPos = CursorPos::toolbarZoomIn;
                break;
            case OverlayLayout::Hit::Toolbar:
                cursorPos = CursorPos::toolbar;
                break;
            case OverlayLayout::Hit::PresentationClose:
                cursorPos = presentationMode ? CursorPos::presentationClose : CursorPos::centerArea;
                break;
            default:
                cursorPos = CursorPos::centerArea;
                break;
            }

            if (y < 50 && abs(x - winWidth / 2) < 100) {
                cursorPos = CursorPos::centerTop;
            }
        }

        if (cursorPosLast != cursorPos) {
            switch (cursorPos) {
            case CursorPos::leftEdge:
                extraUIFlag = ShowExtraUI::bottomToolbar;
                break;
            case CursorPos::centerTop:
                extraUIFlag = curPar.imageAssetPtr->format == ImageFormat::Animated ?
                    ShowExtraUI::animationBar : ShowExtraUI::none;
                break;
            case CursorPos::centerArea:
            case CursorPos::presentationClose:
                extraUIFlag = ShowExtraUI::none;
                break;
            case CursorPos::rightEdge:
                extraUIFlag = ShowExtraUI::bottomToolbar;
                break;
            case CursorPos::toolbarRotateLeft:
            case CursorPos::toolbarRotateRight:
            case CursorPos::toolbarFlipHorizontal:
            case CursorPos::toolbarFlipVertical:
            case CursorPos::toolbarZoomFit:
            case CursorPos::toolbarZoomActual:
            case CursorPos::toolbarFullscreen:
            case CursorPos::toolbarFavorite:
            case CursorPos::toolbarCopy:
            case CursorPos::toolbarDelete:
            case CursorPos::toolbarSetting:
            case CursorPos::toolbarZoomOut:
            case CursorPos::toolbarZoomText:
            case CursorPos::toolbarZoomIn:
            case CursorPos::toolbarPrevious:
            case CursorPos::toolbarPlayPause:
            case CursorPos::toolbarNext:
            case CursorPos::toolbar:
                extraUIFlag = ShowExtraUI::bottomToolbar;
                break;
            }

            if (zoomTextEditing)
                extraUIFlag = ShowExtraUI::bottomToolbar;

            operateQueue.push({ ActionENUM::refresh });
            cursorPosLast = cursorPos;
        }

        if (mouseIsPressing) {
            auto slideDelta = mousePos - mousePressPos;
            mousePressPos = mousePos;
            operateQueue.push({ ActionENUM::slide, slideDelta.x, slideDelta.y });
        }
    }

    void OnMouseLeave() override {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        cursorPosLast = cursorPos = CursorPos::centerArea;
        extraUIFlag = zoomTextEditing ? ShowExtraUI::bottomToolbar : ShowExtraUI::none;
        mouseIsPressing = false;
        operateQueue.push({ ActionENUM::refresh });
    }

    void OnMouseWheel(UINT nFlags, short zDelta, int x, int y) override {
        POINT clientPoint{ static_cast<short>(x), static_cast<short>(y) };
        ScreenToClient(m_hWnd, &clientPoint);
        if (showExif && (nFlags & (MK_CONTROL | MK_SHIFT)) == 0 &&
            imageInfoPanelRect.contains(cv::Point{ clientPoint.x, clientPoint.y })) {
            if (imageInfoContentHeight > imageInfoViewportHeight) {
                const int step = TextRenderingPolicy::scaledPixelSize(54, GetDpiForWindow(m_hWnd));
                imageInfoScrollOffset = ImageInfoPresentation::clampScrollOffset(
                    imageInfoContentHeight, imageInfoViewportHeight,
                    imageInfoScrollOffset + (zDelta < 0 ? step : -step));
                operateQueue.push({ ActionENUM::refresh });
            }
            return;
        }

        const int panStep = std::max(1, (winWidth + winHeight) / 16);
        const auto* shortcutStorage = GlobalVar::settingParameter.reserve;
        const auto modifiedWheel = WheelInput::resolve(nFlags, zDelta, panStep,
            ShortcutConfig::getWheelAction(shortcutStorage, 0),
            ShortcutConfig::getWheelAction(shortcutStorage, 1),
            ShortcutConfig::getWheelAction(shortcutStorage, 2));
        switch (modifiedWheel.intent) {
        case WheelInput::Intent::ZoomIn:
            operateQueue.push({ ActionENUM::zoomIn });
            return;
        case WheelInput::Intent::ZoomOut:
            operateQueue.push({ ActionENUM::zoomOut });
            return;
        case WheelInput::Intent::PanVertical:
            operateQueue.push({ ActionENUM::slide, 0, modifiedWheel.verticalDelta });
            return;
        case WheelInput::Intent::PanHorizontal:
            operateQueue.push({ ActionENUM::slide, modifiedWheel.horizontalDelta, 0 });
            return;
        case WheelInput::Intent::PreviousImage:
            operateQueue.push({ ActionENUM::preImg });
            return;
        case WheelInput::Intent::NextImage:
            operateQueue.push({ ActionENUM::nextImg });
            return;
        case WheelInput::Intent::Default:
            break;
        }

        switch (cursorPos)
        {
        case CursorPos::centerArea:
            operateQueue.push({ zDelta < 0 ? ActionENUM::zoomOut : ActionENUM::zoomIn });
            break;

        case CursorPos::leftEdge:
        case CursorPos::rightEdge:
        case CursorPos::toolbarPrevious:
        case CursorPos::toolbarNext:
            operateQueue.push({ zDelta < 0 ? ActionENUM::nextImg : ActionENUM::preImg });
            break;

        case CursorPos::toolbarSetting:
        case CursorPos::toolbarZoomText:
        case CursorPos::toolbarPlayPause:
        case CursorPos::toolbar:
        case CursorPos::centerTop:
        case CursorPos::presentationClose:
            break;

        case CursorPos::toolbarRotateLeft:
        case CursorPos::toolbarRotateRight:
            operateQueue.push({ zDelta < 0 ? ActionENUM::rotateRight : ActionENUM::rotateLeft });
            break;
        }
    }

    void OnMaximizeRequested() override {
        enterPresentationMode();
    }

    void handleEscapeKey() {
        const auto action = EscapeBehavior::resolve(
            presentationMode, jarkUtils::IsFullScreen(m_hWnd), IsZoomed(m_hWnd),
            GlobalVar::settingParameter.escapeClosesImage);
        switch (action) {
        case EscapeBehavior::Action::ExitPresentation:
            exitPresentationMode();
            break;
        case EscapeBehavior::Action::ExitFullScreen:
            jarkUtils::ToggleFullScreen(m_hWnd);
            break;
        case EscapeBehavior::Action::RestoreWindow:
            ShowWindow(m_hWnd, SW_RESTORE);
            break;
        case EscapeBehavior::Action::CloseImage:
            operateQueue.push({ ActionENUM::requestExit });
            break;
        case EscapeBehavior::Action::Ignore:
            break;
        }
    }

    uint32_t currentShortcutModifiers() const {
        uint32_t modifiers = ctrlIsPressing ? ShortcutConfig::MODIFIER_CONTROL : 0;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            modifiers |= ShortcutConfig::MODIFIER_SHIFT;
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            modifiers |= ShortcutConfig::MODIFIER_ALT;
        return modifiers;
    }

    void panByKeyboard(int deltaX, int deltaY) {
        const int displayWidth = (curPar.rotation == 0 || curPar.rotation == 2) ?
            curPar.width : curPar.height;
        const int displayHeight = (curPar.rotation == 0 || curPar.rotation == 2) ?
            curPar.height : curPar.width;
        const int targetXMax = static_cast<int>(displayWidth * curPar.zoomTarget /
            2 / curPar.ZOOM_BASE);
        const int targetYMax = static_cast<int>(displayHeight * curPar.zoomTarget /
            2 / curPar.ZOOM_BASE);
        curPar.slideTarget.x = std::clamp(curPar.slideTarget.x + deltaX,
            -targetXMax, targetXMax);
        curPar.slideTarget.y = std::clamp(curPar.slideTarget.y + deltaY,
            -targetYMax, targetYMax);
        smoothShift = true;
    }

    bool dispatchConfiguredShortcut(WPARAM keyValue) {
        const uint32_t modifiers = currentShortcutModifiers();
        const auto* storage = GlobalVar::settingParameter.reserve;
        std::optional<ShortcutConfig::Action> matched;
        for (uint32_t index = 0;
            index < static_cast<uint32_t>(ShortcutConfig::Action::Count); ++index) {
            const auto action = static_cast<ShortcutConfig::Action>(index);
            if (ShortcutConfig::matches(
                ShortcutConfig::getBinding(storage, action),
                static_cast<uint32_t>(keyValue), modifiers)) {
                matched = action;
                break;
            }
        }
        if (!matched)
            return false;

        const int panStep = std::max(1, (winHeight + winWidth) / 16);
        switch (*matched) {
        case ShortcutConfig::Action::OpenFile: {
            openImageFromDialog();
        } break;
        case ShortcutConfig::Action::ExportFrames: {
            auto& frames = curPar.imageAssetPtr->frames;
            if (frames.empty())
                break;
            if (IDYES == MessageBoxW(m_hWnd,
                std::format(L"{}{}", getUIStringW(5), frames.size()).c_str(),
                getUIStringW(6), MB_YESNO | MB_ICONQUESTION)) {
                std::thread saveThread([](std::wstring filePath,
                    std::shared_ptr<ImageAsset> imageAssetPtr) {
                        auto& sourceFrames = imageAssetPtr->frames;
                        auto dotIdx = filePath.find_last_of(L".");
                        if (dotIdx == std::string::npos)
                            dotIdx = filePath.size();
                        for (int index = 0; index < sourceFrames.size(); ++index) {
                            std::vector<uchar> buffer;
                            if (cv::imencode(".png", sourceFrames[index], buffer)) {
                                std::ofstream file(std::format(L"{}_{:04d}.png",
                                    filePath.substr(0, dotIdx), index + 1), std::ios::binary);
                                if (file.is_open())
                                    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                            }
                        }
                    }, imgFileList[curFileIdx], curPar.imageAssetPtr);
                saveThread.detach();
            }
            ctrlIsPressing = false;
        } break;
        case ShortcutConfig::Action::CopyImage: {
            cv::Mat source = (curPar.imageAssetPtr->format == ImageFormat::None ||
                curPar.imageAssetPtr->format == ImageFormat::Still) ?
                curPar.imageAssetPtr->primaryFrame :
                curPar.imageAssetPtr->frames[curPar.curFrameIdx];
            jarkUtils::copyImageToClipboard(source);
            ctrlIsPressing = false;
        } break;
        case ShortcutConfig::Action::PrintImage:
            operateQueue.push({ ActionENUM::printImage });
            ctrlIsPressing = false;
            break;
        case ShortcutConfig::Action::CloseViewer:
            operateQueue.push({ ActionENUM::requestExit });
            ctrlIsPressing = false;
            break;
        case ShortcutConfig::Action::PreviousFrame:
            if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause) {
                if (--curPar.curFrameIdx < 0)
                    curPar.curFrameIdx = curPar.curFrameIdxMax;
                operateQueue.push({ ActionENUM::refresh });
            }
            break;
        case ShortcutConfig::Action::ToggleAnimation:
            if (curPar.imageAssetPtr->format == ImageFormat::Animated) {
                curPar.isAnimationPause = !curPar.isAnimationPause;
                operateQueue.push({ ActionENUM::refresh });
            }
            break;
        case ShortcutConfig::Action::NextFrame:
            if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause) {
                if (++curPar.curFrameIdx > curPar.curFrameIdxMax)
                    curPar.curFrameIdx = 0;
                operateQueue.push({ ActionENUM::refresh });
            }
            break;
        case ShortcutConfig::Action::CopyImageInfo:
            jarkUtils::copyToClipboard(jarkUtils::utf8ToWstring(curPar.imageAssetPtr->exifInfo));
            break;
        case ShortcutConfig::Action::ToggleFullscreen:
            if (presentationMode)
                exitPresentationMode();
            else
                jarkUtils::ToggleFullScreen(m_hWnd);
            break;
        case ShortcutConfig::Action::RotateLeft:
            operateQueue.push({ ActionENUM::rotateLeft });
            break;
        case ShortcutConfig::Action::RotateRight:
            operateQueue.push({ ActionENUM::rotateRight });
            break;
        case ShortcutConfig::Action::PanUp:
            panByKeyboard(0, panStep);
            break;
        case ShortcutConfig::Action::PanDown:
            panByKeyboard(0, -panStep);
            break;
        case ShortcutConfig::Action::PanLeft:
            panByKeyboard(panStep, 0);
            break;
        case ShortcutConfig::Action::PanRight:
            panByKeyboard(-panStep, 0);
            break;
        case ShortcutConfig::Action::ZoomIn:
            operateQueue.push({ ActionENUM::zoomIn });
            break;
        case ShortcutConfig::Action::ZoomOut:
            operateQueue.push({ ActionENUM::zoomOut });
            break;
        case ShortcutConfig::Action::ZoomFit:
            operateQueue.push({ ActionENUM::zoomFix });
            break;
        case ShortcutConfig::Action::PreviousImage:
            operateQueue.push({ ActionENUM::preImg });
            break;
        case ShortcutConfig::Action::NextImage:
            operateQueue.push({ ActionENUM::nextImg });
            break;
        case ShortcutConfig::Action::FirstImage:
            operateQueue.push({ ActionENUM::firstImg });
            break;
        case ShortcutConfig::Action::LastImage:
            operateQueue.push({ ActionENUM::finalImg });
            break;
        case ShortcutConfig::Action::PlayPause:
            if (curPar.imageAssetPtr->format == ImageFormat::Still &&
                !curPar.imageAssetPtr->frames.empty()) {
                curPar.imageAssetPtr->format = ImageFormat::Animated;
                initCurrentImageParameters();
                operateQueue.push({ ActionENUM::refresh });
            }
            else if (curPar.imageAssetPtr->format == ImageFormat::Animated) {
                curPar.isAnimationPause = !curPar.isAnimationPause;
                operateQueue.push({ ActionENUM::refresh });
            }
            else {
                operateQueue.push({ ActionENUM::nextImg });
            }
            break;
        case ShortcutConfig::Action::ToggleImageInfo:
            operateQueue.push({ ActionENUM::toggleExif });
            break;
        case ShortcutConfig::Action::OpenSettings:
            operateQueue.push({ ActionENUM::setting, 0 });
            break;
        case ShortcutConfig::Action::RenameImage:
            renameCurrentImage();
            break;
        case ShortcutConfig::Action::OpenShortcuts:
            operateQueue.push({ ActionENUM::setting, 2 });
            break;
        case ShortcutConfig::Action::OpenAbout:
            operateQueue.push({ ActionENUM::setting, 3 });
            break;
        case ShortcutConfig::Action::DeleteImage:
            operateQueue.push({ ActionENUM::deleteImg });
            break;
        case ShortcutConfig::Action::Count:
            return false;
        }
        return true;
    }

    bool isEnabledLegacyAlias(WPARAM keyValue) const {
        const auto* storage = GlobalVar::settingParameter.reserve;
        const auto stillDefault = [&](ShortcutConfig::Action action) {
            const auto index = ShortcutConfig::actionIndex(action);
            return ShortcutConfig::getBinding(storage, action) ==
                ShortcutConfig::DEFAULT_BINDINGS[index];
        };
        return (keyValue == VK_F11 && stillDefault(ShortcutConfig::Action::ToggleFullscreen)) ||
            (keyValue == VK_NUMPAD5 && stillDefault(ShortcutConfig::Action::ZoomFit)) ||
            (keyValue == VK_PRIOR && stillDefault(ShortcutConfig::Action::PreviousImage)) ||
            (keyValue == VK_NEXT && stillDefault(ShortcutConfig::Action::NextImage)) ||
            (keyValue == VK_TAB && stillDefault(ShortcutConfig::Action::ToggleImageInfo));
    }

    void OnKeyDown(WPARAM keyValue) override {
        if (zoomTextEditing) {
            if (keyValue == VK_RETURN) {
                commitZoomTextEdit();
                return;
            }
            if (keyValue == VK_ESCAPE) {
                cancelZoomTextEdit();
                return;
            }
            if (keyValue == VK_BACK) {
                if (zoomEditReplaceSelection) {
                    zoomEditText.clear();
                    zoomEditReplaceSelection = false;
                }
                else if (!zoomEditText.empty()) {
                    zoomEditText.pop_back();
                }
                operateQueue.push({ ActionENUM::refresh });
                return;
            }
            if (keyValue == VK_CONTROL) {
                ctrlIsPressing = true;
                return;
            }
            if (ctrlIsPressing && keyValue == 'A') {
                zoomEditReplaceSelection = true;
                operateQueue.push({ ActionENUM::refresh });
                return;
            }

            char digit = '\0';
            if (keyValue >= '0' && keyValue <= '9')
                digit = static_cast<char>(keyValue);
            else if (keyValue >= VK_NUMPAD0 && keyValue <= VK_NUMPAD9)
                digit = static_cast<char>('0' + keyValue - VK_NUMPAD0);
            if (digit != '\0') {
                if (ZoomEditPolicy::appendDigit(
                    zoomEditText, digit, zoomEditReplaceSelection)) {
                    zoomEditReplaceSelection = false;
                    operateQueue.push({ ActionENUM::refresh });
                }
                return;
            }

            // While the inline field owns keyboard focus, do not let unrelated
            // viewer shortcuts rotate, browse, or close the current image.
            return;
        }

        if (keyValue == VK_CONTROL) {
            ctrlIsPressing = true;
            return;
        }
        if (keyValue == VK_ESCAPE) {
            handleEscapeKey();
            return;
        }
        if (dispatchConfiguredShortcut(keyValue))
            return;
        // Retain familiar secondary aliases while their primary action remains
        // at its default. Customizing that action removes the aliases as well.
        if (!isEnabledLegacyAlias(keyValue))
            return;

        if (ctrlIsPressing) {
            switch (keyValue)
            {
            case 'O': { // Ctrl + O  打开图片
                openImageFromDialog();
            }break;

            case 'S': { // Ctrl + S  动图或实况图视频 批量保存每一帧到png图片
                auto& frames = curPar.imageAssetPtr->frames;
                if (frames.empty())
                    break;

                if (IDYES == MessageBoxW(
                    m_hWnd,
                    std::format(L"{}{}", getUIStringW(5), frames.size()).c_str(),
                    getUIStringW(6),
                    MB_YESNO | MB_ICONQUESTION
                )) {
                    std::thread saveThread([](std::wstring filePath, std::shared_ptr<ImageAsset> imageAssetPtr) {
                        auto& frames = imageAssetPtr->frames;
                        auto dotIdx = filePath.find_last_of(L".");
                        if (dotIdx == string::npos)
                            dotIdx = filePath.size();

                        for (int i = 0; i < frames.size(); i++) {
                            std::vector<uchar> buffer;
                            if (cv::imencode(".png", frames[i], buffer)) {
                                std::ofstream file(std::format(L"{}_{:04d}.png", filePath.substr(0, dotIdx), i + 1), std::ios::binary);
                                if (file.is_open()) {
                                    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                                    file.close();
                                }
                            }
                        }
                        }, imgFileList[curFileIdx], curPar.imageAssetPtr);

                    saveThread.detach();
                }
                ctrlIsPressing = false; // 上面弹出窗口导致收不到CTRL键释放的消息
            }break;

            case 'C': { // Ctrl + C  复制到剪贴板
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                jarkUtils::copyImageToClipboard(srcImg);
                ctrlIsPressing = false;
            }break;

            case 'P': { // Ctrl + P 打印
                operateQueue.push({ ActionENUM::printImage });
                ctrlIsPressing = false;
            }break;

            case 'W': { // Ctrl + W 退出
                operateQueue.push({ ActionENUM::requestExit });
                ctrlIsPressing = false;
            }break;
            }
        }
        else {
            switch (keyValue)
            {

            case 'J': { // 上一帧
                if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause) {
                    curPar.curFrameIdx--;
                    if (curPar.curFrameIdx < 0)
                        curPar.curFrameIdx = curPar.curFrameIdxMax;
                    operateQueue.push({ ActionENUM::refresh });
                }
            }break;

            case 'K': { // 动图 暂停/继续
                if (curPar.imageAssetPtr->format == ImageFormat::Animated) {
                    curPar.isAnimationPause = !curPar.isAnimationPause;
                    operateQueue.push({ ActionENUM::refresh });
                }
            }break;

            case 'L': { // 下一帧
                if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause) {
                    curPar.curFrameIdx++;
                    if (curPar.curFrameIdx > curPar.curFrameIdxMax)
                        curPar.curFrameIdx = 0;
                    operateQueue.push({ ActionENUM::refresh });
                }
            }break;

            case VK_CONTROL: {
                ctrlIsPressing = true;
            }break;

            case 'C': { // 复制图像信息到剪贴板
                jarkUtils::copyToClipboard(jarkUtils::utf8ToWstring(curPar.imageAssetPtr->exifInfo));
            }break;

            case 'F':
            case VK_F11: {
                if (presentationMode)
                    exitPresentationMode();
                else
                    jarkUtils::ToggleFullScreen(m_hWnd);
            }break;

            case 'Q': {
                operateQueue.push({ ActionENUM::rotateLeft });
            }break;

            case 'E': {
                operateQueue.push({ ActionENUM::rotateRight });
            }break;

            case 'W': {
                const int newTargetYMax = (int)(((curPar.rotation == 0 or curPar.rotation == 2) ?
                    curPar.height : curPar.width) * curPar.zoomTarget / 2 / curPar.ZOOM_BASE);
                int newTargetY = curPar.slideTarget.y + ((winHeight + winWidth) / 16);
                newTargetY = std::clamp(newTargetY, -newTargetYMax, newTargetYMax);
                curPar.slideTarget.y = newTargetY;
                smoothShift = true;
            }break;

            case 'S': {
                const int newTargetYMax = (int)(((curPar.rotation == 0 or curPar.rotation == 2) ?
                    curPar.height : curPar.width) * curPar.zoomTarget / 2 / curPar.ZOOM_BASE);
                int newTargetY = curPar.slideTarget.y - ((winHeight + winWidth) / 16);
                newTargetY = std::clamp(newTargetY, -newTargetYMax, newTargetYMax);
                curPar.slideTarget.y = newTargetY;
                smoothShift = true;
            }break;

            case 'A': {
                const int newTargetXMax = (int)(((curPar.rotation == 0 || curPar.rotation == 2) ?
                    curPar.width : curPar.height) * curPar.zoomTarget / 2 / curPar.ZOOM_BASE);
                int newTargetX = curPar.slideTarget.x + ((winHeight + winWidth) / 16);
                newTargetX = std::clamp(newTargetX, -newTargetXMax, newTargetXMax);
                curPar.slideTarget.x = newTargetX;
                smoothShift = true;
            }break;

            case 'D': {
                const int newTargetXMax = (int)(((curPar.rotation == 0 || curPar.rotation == 2) ?
                    curPar.width : curPar.height) * curPar.zoomTarget / 2 / curPar.ZOOM_BASE);
                int newTargetX = curPar.slideTarget.x - ((winHeight + winWidth) / 16);
                newTargetX = std::clamp(newTargetX, -newTargetXMax, newTargetXMax);
                curPar.slideTarget.x = newTargetX;
                smoothShift = true;
            }break;

            case VK_UP: {
                operateQueue.push({ ActionENUM::zoomIn });
            }break;

            case VK_DOWN: {
                operateQueue.push({ ActionENUM::zoomOut });
            }break;

            case '5':
            case VK_NUMPAD5: {
                operateQueue.push({ ActionENUM::zoomFix });
            }break;

            case VK_PRIOR:
            case VK_LEFT: {
                operateQueue.push({ ActionENUM::preImg });
            }break;

            case VK_NEXT:
            case VK_RIGHT: {
                operateQueue.push({ ActionENUM::nextImg });
            }break;

            case VK_HOME: {
                operateQueue.push({ ActionENUM::firstImg });
            }break;

            case VK_END: {
                operateQueue.push({ ActionENUM::finalImg });
            }break;

            case VK_SPACE: {
                if (curPar.imageAssetPtr->format == ImageFormat::Still && !curPar.imageAssetPtr->frames.empty()) {
                    curPar.imageAssetPtr->format = ImageFormat::Animated;
                    initCurrentImageParameters();
                    operateQueue.push({ ActionENUM::refresh });
                }
                else if (curPar.imageAssetPtr->format == ImageFormat::Animated) {
                    curPar.isAnimationPause = !curPar.isAnimationPause;
                    operateQueue.push({ ActionENUM::refresh });
                }
                else {
                    operateQueue.push({ ActionENUM::nextImg });
                }
            }break;

            case VK_TAB:
            case 'I': {
                operateQueue.push({ ActionENUM::toggleExif });
            }break;

            case VK_F1: {
                operateQueue.push({ ActionENUM::setting, 0 });
            }break;

            case VK_F2: {
                renameCurrentImage();
            }break;

            case VK_F3: {
                operateQueue.push({ ActionENUM::setting, 2 });
            }break;

            case VK_F4: {
                operateQueue.push({ ActionENUM::setting, 3 });
            }break;

            case VK_ESCAPE: { // ESC
                handleEscapeKey();
            }break;

            case VK_DELETE: { //DELETE
                operateQueue.push({ ActionENUM::deleteImg });
            }

            default: {
                JARK_LOG("{} KeyValue: 0x{:04x}", __FUNCTION__, (uint64_t)keyValue);
            }break;
            }
        }
    }

    void OnKeyUp(WPARAM keyValue) override {
        switch (keyValue)
        {
        case VK_CONTROL: {
            ctrlIsPressing = false;
        }break;

        default: {
            JARK_LOG("{} KeyValue: 0x{:04x}", __FUNCTION__, (uint64_t)keyValue);
        }break;
        }
    }

    void OnDropFiles(WPARAM wParam) override {
        wstring path;
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

        if (0 < fileCount) { // 拖入多文件时，只接受第一个
            wchar_t filePath[4096] = {};
            DragQueryFileW(hDrop, 0, filePath, 4096);
            path = filePath;
        }
        DragFinish(hDrop);

        if (!path.empty()) {
            initOpenFile(path);
            operateQueue.push({ ActionENUM::refresh });
        }
    }

    void OnContextMenuCommand(WPARAM wParam) override {
        const int commandId = static_cast<int>(LOWORD(wParam));
        const int firstEditorCommand = static_cast<int>(ContextMenu::editImageFirst);
        const int lastEditorCommand = static_cast<int>(ContextMenu::editImageLast);
        if (commandId >= firstEditorCommand && commandId <= lastEditorCommand) {
            const std::size_t index = static_cast<std::size_t>(
                commandId - firstEditorCommand);
            if (index < GlobalVar::externalEditors.size())
                launchCurrentImageInExternalEditor(GlobalVar::externalEditors[index]);
            return;
        }
        switch ((ContextMenu)wParam) {
        case ContextMenu::openNewImage: {
            openImageFromDialog();
        }break;

        case ContextMenu::copyImageInfo: {
            jarkUtils::copyToClipboard(jarkUtils::utf8ToWstring(curPar.imageAssetPtr->exifInfo));
        }break;

        case ContextMenu::copyImagePath: {
            jarkUtils::copyToClipboard(imgFileList[curFileIdx]);
        }break;

        case ContextMenu::copyImageData: {
            cv::Mat srcImg;
            if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                srcImg = curPar.imageAssetPtr->primaryFrame;
            else
                srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];
            jarkUtils::copyImageToClipboard(srcImg);
        }break;

        case ContextMenu::toggleExifDisplay: {
            operateQueue.push({ ActionENUM::toggleExif });
        }break;

        case ContextMenu::openContainerFloder: {
            jarkUtils::openFileLocation(imgFileList[curFileIdx]);
        }break;

        case ContextMenu::editImageChoose: {
            chooseExternalEditorAndOpenCurrentImage();
        }break;

        case ContextMenu::renameImage: {
            renameCurrentImage();
        }break;

        case ContextMenu::deleteImage: {
            operateQueue.push({ ActionENUM::deleteImg });
        }break;

        case ContextMenu::openFileProperties: {
            jarkUtils::openFileProperties(imgFileList[curFileIdx]);
        }break;

        case ContextMenu::printImage: {
            operateQueue.push({ ActionENUM::printImage });
        }break;

        case ContextMenu::toggleFullScreen: {
            if (presentationMode)
                exitPresentationMode();
            else
                jarkUtils::ToggleFullScreen(m_hWnd);
        }break;

        case ContextMenu::openSetting: {
            operateQueue.push({ ActionENUM::setting, 0 });
        }break;

        case ContextMenu::openHelp: {
            operateQueue.push({ ActionENUM::setting, 2 });
        }break;

        case ContextMenu::aboutSoftware: {
            operateQueue.push({ ActionENUM::setting, 3 });
        }break;

        case ContextMenu::exitSoftware: {
            operateQueue.push({ ActionENUM::requestExit });
        }break;

        case ContextMenu::backgroundTransparent: {
            setBackgroundMode(BackgroundMode::Transparent);
        }break;

        case ContextMenu::backgroundWhite: {
            setBackgroundMode(BackgroundMode::White);
        }break;

        case ContextMenu::backgroundBlack: {
            setBackgroundMode(BackgroundMode::Black);
        }break;

        case ContextMenu::backgroundFrostedGlass: {
            setBackgroundMode(BackgroundMode::FrostedGlass);
        }break;

        default:
            break;
        }
    }

    BackgroundMode currentBackgroundMode() const {
        return BackgroundRenderer::normalizeMode(GlobalVar::settingParameter.backgroundMode);
    }

    void setBackgroundMode(BackgroundMode mode) {
        GlobalVar::settingParameter.backgroundMode = static_cast<uint32_t>(mode);
        ApplyWindowBackgroundMode();
        operateQueue.push({ ActionENUM::refresh });
    }

    uint32_t windowBackgroundPixel() const {
        return BackgroundPolicy::windowCanvasPixel(
            presentationMode, IsFrostedGlassActive(), GlobalVar::currentTheme.BG);
    }

    void fillCanvasBackground(cv::Mat& canvas) const {
        const uint32_t canvasPixel = BackgroundPolicy::windowCanvasPixel(
            presentationMode, IsFrostedGlassActive(), GlobalVar::currentTheme.BG);
        concurrency::parallel_for(0, canvas.rows, [&, canvasPixel](int y) {
            auto row = reinterpret_cast<uint32_t*>(canvas.ptr(y));
            std::fill(row, row + canvas.cols, canvasPixel);
        });
    }

    void OnResize(UINT width, UINT height) override {
        if (width == 0 || height == 0)
            return;

        if (winWidth == width && winHeight == height)
            return;

        winWidth = width;
        winHeight = height;

        if (winWidth != mainCanvas.cols || winHeight != mainCanvas.rows) {
            mainCanvas = cv::Mat(winHeight, winWidth, CV_8UC4);
            CreateWindowSizeDependentResources();
        }

        if (hasInitWinSize) {
            if (presentationMode)
                applyPresentationImageLayout();
            else if (framedWindowAnchored)
                applyAnchoredWindowImageLayout();
            else
                curPar.updateZoomList(winWidth, winHeight);

            cv::Mat srcImg;
            if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                srcImg = curPar.imageAssetPtr->primaryFrame;
            else
                srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

            drawCanvas(srcImg, mainCanvas);
            drawExifInfo(mainCanvas);
        }
        else {
            hasInitWinSize = true;
            initCurrentImageParameters();

            fillCanvasBackground(mainCanvas);
        }

        drawExtraUI(mainCanvas);
        updateMainCanvas();
        operateQueue.push({ ActionENUM::refresh });
    }

    uint32_t getSrcPx1(const cv::Mat& srcImg, int srcX, int srcY) const {
        uchar srcPx = srcImg.at<uchar>(srcY, srcX);
        if (curPar.zoomCur < curPar.ZOOM_BASE && srcY > 0 && srcX > 0) { // 简单临近像素平均
            const uchar px0 = srcImg.at<uchar>(srcY - 1, srcX - 1);
            const uchar px1 = srcImg.at<uchar>(srcY - 1, srcX);
            const uchar px2 = srcImg.at<uchar>(srcY, srcX - 1);
            srcPx = (px0 + px1 + px2 + srcPx) >> 2;
        }
        return srcPx | srcPx << 8 | srcPx << 16 | 255 << 24;
    }

    inline static uint32_t getSrcPx3(const cv::Mat& srcImg, int srcX, int srcY) {
        cv::Vec3b srcPx = srcImg.at<cv::Vec3b>(srcY, srcX);

        if (isLowZoom && srcY > 0 && srcX > 0) { // 简单临近像素平均
            const cv::Vec3b px1 = srcImg.at<cv::Vec3b>(srcY - 1, srcX - 1);
            const cv::Vec3b px2 = srcImg.at<cv::Vec3b>(srcY - 1, srcX);
            const cv::Vec3b px3 = srcImg.at<cv::Vec3b>(srcY, srcX - 1);
            for (int i = 0; i < 3; i++)
                srcPx[i] = (px1[i] + px2[i] + px3[i] + srcPx[i]) >> 2;
        }
        return *((uint32_t*)&srcPx) | (255 << 24);
    }

    inline uint32_t compositeSrcPx4(intUnion srcPx, int mainX, int mainY) const {
        return BackgroundRenderer::compositeBgra(
            srcPx.u32, BackgroundPolicy::imageAreaMode(currentBackgroundMode()),
            IsFrostedGlassActive(), mainX, mainY,
            GlobalVar::currentTheme.BG);
    }

    inline uint32_t getSrcPx4(const cv::Mat& srcImg, int srcX, int srcY, int mainX, int mainY) const {
        // 按行首指针取值：cols 不等于行字节数时（ROI/对齐填充）用 cols 会取错像素，
        // 且 int 乘法在超大图上会溢出，ptr(y) 内部按 size_t 步长计算。
        const intUnion* curRow = (const intUnion*)srcImg.ptr(srcY);

        intUnion srcPx = curRow[srcX];

        if (isLowZoom && srcY > 0 && srcX > 0) { // 简单临近像素平均
            const intUnion* prevRow = (const intUnion*)srcImg.ptr(srcY - 1);
            intUnion px1 = prevRow[srcX - 1];
            intUnion px2 = prevRow[srcX];
            intUnion px3 = curRow[srcX - 1];
            for (int i = 0; i < 4; i++)
                srcPx[i] = (px1[i] + px2[i] + px3[i] + srcPx[i]) >> 2;
        }

        return compositeSrcPx4(srcPx, mainX, mainY);
    }

    void drawSvgCanvas(cv::Mat& canvas) const {
        const auto& renderer = curPar.imageAssetPtr->svgRenderer;
        const int canvasH = canvas.rows;
        const int canvasW = canvas.cols;
        const float scale = (float)curPar.zoomCur / curPar.ZOOM_BASE;
        const float nativeW = renderer->width();
        const float nativeH = renderer->height();

        const bool isQuarterTurn = curPar.rotation == 1 || curPar.rotation == 3;
        const double renderedW = (isQuarterTurn ? nativeH : nativeW) * scale;
        const double renderedH = (isQuarterTurn ? nativeW : nativeH) * scale;
        const int deltaW = curPar.slideCur.x + (int)std::round((canvasW - renderedW) / 2.0);
        const int deltaH = curPar.slideCur.y + (int)std::round((canvasH - renderedH) / 2.0);

        SvgTransform transform;
        switch (curPar.rotation) {
        case 1:
            transform = { 0.0f, -scale, scale, 0.0f,
                (float)deltaW, (float)deltaH + nativeW * scale };
            break;
        case 2:
            transform = { -scale, 0.0f, 0.0f, -scale,
                (float)deltaW + nativeW * scale, (float)deltaH + nativeH * scale };
            break;
        case 3:
            transform = { 0.0f, scale, -scale, 0.0f,
                (float)deltaW + nativeH * scale, (float)deltaH };
            break;
        default:
            transform = { scale, 0.0f, 0.0f, scale, (float)deltaW, (float)deltaH };
            break;
        }

        if (curPar.flipHorizontal) {
            transform.a = -transform.a;
            transform.c = -transform.c;
            transform.e = static_cast<float>(2.0 * deltaW + renderedW) - transform.e;
        }
        if (curPar.flipVertical) {
            transform.b = -transform.b;
            transform.d = -transform.d;
            transform.f = static_cast<float>(2.0 * deltaH + renderedH) - transform.f;
        }

        auto rendered = renderer->renderViewport(canvasW, canvasH, transform);
        fillCanvasBackground(canvas);
        if (rendered.empty()) {
            return;
        }

        cv::Mat viewport(canvasH, canvasW, CV_8UC4, rendered.bgra.data());
        const int xStart = std::clamp(deltaW, 0, canvasW);
        const int yStart = std::clamp(deltaH, 0, canvasH);
        const int xEnd = std::clamp((int)std::ceil(renderedW) + deltaW, 0, canvasW);
        const int yEnd = std::clamp((int)std::ceil(renderedH) + deltaH, 0, canvasH);

        isLowZoom = false;
        concurrency::parallel_for(yStart, yEnd, [&](int y) {
            auto destination = (uint32_t*)canvas.ptr() + (size_t)y * canvasW;
            for (int x = xStart; x < xEnd; ++x) {
                destination[x] = getSrcPx4(viewport, x, y, x, y);
            }
        });

        const uint32_t lineColor = 0xFF808080;
        if (0 < xStart && xStart < canvasW) {
            for (int y = std::max(yStart - 1, 0); y < std::min(yEnd + 1, canvasH); ++y)
                canvas.at<uint32_t>(y, xStart - 1) = lineColor;
        }
        if (0 < xEnd && xEnd < canvasW) {
            for (int y = std::max(yStart - 1, 0); y < std::min(yEnd + 1, canvasH); ++y)
                canvas.at<uint32_t>(y, xEnd) = lineColor;
        }
        if (0 < yStart && yStart < canvasH) {
            for (int x = xStart; x < xEnd; ++x)
                canvas.at<uint32_t>(yStart - 1, x) = lineColor;
        }
        if (0 < yEnd && yEnd < canvasH) {
            for (int x = xStart; x < xEnd; ++x)
                canvas.at<uint32_t>(yEnd, x) = lineColor;
        }
    }

    void drawCanvas(const cv::Mat& srcImg, cv::Mat& canvas) const {
        if (curPar.imageAssetPtr && curPar.imageAssetPtr->svgRenderer &&
            srcImg.data == curPar.imageAssetPtr->primaryFrame.data) {
            drawSvgCanvas(canvas);
            return;
        }

        int srcH, srcW;
        if (curPar.rotation == 0 || curPar.rotation == 2) {
            srcH = srcImg.rows;
            srcW = srcImg.cols;
        }
        else {
            srcH = srcImg.cols;
            srcW = srcImg.rows;
        }

        const int canvasH = canvas.rows;
        const int canvasW = canvas.cols;

        if (srcH <= 0 || srcW <= 0)
            return;

        // 源图和画板canvas均100%缩放且居中重合，此时随机取一个点，先只考虑水平方向
        // 该点与画板中心的距离，等于该点与源图中心的距离
        // 即 canvasW / 2 - x = srcW / 2 - srcX
        // 再考虑偏移量：canvasW / 2 - x = srcW / 2 - srcX - slide * srcW
        // 再考虑源图缩放：canvasW / 2 - x = (srcW / 2 - srcX - slide * srcW) * zoom
        // 即为源图和画板在特定位移和缩放的坐标变换公式
        // x = canvasW / 2.0 - (srcW / 2.0 - srcX - slide * srcW) * zoom
        // srcX = srcW / 2.0 - ((canvasW / 2.0 - x) / zoom + slide * srcW)

        const double renderedW = (double)srcW * curPar.zoomCur / curPar.ZOOM_BASE;
        const double renderedH = (double)srcH * curPar.zoomCur / curPar.ZOOM_BASE;
        const int deltaW = curPar.slideCur.x + (int)std::round((canvasW - renderedW) / 2.0);
        const int deltaH = curPar.slideCur.y + (int)std::round((canvasH - renderedH) / 2.0);

        int xStart = deltaW < 0 ? 0 : deltaW;
        int yStart = deltaH < 0 ? 0 : deltaH;
        int xEnd = (int)std::round(renderedW) + deltaW;
        int yEnd = (int)std::round(renderedH) + deltaH;
        if (xEnd > canvasW) xEnd = canvasW;
        if (yEnd > canvasH) yEnd = canvasH;

        fillCanvasBackground(canvas);

        if (((srcH == 600 and srcW == 800) or (srcH == 800 and srcW == 600)) and 
            (*((uint32_t*)srcImg.ptr()) == deepTheme.BG) or (*((uint32_t*)srcImg.ptr()) == lightTheme.BG)) {
            // 内置的用于提示的图像
        }
        else { // 普通图像  画边框
            const uint32_t lineColor = 0xFF808080;
            if (0 < xStart and xStart < canvasW) {
                const int yMax = std::min(yEnd + 1, canvasH);
                for (int y = std::max(yStart - 1, 0); y < yMax; y++) {
                    ((uint32_t*)canvas.ptr())[y * canvasW + xStart - 1] = lineColor;
                }
            }
            if (0 < xEnd and xEnd < canvasW) {
                const int yMax = std::min(yEnd + 1, canvasH);
                for (int y = std::max(yStart - 1, 0); y < yMax; y++) {
                    ((uint32_t*)canvas.ptr())[y * canvasW + xEnd] = lineColor;
                }
            }

            if (0 < yStart and yStart < canvasH) {
                for (int x = xStart; x < xEnd; x++) {
                    ((uint32_t*)canvas.ptr())[(yStart - 1) * canvasW + x] = lineColor;
                }
            }
            if (0 < yEnd and yEnd < canvasH) {
                for (int x = xStart; x < xEnd; x++) {
                    ((uint32_t*)canvas.ptr())[yEnd * canvasW + x] = lineColor;
                }
            }
        }

        const float zoomInvert = (float)curPar.ZOOM_BASE / curPar.zoomCur;
        isLowZoom = curPar.zoomCur < curPar.ZOOM_BASE;

        if (curPar.zoomCur > curPar.ZOOM_BASE &&
            (srcImg.channels() == 1 || srcImg.channels() == 3 || srcImg.channels() == 4)) {
            const int channels = srcImg.channels();
            concurrency::parallel_for(yStart, yEnd, [&](int y) {
                auto destination = ((uint32_t*)canvas.ptr()) + (size_t)y * canvasW;
                const float rotatedY = ((y - deltaH) + 0.5f) * zoomInvert - 0.5f;

                for (int x = xStart; x < xEnd; ++x) {
                    const float rotatedX = ((x - deltaW) + 0.5f) * zoomInvert - 0.5f;
                    const auto source = ImageViewTransform::displayToSource(
                        rotatedX, rotatedY, srcW, srcH, curPar.rotation,
                        curPar.flipHorizontal, curPar.flipVertical);

                    intUnion sampled;
                    sampled.u32 = ImageInterpolation::sampleBilinearBgra(
                        srcImg.ptr(), srcImg.cols, srcImg.rows, srcImg.step,
                        channels, source.x, source.y);
                    destination[x] = channels == 4 ?
                        compositeSrcPx4(sampled, x, y) : sampled.u32;
                }
            });
            return;
        }

        switch (srcImg.type()) {
        case CV_8UC4: {
            concurrency::parallel_for(yStart, yEnd, [&](int y) {
                auto ptr = ((uint32_t*)canvas.ptr()) + y * canvasW;
                //int srcY = (int)((int64_t)(y - deltaH) * curPar.ZOOM_BASE / curPar.zoomCur); // 2K屏 50%缩放一帧34ms 100%缩放一帧14ms
                int srcY = (int)((y - deltaH) * zoomInvert); // 快一点  2K屏 50%缩放一帧28ms 100%缩放一帧14ms

                srcY = std::clamp(srcY, 0, srcH - 1);

                for (int x = xStart; x < xEnd; x++) {
                    int srcX = (int)((x - deltaW) * zoomInvert);
                    srcX = std::clamp(srcX, 0, srcW - 1);
                    const auto source = ImageViewTransform::displayToSource(
                        srcX, srcY, srcW, srcH, curPar.rotation,
                        curPar.flipHorizontal, curPar.flipVertical);
                    ptr[x] = getSrcPx4(srcImg, source.x, source.y, x, y);
                }
            });
        }break;

        case CV_8UC3: {
            concurrency::parallel_for(yStart, yEnd, [&](int y) {
                auto ptr = ((uint32_t*)canvas.ptr()) + y * canvasW;
                //int srcY = (int)((int64_t)(y - deltaH) * curPar.ZOOM_BASE / curPar.zoomCur);
                int srcY = (int)((y - deltaH) * zoomInvert);

                srcY = std::clamp(srcY, 0, srcH - 1);

                for (int x = xStart; x < xEnd; x++) {
                    int srcX = (int)((x - deltaW) * zoomInvert);
                    srcX = std::clamp(srcX, 0, srcW - 1);
                    const auto source = ImageViewTransform::displayToSource(
                        srcX, srcY, srcW, srcH, curPar.rotation,
                        curPar.flipHorizontal, curPar.flipVertical);
                    ptr[x] = getSrcPx3(srcImg, source.x, source.y);
                }
            });
        }break;

        case CV_8UC1: {
            concurrency::parallel_for(yStart, yEnd, [&](int y) {
                auto ptr = ((uint32_t*)canvas.ptr()) + y * canvasW;
                //int srcY = (int)((int64_t)(y - deltaH) * curPar.ZOOM_BASE / curPar.zoomCur);
                int srcY = (int)((y - deltaH) * zoomInvert);

                srcY = std::clamp(srcY, 0, srcH - 1);

                for (int x = xStart; x < xEnd; x++) {
                    int srcX = (int)((x - deltaW) * zoomInvert);
                    srcX = std::clamp(srcX, 0, srcW - 1);
                    const auto source = ImageViewTransform::displayToSource(
                        srcX, srcY, srcW, srcH, curPar.rotation,
                        curPar.flipHorizontal, curPar.flipVertical);
                    ptr[x] = getSrcPx1(srcImg, source.x, source.y);
                }
            });
        }break;
        }
    }

    cv::Mat rotateImage(const cv::Mat& image, double angle) {
        int width = image.cols;
        int height = image.rows;
        cv::Point2f center(width / 2.0f, height / 2.0f);

        cv::Mat rotationMatrix = cv::getRotationMatrix2D(center, angle, 1.0);

        intUnion background{ windowBackgroundPixel() };
        cv::Mat rotatedImage;
        cv::warpAffine(image, rotatedImage, rotationMatrix, image.size(),
            cv::INTER_LINEAR, cv::BORDER_CONSTANT, 
            cv::Scalar(background[0], background[1], background[2], background[3]));

        return rotatedImage;
    }

    // 取对角线作为新画布的宽高，绘制好内容再旋转，最后截取画布。
    // 若直接使用原尺寸画布进行旋转，宽或高其中较小的那边旋转到较长那边时，会缺失部分内容
    void rotateLeftAnimation() {
        using namespace std::chrono;

        int maxEdge = (int)std::ceil(std::sqrt(winWidth * winWidth + winHeight * winHeight));
        if (maxEdge < 2)
            return;
        auto tmpCanvas = cv::Mat(maxEdge, maxEdge, CV_8UC4);
        fillCanvasBackground(tmpCanvas);

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        drawCanvas(srcImg, tmpCanvas);
        cv::resize(tmpCanvas, tmpCanvas, cv::Size(tmpCanvas.cols / 2, tmpCanvas.cols / 2));

        for (int i = 0; i <= 90; i += ((100 - i) / 6)) {
            auto start_clock = steady_clock::now();
            auto view = rotateImage(tmpCanvas, i)(cv::Rect((maxEdge - winWidth) / 4, (maxEdge - winHeight) / 4, winWidth / 2, winHeight / 2));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExifInfo(mainCanvas);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }

    void rotateRightAnimation() {
        using namespace std::chrono;

        int maxEdge = (int)std::ceil(std::sqrt(winWidth * winWidth + winHeight * winHeight));
        if (maxEdge < 2)
            return;
        auto tmpCanvas = cv::Mat(maxEdge, maxEdge, CV_8UC4);
        fillCanvasBackground(tmpCanvas);

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        drawCanvas(srcImg, tmpCanvas);
        cv::resize(tmpCanvas, tmpCanvas, cv::Size(tmpCanvas.cols / 2, tmpCanvas.cols / 2));

        for (int i = 0; i >= -90; i -= ((100 + i) / 6)) {
            auto start_clock = steady_clock::now();
            auto view = rotateImage(tmpCanvas, i)(cv::Rect((maxEdge - winWidth) / 4, (maxEdge - winHeight) / 4, winWidth / 2, winHeight / 2));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExifInfo(mainCanvas);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }

    // 添加水平运动模糊
    void applyHorizontalMotionBlur(cv::Mat& src, cv::Mat& dst, int kernelSize = 15, int direction = 1) {
        // 创建水平运动模糊核 (1行 x kernelSize列)
        cv::Mat kernel = cv::Mat::zeros(1, kernelSize, CV_32F);

        // 设置方向 (1.0 = 右移模糊, -1.0 = 左移模糊)
        int start = (direction > 0) ? 0 : kernelSize - 1;
        int end = (direction > 0) ? kernelSize : -1;
        int step = (direction > 0) ? 1 : -1;

        // 填充核 (线性衰减效果)
        float sum = 0.0f;
        for (int i = start; i != end; i += step) {
            float weight = 1.0f - std::abs(static_cast<float>(i - start) / (kernelSize - 1));
            kernel.at<float>(0, i) = weight;
            sum += weight;
        }
        // 归一化核
        kernel /= sum;
        // 应用水平卷积 (仅水平方向)
        cv::filter2D(src, dst, -1, kernel, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
    }

    // 添加竖直运动模糊
    void applyVerticalMotionBlur(cv::Mat& src, cv::Mat& dst, int kernelSize = 15, float direction = 1.0f) {
        // 创建水平运动模糊核 (1行 x kernelSize列)
        cv::Mat kernel = cv::Mat::zeros(kernelSize, 1, CV_32F);

        // 设置方向 (1.0 = 下移模糊, -1.0 = 上移模糊)
        int start = (direction > 0) ? 0 : kernelSize - 1;
        int end = (direction > 0) ? kernelSize : -1;
        int step = (direction > 0) ? 1 : -1;

        // 填充核 (线性衰减效果)
        float sum = 0.0f;
        for (int i = start; i != end; i += step) {
            float weight = 1.0f - std::abs(static_cast<float>(i - start) / (kernelSize - 1));
            kernel.at<float>(i, 0) = weight;
            sum += weight;
        }
        // 归一化核
        kernel /= sum;
        // 应用水平卷积 (仅水平方向)
        cv::filter2D(src, dst, -1, kernel, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
    }

    // 水平滑动 上一张图
    void mainCanvasSlideToPreAnimationHorizontal() {
        using namespace std::chrono;

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        auto nextmainCanvas = cv::Mat(mainCanvas.size(), mainCanvas.type());
        drawCanvas(srcImg, nextmainCanvas);
        drawExifInfo(nextmainCanvas);

        cv::Mat smallMainCanvas;
        cv::resize(mainCanvas, smallMainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);
        cv::resize(nextmainCanvas, nextmainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);

        cv::Mat panorama(nextmainCanvas.rows, nextmainCanvas.cols * 2, nextmainCanvas.type());
        nextmainCanvas.copyTo(panorama(cv::Rect(0, 0, smallMainCanvas.cols, smallMainCanvas.rows)));
        smallMainCanvas.copyTo(panorama(cv::Rect(smallMainCanvas.cols, 0, nextmainCanvas.cols, nextmainCanvas.rows)));

        cv::Mat blurred;
        applyHorizontalMotionBlur(panorama, blurred, 15, 1);
        panorama = blurred;

        const int frame_width = nextmainCanvas.cols;
        const int frame_height = nextmainCanvas.rows;
        for (int x = frame_width; x > 0; x -= (int)((frame_width * 1.5 - x) / 8)) {
            auto start_clock = steady_clock::now();

            cv::Mat view = panorama(cv::Rect(x, 0, frame_width, frame_height));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }

    // 水平滑动 下一张图
    void mainCanvasSlideToNextAnimationHorizontal() {
        using namespace std::chrono;

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        auto nextmainCanvas = cv::Mat(mainCanvas.size(), mainCanvas.type());
        drawCanvas(srcImg, nextmainCanvas);
        drawExifInfo(nextmainCanvas);

        cv::Mat smallMainCanvas;
        cv::resize(mainCanvas, smallMainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);
        cv::resize(nextmainCanvas, nextmainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);

        cv::Mat panorama(nextmainCanvas.rows, nextmainCanvas.cols * 2, nextmainCanvas.type());
        smallMainCanvas.copyTo(panorama(cv::Rect(0, 0, smallMainCanvas.cols, smallMainCanvas.rows)));
        nextmainCanvas.copyTo(panorama(cv::Rect(smallMainCanvas.cols, 0, nextmainCanvas.cols, nextmainCanvas.rows)));

        cv::Mat blurred;
        applyHorizontalMotionBlur(panorama, blurred, 15, -1);
        panorama = blurred;

        const int frame_width = nextmainCanvas.cols;
        const int frame_height = nextmainCanvas.rows;
        for (int x = 0; x <= frame_width; x += (int)((frame_width*1.5 - x) / 8)) {
            auto start_clock = steady_clock::now();

            cv::Mat view = panorama(cv::Rect(x, 0, frame_width, frame_height));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }

    // 竖直滑动 上一张图
    void mainCanvasSlideToPreAnimationVertical() {
        using namespace std::chrono;

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        auto nextmainCanvas = cv::Mat(mainCanvas.size(), mainCanvas.type());
        drawCanvas(srcImg, nextmainCanvas);
        drawExifInfo(nextmainCanvas);

        cv::Mat smallMainCanvas;
        cv::resize(mainCanvas, smallMainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);
        cv::resize(nextmainCanvas, nextmainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);

        cv::Mat panorama(nextmainCanvas.rows * 2, nextmainCanvas.cols, nextmainCanvas.type());
        nextmainCanvas.copyTo(panorama(cv::Rect(0, 0, smallMainCanvas.cols, smallMainCanvas.rows)));
        smallMainCanvas.copyTo(panorama(cv::Rect(0, smallMainCanvas.rows, nextmainCanvas.cols, nextmainCanvas.rows)));

        cv::Mat blurred;
        applyVerticalMotionBlur(panorama, blurred, 15, 1);
        panorama = blurred;

        const int frame_width = nextmainCanvas.cols;
        const int frame_height = nextmainCanvas.rows;
        for (int y = frame_height; y >= 0; y -= (int)((frame_height * 1.5 - y) / 8)) {
            auto start_clock = steady_clock::now();

            cv::Mat view = panorama(cv::Rect(0, y, frame_width, frame_height));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }
    
    // 竖直滑动 下一张图
    void mainCanvasSlideToNextAnimationVertical() {
        using namespace std::chrono;

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
            srcImg = curPar.imageAssetPtr->primaryFrame;
        else
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

        auto nextmainCanvas = cv::Mat(mainCanvas.size(), mainCanvas.type());
        drawCanvas(srcImg, nextmainCanvas);
        drawExifInfo(nextmainCanvas);

        cv::Mat smallMainCanvas;
        cv::resize(mainCanvas, smallMainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);
        cv::resize(nextmainCanvas, nextmainCanvas, cv::Size(winWidth / 4, winHeight / 4), 0, 0, cv::INTER_NEAREST);

        cv::Mat panorama(nextmainCanvas.rows*2, nextmainCanvas.cols, nextmainCanvas.type());
        smallMainCanvas.copyTo(panorama(cv::Rect(0, 0, smallMainCanvas.cols, smallMainCanvas.rows)));
        nextmainCanvas.copyTo(panorama(cv::Rect(0, smallMainCanvas.rows, nextmainCanvas.cols, nextmainCanvas.rows)));

        cv::Mat blurred;
        applyVerticalMotionBlur(panorama, blurred, 15, -1);
        panorama = blurred;

        const int frame_width = nextmainCanvas.cols;
        const int frame_height = nextmainCanvas.rows;
        for (int y = 0; y <= frame_height; y += (int)((frame_height * 1.5 - y) / 8)) {
            auto start_clock = steady_clock::now();

            cv::Mat view = panorama(cv::Rect(0, y, frame_width, frame_height));
            cv::resize(view, mainCanvas, mainCanvas.size(), 0, 0, cv::INTER_NEAREST);
            drawExtraUI(mainCanvas);

            updateMainCanvas();

            if (duration_cast<milliseconds>(steady_clock::now() - start_clock).count() < 10)
                Sleep(1);
        }
    }

    struct ImageInfoPalette {
        uint32_t background;
        uint32_t border;
        uint32_t primary;
        uint32_t secondary;
        uint32_t muted;
        uint32_t accent;
    };

    static ImageInfoPalette imageInfoPalette(bool light) {
        if (light) {
            return {
                ImageInfoPresentation::LIGHT_PANEL_BACKGROUND,
                ImageInfoPresentation::LIGHT_PANEL_BORDER,
                ImageInfoPresentation::LIGHT_TEXT_PRIMARY,
                ImageInfoPresentation::LIGHT_TEXT_SECONDARY,
                ImageInfoPresentation::LIGHT_TEXT_MUTED,
                ImageInfoPresentation::LIGHT_ACCENT,
            };
        }
        return {
            ImageInfoPresentation::DARK_PANEL_BACKGROUND,
            ImageInfoPresentation::DARK_PANEL_BORDER,
            ImageInfoPresentation::DARK_TEXT_PRIMARY,
            ImageInfoPresentation::DARK_TEXT_SECONDARY,
            ImageInfoPresentation::DARK_TEXT_MUTED,
            ImageInfoPresentation::DARK_ACCENT,
        };
    }

    static std::string imageColorMode(const ImageAsset* asset) {
        if (!asset)
            return {};
        const cv::Mat* image = nullptr;
        if (!asset->primaryFrame.empty())
            image = &asset->primaryFrame;
        else if (!asset->frames.empty() && !asset->frames.front().empty())
            image = &asset->frames.front();
        if (!image)
            return {};

        int bitsPerChannel = 0;
        switch (image->depth()) {
        case CV_8U:
        case CV_8S:
            bitsPerChannel = 8;
            break;
        case CV_16U:
        case CV_16S:
            bitsPerChannel = 16;
            break;
        case CV_32S:
        case CV_32F:
            bitsPerChannel = 32;
            break;
        case CV_64F:
            bitsPerChannel = 64;
            break;
        default:
            break;
        }
        const int channels = image->channels();
        const std::string name = channels == 1 ? "GRAY" : channels == 2 ? "GRAY+A" :
            channels == 3 ? "RGB" : channels == 4 ? "RGBA" :
            std::format("{} channels", channels);
        return bitsPerChannel ? std::format("{} · {}bpp", name, bitsPerChannel * channels) : name;
    }

    static bool imageInfoUsesLightPalette(const cv::Mat& canvas, const cv::Rect& panel) {
        const cv::Rect sample = panel & cv::Rect{ 0, 0, canvas.cols, canvas.rows };
        if (sample.empty())
            return false;
        const cv::Scalar average = cv::mean(canvas(sample));
        return ImageInfoPresentation::useLightPalette(
            static_cast<uint8_t>(average[0]), static_cast<uint8_t>(average[1]),
            static_cast<uint8_t>(average[2]));
    }

    static void drawInfoPanelBackdrop(cv::Mat& canvas, const cv::Rect& panel,
        int radius, const ImageInfoPalette& palette) {
        const cv::Rect clipped = panel & cv::Rect{ 0, 0, canvas.cols, canvas.rows };
        if (clipped.empty())
            return;

        cv::Mat blurred;
        cv::GaussianBlur(canvas(clipped), blurred, { 0, 0 }, 8.0, 8.0, cv::BORDER_REPLICATE);
        auto maskSurface = roundedSurface(clipped.width, clipped.height, radius, 0xFFFFFFFFu);
        cv::Mat mask;
        cv::extractChannel(maskSurface, mask, 3);
        blurred.copyTo(canvas(clipped), mask);

        auto border = roundedSurface(panel.width, panel.height, radius, palette.border);
        jarkUtils::overlayImg(canvas, border, panel.x, panel.y);
        if (panel.width > 2 && panel.height > 2) {
            auto fill = roundedSurface(panel.width - 2, panel.height - 2,
                std::max(1, radius - 1), palette.background);
            jarkUtils::overlayImg(canvas, fill, panel.x + 1, panel.y + 1);
        }
    }

    const ImageInfoPresentation::Model& currentImageInfoModel() {
        const ImageAsset* asset = curPar.imageAssetPtr.get();
        const uint32_t language = GlobalVar::settingParameter.UI_LANG;
        if (asset != imageInfoAssetCache || language != imageInfoLanguageCache) {
            imageInfoAssetCache = asset;
            imageInfoLanguageCache = language;
            imageInfoScrollOffset = 0;
            imageInfoModelCache = ImageInfoPresentation::build(
                asset ? asset->exifInfo : std::string_view{}, language == 0,
                imageColorMode(asset));
        }
        return imageInfoModelCache;
    }

    void drawImageInfoCard(cv::Mat& canvas, ImageInfoPresentation::Mode mode) {
        imageInfoPanelRect = {};
        imageInfoCloseRect = {};
        imageInfoCopyRect = {};
        imageInfoContentHeight = 0;
        imageInfoViewportHeight = 0;
        if (canvas.cols < 160 || canvas.rows < 160)
            return;

        const auto& model = currentImageInfoModel();
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        const bool compact = mode == ImageInfoPresentation::Mode::Compact;
        const auto rows = compact ? ImageInfoPresentation::compactRows(model, chinese) : model.basic;
        const UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
        const auto scaled = [dpi](int logical) {
            return TextRenderingPolicy::scaledPixelSize(logical, dpi);
            };
        const int logicalPadding = compact ? ImageInfoPresentation::LOGICAL_COMPACT_PADDING :
            ImageInfoPresentation::LOGICAL_FULL_PADDING;
        const int margin = scaled(compact ? 12 : 16);
        const int padding = scaled(logicalPadding);
        const int fontSize = scaled(ImageInfoPresentation::LOGICAL_FONT_SIZE);
        const int lineHeight = scaled(ImageInfoPresentation::LOGICAL_LINE_HEIGHT);
        const int rowPadding = scaled(ImageInfoPresentation::LOGICAL_ROW_PADDING);
        const int headerHeight = scaled(compact ?
            ImageInfoPresentation::LOGICAL_COMPACT_HEADER_HEIGHT :
            ImageInfoPresentation::LOGICAL_FULL_HEADER_HEIGHT);
        const int sectionHeight = scaled(ImageInfoPresentation::LOGICAL_SECTION_HEIGHT);
        const int footerHeight = scaled(ImageInfoPresentation::LOGICAL_FOOTER_HEIGHT);
        const int labelWidth = scaled(compact ? ImageInfoPresentation::LOGICAL_COMPACT_LABEL_WIDTH :
            ImageInfoPresentation::LOGICAL_FULL_LABEL_WIDTH);
        const int panelWidth = std::min(scaled(compact ?
            ImageInfoPresentation::LOGICAL_COMPACT_PANEL_WIDTH :
            ImageInfoPresentation::LOGICAL_FULL_PANEL_WIDTH),
            canvas.cols - margin * 2);
        const int valueWidth = std::max(fontSize, panelWidth - padding * 2 - labelWidth - scaled(12));
        const int maxUnits = std::max(1, valueWidth * 17 /
            (std::max(1, fontSize) * 10));
        const auto rowHeight = [&](const ImageInfoPresentation::Row& row) {
            return static_cast<int>(ImageInfoPresentation::wrapUtf8(row.value, maxUnits).size()) *
                lineHeight + rowPadding * 2;
            };

        int contentHeight = compact ? 0 : sectionHeight;
        for (const auto& row : rows)
            contentHeight += rowHeight(row);
        if (!compact && !model.details.empty()) {
            contentHeight += 1 + sectionHeight;
            for (const auto& row : model.details)
                contentHeight += rowHeight(row);
        }

        const int availableHeight = canvas.rows - margin * 2;
        const int maxPanelHeight = std::min(availableHeight, scaled(compact ?
            ImageInfoPresentation::LOGICAL_COMPACT_MAX_HEIGHT :
            ImageInfoPresentation::LOGICAL_FULL_MAX_HEIGHT));
        const int panelHeight = std::min(maxPanelHeight, headerHeight + contentHeight + footerHeight);
        if (panelWidth <= 0 || panelHeight <= headerHeight + footerHeight)
            return;
        const int panelY = compact ? margin : canvas.rows - margin - panelHeight;
        const cv::Rect panel{ margin, panelY, panelWidth, panelHeight };
        const bool light = imageInfoUsesLightPalette(canvas, panel);
        const auto palette = imageInfoPalette(light);
        drawInfoPanelBackdrop(canvas, panel, scaled(compact ? 12 : 14), palette);

        imageInfoPanelRect = panel;
        imageInfoCloseRect = {
            panel.x + panel.width - padding - scaled(20),
            panel.y + (headerHeight - scaled(20)) / 2,
            scaled(20), scaled(20)
        };

        textDrawer.setSize(fontSize);
        const int iconSize = scaled(14);
        const cv::Point iconCenter{ panel.x + padding + iconSize / 2,
            panel.y + headerHeight / 2 };
        cv::circle(canvas, iconCenter, iconSize / 2,
            jarkUtils::to_cv_scalar(palette.accent), 1, cv::LINE_AA);
        textDrawer.putAlignCenter(canvas,
            { iconCenter.x - iconSize / 2, iconCenter.y - iconSize / 2, iconSize, iconSize },
            "i", palette.accent);
        textDrawer.putAlignLeft(canvas,
            { panel.x + padding + iconSize + scaled(8), panel.y,
                panel.width - padding * 2 - iconSize - scaled(36), headerHeight },
            chinese ? "图像信息" : "Image information",
            palette.primary);
        cv::line(canvas,
            { imageInfoCloseRect.x + scaled(5), imageInfoCloseRect.y + scaled(5) },
            { imageInfoCloseRect.x + imageInfoCloseRect.width - scaled(5),
                imageInfoCloseRect.y + imageInfoCloseRect.height - scaled(5) },
            jarkUtils::to_cv_scalar(palette.muted), 1, cv::LINE_AA);
        cv::line(canvas,
            { imageInfoCloseRect.x + imageInfoCloseRect.width - scaled(5), imageInfoCloseRect.y + scaled(5) },
            { imageInfoCloseRect.x + scaled(5),
                imageInfoCloseRect.y + imageInfoCloseRect.height - scaled(5) },
            jarkUtils::to_cv_scalar(palette.muted), 1, cv::LINE_AA);
        cv::line(canvas, { panel.x, panel.y + headerHeight },
            { panel.x + panel.width, panel.y + headerHeight },
            jarkUtils::to_cv_scalar(palette.border), 1);

        const cv::Rect contentRect{ panel.x, panel.y + headerHeight,
            panel.width, panel.height - headerHeight - footerHeight };
        imageInfoContentHeight = contentHeight;
        imageInfoViewportHeight = contentRect.height;
        imageInfoScrollOffset = ImageInfoPresentation::clampScrollOffset(
            contentHeight, contentRect.height, imageInfoScrollOffset);
        cv::Mat contentCanvas = canvas(contentRect);
        int y = -imageInfoScrollOffset;

        const auto drawSectionLabel = [&](const char* title) {
            textDrawer.putWrappedLeft(contentCanvas,
                { padding, y + (sectionHeight - lineHeight) / 2,
                    contentRect.width - padding * 2, lineHeight },
                title, palette.accent);
            y += sectionHeight;
            };
        const auto drawRows = [&](const std::vector<ImageInfoPresentation::Row>& sectionRows) {
            for (const auto& row : sectionRows) {
                const auto lines = ImageInfoPresentation::wrapUtf8(row.value, maxUnits);
                const std::string wrapped = ImageInfoPresentation::joinWrappedLines(lines);
                const int height = static_cast<int>(lines.size()) * lineHeight + rowPadding * 2;
                textDrawer.putWrappedLeft(contentCanvas,
                    { padding, y + rowPadding, labelWidth, lineHeight },
                    row.label.c_str(), palette.muted);
                textDrawer.putWrappedLeft(contentCanvas,
                    { padding + labelWidth + scaled(12), y + rowPadding,
                        valueWidth, static_cast<int>(lines.size()) * lineHeight },
                    wrapped.c_str(), palette.secondary);
                if (y + height >= 0 && y + height < contentRect.height) {
                    cv::line(contentCanvas, { padding, y + height - 1 },
                        { contentRect.width - padding, y + height - 1 },
                        jarkUtils::to_cv_scalar(palette.border), 1);
                }
                y += height;
            }
            };

        if (compact) {
            drawRows(rows);
        }
        else {
            drawSectionLabel(chinese ? "基本信息" : "BASIC INFORMATION");
            drawRows(rows);
            if (!model.details.empty()) {
                cv::line(contentCanvas, { padding, y },
                    { contentRect.width - padding, y },
                    jarkUtils::to_cv_scalar(palette.border), 1);
                ++y;
                drawSectionLabel(chinese ? "照片信息" : "PHOTO METADATA");
                drawRows(model.details);
            }
        }

        if (contentHeight > contentRect.height) {
            const int trackWidth = std::max(2, scaled(3));
            const int trackX = panel.x + panel.width - scaled(7);
            const int thumbHeight = std::max(scaled(24),
                contentRect.height * contentRect.height / contentHeight);
            const int maxScroll = std::max(1, contentHeight - contentRect.height);
            const int thumbY = contentRect.y +
                (contentRect.height - thumbHeight) * imageInfoScrollOffset / maxScroll;
            cv::rectangle(canvas, { trackX, thumbY, trackWidth, thumbHeight },
                jarkUtils::to_cv_scalar(palette.muted), -1, cv::LINE_AA);
        }

        const int footerY = panel.y + panel.height - footerHeight;
        cv::line(canvas, { panel.x, footerY }, { panel.x + panel.width, footerY },
            jarkUtils::to_cv_scalar(palette.border), 1);
        if (!compact) {
            textDrawer.putAlignLeft(canvas,
                { panel.x + padding, footerY, scaled(90), footerHeight },
                "Tab · I", palette.muted);
        }

        const int copyWidth = scaled(compact ? 96 : 108);
        const int copyHeight = scaled(24);
        imageInfoCopyRect = {
            panel.x + panel.width - padding - copyWidth,
            footerY + (footerHeight - copyHeight) / 2,
            copyWidth, copyHeight
        };
        auto copyButton = roundedSurface(copyWidth, copyHeight, scaled(7),
            light ? 0x14000000u : 0x18FFFFFFu);
        jarkUtils::overlayImg(canvas, copyButton, imageInfoCopyRect.x, imageInfoCopyRect.y);
        const cv::Rect keyRect{ imageInfoCopyRect.x + scaled(8), imageInfoCopyRect.y + scaled(4),
            scaled(20), copyHeight - scaled(8) };
        cv::rectangle(canvas, keyRect, jarkUtils::to_cv_scalar(palette.border), 1, cv::LINE_AA);
        textDrawer.putAlignCenter(canvas, keyRect, "C", palette.secondary);
        textDrawer.putAlignLeft(canvas,
            { keyRect.x + keyRect.width + scaled(7), imageInfoCopyRect.y,
                imageInfoCopyRect.width - keyRect.width - scaled(18), copyHeight },
            chinese ? "复制全部" : "Copy all", palette.muted);
    }

    void drawExifInfo(cv::Mat& canvas) {
        if (!showExif) {
            imageInfoPanelRect = {};
            imageInfoCloseRect = {};
            imageInfoCopyRect = {};
            return;
        }
        drawImageInfoCard(canvas, presentationMode ? ImageInfoPresentation::Mode::Full :
            ImageInfoPresentation::Mode::Compact);
    }

    static cv::Mat roundedSurface(int width, int height, int radius,
        uint32_t fill, uint32_t border = 0) {
        cv::Mat surface(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        radius = std::clamp(radius, 1, std::min(width, height) / 2);
        const auto fillColor = jarkUtils::to_cv_scalar(fill);
        cv::rectangle(surface, { radius, 0, width - radius * 2, height }, fillColor, -1, cv::LINE_AA);
        cv::rectangle(surface, { 0, radius, width, height - radius * 2 }, fillColor, -1, cv::LINE_AA);
        cv::circle(surface, { radius, radius }, radius, fillColor, -1, cv::LINE_AA);
        cv::circle(surface, { width - radius - 1, radius }, radius, fillColor, -1, cv::LINE_AA);
        cv::circle(surface, { radius, height - radius - 1 }, radius, fillColor, -1, cv::LINE_AA);
        cv::circle(surface, { width - radius - 1, height - radius - 1 }, radius, fillColor, -1, cv::LINE_AA);
        if (border != 0)
            cv::rectangle(surface, { 0, 0, width - 1, height - 1 },
                jarkUtils::to_cv_scalar(border), 1, cv::LINE_AA);
        return surface;
    }

    static cv::Rect toCvRect(const OverlayLayout::Rect& rect) {
        return { rect.x, rect.y, rect.width, rect.height };
    }

    void drawOverlayIcon(cv::Mat& canvas, const cv::Mat& source,
        const OverlayLayout::Rect& target, uint32_t tint = 0,
        bool compactControl = false) {
        if (source.empty() || target.width <= 0 || target.height <= 0)
            return;
        const int iconSize = OverlayLayout::toolbarIconSize(
            canvas.cols, target, compactControl);
        cv::Mat icon;
        if (source.cols == iconSize && source.rows == iconSize)
            icon = source;
        else
            cv::resize(source, icon, { iconSize, iconSize }, 0, 0, cv::INTER_AREA);
        if (tint != 0) {
            icon = icon.clone();
            intUnion tintColor{ tint };
            for (int y = 0; y < icon.rows; ++y) {
                auto* row = icon.ptr<intUnion>(y);
                for (int x = 0; x < icon.cols; ++x) {
                    if (row[x][3] == 0)
                        continue;
                    row[x][0] = tintColor[0];
                    row[x][1] = tintColor[1];
                    row[x][2] = tintColor[2];
                }
            }
        }
        jarkUtils::overlayImg(canvas, icon,
            target.x + (target.width - icon.cols) / 2,
            target.y + (target.height - icon.rows) / 2);
    }

    const char* toolbarTooltip() const {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        switch (cursorPos) {
        case CursorPos::toolbarPrevious: return chinese ? "上一张" : "Previous";
        case CursorPos::toolbarPlayPause: return slideshowPlaying ?
            (chinese ? "暂停播放" : "Pause slideshow") :
            (chinese ? "播放幻灯片" : "Play slideshow");
        case CursorPos::toolbarNext: return chinese ? "下一张" : "Next";
        case CursorPos::toolbarRotateLeft: return chinese ? "左旋转 90°" : "Rotate left";
        case CursorPos::toolbarRotateRight: return chinese ? "右旋转 90°" : "Rotate right";
        case CursorPos::toolbarFlipHorizontal: return chinese ? "左右镜像" : "Flip horizontal";
        case CursorPos::toolbarFlipVertical: return chinese ? "上下镜像" : "Flip vertical";
        case CursorPos::toolbarZoomFit: return chinese ? "适应窗口" : "Fit to window";
        case CursorPos::toolbarZoomActual: return chinese ? "实际大小 (1:1)" : "Actual size (1:1)";
        case CursorPos::toolbarFullscreen: return presentationMode ?
            (chinese ? "退出沉浸" : "Exit immersive") : (chinese ? "沉浸显示" : "Immersive view");
        case CursorPos::toolbarSetting: return chinese ? "设置" : "Settings";
        case CursorPos::toolbarZoomOut: return chinese ? "缩小" : "Zoom out";
        case CursorPos::toolbarZoomText: return chinese ? "输入缩放倍率" : "Enter zoom percentage";
        case CursorPos::toolbarZoomIn: return chinese ? "放大" : "Zoom in";
        default: return nullptr;
        }
    }

    void drawToolbarButton(cv::Mat& canvas, const OverlayLayout::Rect& rect,
        const cv::Mat& icon, CursorPos expectedCursor, bool active = false,
        bool danger = false, bool compactControl = false) {
        const bool hovered = cursorPos == expectedCursor;
        if (hovered || active) {
            const uint32_t background = active ? 0x383B82F6u :
                (danger ? 0x1FEF4444u : 0x12FFFFFFu);
            auto surface = roundedSurface(rect.width, rect.height,
                std::max(4, rect.width / 3), background);
            jarkUtils::overlayImg(canvas, surface, rect.x, rect.y);
        }
        drawOverlayIcon(canvas, icon, rect, active ? 0xFF60A5FAu : 0,
            compactControl);
        if (active) {
            cv::circle(canvas, { rect.x + rect.width / 2, rect.y + rect.height - 4 },
                2, jarkUtils::to_cv_scalar(0xFF60A5FAu), -1, cv::LINE_AA);
        }
    }

    void drawViewerToolbar(cv::Mat& canvas) {
        const auto toolbar = OverlayLayout::toolbarRect(canvas.cols, canvas.rows);
        auto pill = roundedSurface(toolbar.width, toolbar.height,
            std::max(8, toolbar.height / 3), 0xD10D0F14u, OverlayLayout::TOOLBAR_BORDER);
        jarkUtils::overlayImg(canvas, pill, toolbar.x, toolbar.y);

        const int scale = OverlayLayout::toolbarScale(canvas.cols);
        for (const int baseX : { 42, 199, 342 }) {
            const int x = toolbar.x + OverlayLayout::scaled(
                OverlayLayout::BASE_TOOLBAR_PADDING + baseX, scale);
            const int half = OverlayLayout::scaled(10, scale);
            cv::line(canvas, { x, toolbar.y + toolbar.height / 2 - half },
                { x, toolbar.y + toolbar.height / 2 + half },
                cv::Scalar(255, 255, 255, 26), 1, cv::LINE_AA);
        }

        drawToolbarButton(canvas, OverlayLayout::settingsRect(canvas.cols, canvas.rows),
            extraUIRes.setting, CursorPos::toolbarSetting);
        drawToolbarButton(canvas, OverlayLayout::rotateLeftRect(canvas.cols, canvas.rows),
            extraUIRes.leftRotate, CursorPos::toolbarRotateLeft);
        drawToolbarButton(canvas, OverlayLayout::rotateRightRect(canvas.cols, canvas.rows),
            extraUIRes.rightRotate, CursorPos::toolbarRotateRight);
        drawToolbarButton(canvas, OverlayLayout::flipHorizontalRect(canvas.cols, canvas.rows),
            extraUIRes.flipHorizontal, CursorPos::toolbarFlipHorizontal, curPar.flipHorizontal);
        drawToolbarButton(canvas, OverlayLayout::flipVerticalRect(canvas.cols, canvas.rows),
            extraUIRes.flipVertical, CursorPos::toolbarFlipVertical, curPar.flipVertical);

        drawToolbarButton(canvas, OverlayLayout::toolbarPreviousRect(canvas.cols, canvas.rows),
            extraUIRes.leftArrow, CursorPos::toolbarPrevious);
        drawToolbarButton(canvas, OverlayLayout::toolbarPlayPauseRect(canvas.cols, canvas.rows),
            slideshowPlaying ? extraUIRes.pause : extraUIRes.play,
            CursorPos::toolbarPlayPause, slideshowPlaying);
        drawToolbarButton(canvas, OverlayLayout::toolbarNextRect(canvas.cols, canvas.rows),
            extraUIRes.rightArrow, CursorPos::toolbarNext);

        drawToolbarButton(canvas, OverlayLayout::zoomFitRect(canvas.cols, canvas.rows),
            extraUIRes.fitWindow, CursorPos::toolbarZoomFit,
            curPar.zoomIndex == curPar.zoomIndexFix);
        drawToolbarButton(canvas, OverlayLayout::zoomActualRect(canvas.cols, canvas.rows),
            extraUIRes.actualSize, CursorPos::toolbarZoomActual,
            curPar.zoomIndex == curPar.zoomIndex100percent);
        drawToolbarButton(canvas, OverlayLayout::fullscreenRect(canvas.cols, canvas.rows),
            extraUIRes.fullscreen, CursorPos::toolbarFullscreen, presentationMode);
        drawToolbarButton(canvas, OverlayLayout::zoomOutRect(canvas.cols, canvas.rows),
            extraUIRes.zoomOut, CursorPos::toolbarZoomOut, false, false, true);
        drawToolbarButton(canvas, OverlayLayout::zoomInRect(canvas.cols, canvas.rows),
            extraUIRes.zoomIn, CursorPos::toolbarZoomIn, false, false, true);

        const auto zoomTextLayout = OverlayLayout::zoomTextRect(canvas.cols, canvas.rows);
        if (zoomTextEditing) {
            auto editSurface = roundedSurface(zoomTextLayout.width, zoomTextLayout.height,
                std::max(4, zoomTextLayout.height / 4), 0x263B82F6u);
            jarkUtils::overlayImg(canvas, editSurface, zoomTextLayout.x, zoomTextLayout.y);
            cv::line(canvas,
                { zoomTextLayout.x + 4, zoomTextLayout.y + zoomTextLayout.height - 1 },
                { zoomTextLayout.x + zoomTextLayout.width - 5,
                  zoomTextLayout.y + zoomTextLayout.height - 1 },
                jarkUtils::to_cv_scalar(0xFF60A5FAu), 1, cv::LINE_AA);
        }

        textDrawer.setSize(OverlayLayout::TOOLBAR_TEXT_SIZE);
        const std::string zoomText = zoomTextEditing ?
            std::format("{}%", zoomEditText) :
            std::format("{}%",
                ZoomPolicy::displayPercent(curPar.zoomCur, CurImageParameter::ZOOM_BASE));
        const auto zoomTextRect = toCvRect(zoomTextLayout);
        textDrawer.putAlignCenter(canvas, zoomTextRect,
            zoomText.c_str(), zoomTextEditing ? 0xFFFFFFFFu : 0xFFDDE1E9u);
        if (!zoomTextEditing && OverlayLayout::TOOLBAR_TEXT_BOLD_OFFSET > 0) {
            auto boldRect = zoomTextRect;
            boldRect.x += OverlayLayout::TOOLBAR_TEXT_BOLD_OFFSET;
            textDrawer.putAlignCenter(canvas, boldRect,
                zoomText.c_str(), 0xBFDDE1E9u);
        }

        if (const char* tooltip = toolbarTooltip()) {
            OverlayLayout::Rect hovered{};
            switch (cursorPos) {
            case CursorPos::toolbarPrevious: hovered = OverlayLayout::toolbarPreviousRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarPlayPause: hovered = OverlayLayout::toolbarPlayPauseRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarNext: hovered = OverlayLayout::toolbarNextRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarRotateLeft: hovered = OverlayLayout::rotateLeftRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarRotateRight: hovered = OverlayLayout::rotateRightRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFlipHorizontal: hovered = OverlayLayout::flipHorizontalRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFlipVertical: hovered = OverlayLayout::flipVerticalRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomFit: hovered = OverlayLayout::zoomFitRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomActual: hovered = OverlayLayout::zoomActualRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFullscreen: hovered = OverlayLayout::fullscreenRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarSetting: hovered = OverlayLayout::settingsRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomOut: hovered = OverlayLayout::zoomOutRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomText: hovered = OverlayLayout::zoomTextRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomIn: hovered = OverlayLayout::zoomInRect(canvas.cols, canvas.rows); break;
            default: break;
            }
            const auto tooltipWide = jarkUtils::utf8ToWstring(tooltip);
            int tooltipTextWidth = 0;
            for (const wchar_t character : tooltipWide)
                tooltipTextWidth += character > 0xFF ? 14 : 7;
            const int tooltipWidth = std::min(180, std::max(72,
                tooltipTextWidth + 24));
            const int tooltipHeight = 30;
            const int tooltipX = std::clamp(hovered.x + hovered.width / 2 - tooltipWidth / 2,
                4, canvas.cols - tooltipWidth - 4);
            const int tooltipY = std::max(4, toolbar.y - tooltipHeight - 8);
            auto surface = roundedSurface(tooltipWidth, tooltipHeight, 7,
                0xE6000000u, 0x1AFFFFFFu);
            jarkUtils::overlayImg(canvas, surface, tooltipX, tooltipY);
            textDrawer.putAlignCenter(canvas,
                { tooltipX, tooltipY, tooltipWidth, tooltipHeight }, tooltip, 0xFFE8EAF0u);
        }
    }

    void drawExtraUI(cv::Mat& canvas) {
        const int canvasHeight = canvas.rows;
        const int canvasWidth = canvas.cols;
        if (canvasWidth < 100 || canvasHeight < 100)
            return;

        switch (extraUIFlag) {
        case ShowExtraUI::bottomToolbar: {
            drawViewerToolbar(canvas);
        } break;
        case ShowExtraUI::leftArrow:
        case ShowExtraUI::rightArrow:
            break;
        case ShowExtraUI::animationBar: {
            auto& img = curPar.isAnimationPause ? extraUIRes.animationBarPausing : extraUIRes.animationBarPlaying;
            jarkUtils::overlayImg(canvas, img, (canvasWidth - img.cols) / 2, 0);
        } break;
        case ShowExtraUI::none:
            break;
        }

        const bool windowHasCaption =
            (GetWindowLongPtrW(m_hWnd, GWL_STYLE) & WS_CAPTION) != 0;
        if (OverlayLayout::shouldDrawPresentationClose(
            presentationMode, windowHasCaption, !extraUIRes.presentationClose.empty())) {
            const auto close = OverlayLayout::presentationCloseRect(canvasWidth, canvasHeight);
            const cv::Point center{ close.x + close.width / 2, close.y + close.height / 2 };
            cv::circle(canvas, center, close.width / 2 - 1,
                cv::Scalar(33, 32, 32, 235), -1, cv::LINE_AA);
            constexpr int arm = 9;
            cv::line(canvas, { center.x - arm, center.y - arm },
                { center.x + arm, center.y + arm }, cv::Scalar(255, 255, 255, 255), 3, cv::LINE_AA);
            cv::line(canvas, { center.x + arm, center.y - arm },
                { center.x - arm, center.y + arm }, cv::Scalar(255, 255, 255, 255), 3, cv::LINE_AA);
        }
    }

    void drawZoomIndicator(cv::Mat& canvas) {
        if (zoomIndicatorStartedAt == std::chrono::steady_clock::time_point{})
            return;

        const int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - zoomIndicatorStartedAt).count());
        const int alpha = ZoomPolicy::indicatorAlpha(elapsedMs);
        if (alpha <= 0) {
            zoomIndicatorStartedAt = {};
            return;
        }

        const auto rect = OverlayLayout::zoomIndicatorRect(canvas.cols, canvas.rows);
        const uint32_t surfaceColor = static_cast<uint32_t>(150 * alpha / 255) << 24;
        auto surface = roundedSurface(rect.width, rect.height,
            std::max(6, rect.height / 5), surfaceColor);
        jarkUtils::overlayImg(canvas, surface, rect.x, rect.y);

        textDrawer.setSize(18);
        const auto text = std::format("{}%",
            ZoomPolicy::displayPercent(curPar.zoomCur, CurImageParameter::ZOOM_BASE));
        const uint32_t textColor = (static_cast<uint32_t>(alpha) << 24) | 0x00FFFFFFu;
        textDrawer.putAlignCenter(canvas, toCvRect(rect), text.c_str(), textColor);
    }

    void updateMainCanvas() {
        PresentCanvas(mainCanvas.ptr(), mainCanvas.cols, mainCanvas.rows, (int)mainCanvas.step);
    }


    int64_t delayRemain = 0;
    const std::chrono::milliseconds frameDuration{ 10 };
    std::chrono::steady_clock::time_point lastTimestamp = std::chrono::steady_clock::now();


    void DrawScene() {
        // 后续逻辑大量直接解引用 imageAssetPtr，这里兜底避免任何路径下的空指针访问
        if (!curPar.imageAssetPtr)
            curPar.imageAssetPtr = imgDB.makeErrorAsset();

        if (GlobalVar::isNeedUpdateTheme) {
            GlobalVar::isNeedUpdateTheme = false;
            BOOL themeMode = GlobalVar::isCurrentUIDarkMode;
            DwmSetWindowAttribute(m_hWnd, 20, &themeMode, sizeof(BOOL));
            operateQueue.push({ ActionENUM::refresh });
        }

        if (GlobalVar::isNeedReloadImageCache) {
            GlobalVar::isNeedReloadImageCache = false;
            if (curFileIdx >= 0 && curFileIdx < (int)imgFileList.size()) {
                const auto currentPath = imgFileList[curFileIdx];
                imgDB.clear();

                if (currentPath == m_wndCaption) {
                    imgDB.put(m_wndCaption, { ImageFormat::Still,
                        imgDB.getHomeMat(GetDpiForWindow(m_hWnd)), {}, {}, getUIString(32) });
                    curPar.imageAssetPtr = imgDB.getCheckedPtr(currentPath, currentPath);
                }
                else {
                    const auto& nextPath = imgFileList[(curFileIdx + 1) % imgFileList.size()];
                    curPar.imageAssetPtr = imgDB.getCheckedPtr(currentPath, nextPath);
                }

                initCurrentImageParameters();
                operateQueue.push({ ActionENUM::refresh });
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const bool slideshowElapsed = slideshowNextAt != std::chrono::steady_clock::time_point{} &&
            now >= slideshowNextAt;
        if (SlideshowPolicy::shouldAdvance(
            slideshowPlaying, imgFileList.size(), slideshowElapsed)) {
            scheduleSlideshowNext();
            operateQueue.push({ ActionENUM::nextImg });
        }

        auto operateAction = operateQueue.get();
        if (operateAction.action == ActionENUM::none &&
            curPar.zoomCur == curPar.zoomTarget &&
            curPar.slideCur == curPar.slideTarget &&
            zoomIndicatorStartedAt == std::chrono::steady_clock::time_point{} &&
            (curPar.imageAssetPtr->format != ImageFormat::Animated || 
                (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause))) {

            Sleep(1); // Windows机制限制，实际时长最小只能 15.6ms
            return;
        }

        if (operateAction.action == ActionENUM::printImage) {
            if (Printer::isWorking) {
                jarkUtils::activateWindow(Printer::hwnd);
            }
            else {
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                std::thread printerThread([](cv::Mat image, int rotation) {
                    cv::Mat rotatedImage;

                    switch (rotation) {
                    case 1:
                        cv::rotate(image, rotatedImage, cv::ROTATE_90_COUNTERCLOCKWISE);
                        break;
                    case 2:
                        cv::rotate(image, rotatedImage, cv::ROTATE_180);
                        break;
                    case 3:
                        cv::rotate(image, rotatedImage, cv::ROTATE_90_CLOCKWISE);
                        break;
                    default:
                        rotatedImage = image;
                        break;
                    }

                    Printer printer(rotatedImage);
                    }, srcImg, curPar.rotation);
                printerThread.detach();
            }
            return;
        }

        if (operateAction.action == ActionENUM::setting) {
            if (Setting::isWorking) {
                Setting::curTabIdx = operateAction.value1;
                PostMessageW(Setting::hwnd, MatWindow::WM_MATWINDOW_DRAW_REQUEST, 0, 0);
                jarkUtils::activateWindow(Setting::hwnd);
            }
            else {
                std::thread settingThread([](int tabIdx) {
                    Setting setting(tabIdx);
                    }, operateAction.value1);
                settingThread.detach();
            }
            return;
        }

        if (operateAction.action == ActionENUM::toggleFullScreen) {
            if (presentationMode)
                exitPresentationMode();
            else
                enterPresentationMode();
            return;
        }

        if (operateAction.action == ActionENUM::copyImage) {
            cv::Mat source;
            if (curPar.imageAssetPtr->format == ImageFormat::None ||
                curPar.imageAssetPtr->format == ImageFormat::Still)
                source = curPar.imageAssetPtr->primaryFrame;
            else
                source = curPar.imageAssetPtr->frames[curPar.curFrameIdx];
            jarkUtils::copyImageToClipboard(source);
            return;
        }

        // 以下action均需要刷新画面
        auto clampSlideForZoom = [&](Cood slide, int64_t zoom) {
            const int srcW = (curPar.rotation == 0 || curPar.rotation == 2) ? curPar.width : curPar.height;
            const int srcH = (curPar.rotation == 0 || curPar.rotation == 2) ? curPar.height : curPar.width;
            const int slideXMax = (int)(srcW * zoom / 2 / curPar.ZOOM_BASE);
            const int slideYMax = (int)(srcH * zoom / 2 / curPar.ZOOM_BASE);

            slide.x = std::clamp(slide.x, -slideXMax, slideXMax);
            slide.y = std::clamp(slide.y, -slideYMax, slideYMax);
            return slide;
        };

        auto computeZoomSlide = [&](int64_t zoomNext) {
            const int srcW = (curPar.rotation == 0 || curPar.rotation == 2) ? curPar.width : curPar.height;
            const int srcH = (curPar.rotation == 0 || curPar.rotation == 2) ? curPar.height : curPar.width;
            const double halfDiffW_old = (winWidth - (double)srcW * curPar.zoomCur / curPar.ZOOM_BASE) / 2.0;
            const double halfDiffH_old = (winHeight - (double)srcH * curPar.zoomCur / curPar.ZOOM_BASE) / 2.0;

            const int imgLeft = (int)std::round(curPar.slideCur.x + halfDiffW_old);
            const int imgTop = (int)std::round(curPar.slideCur.y + halfDiffH_old);
            const int imgRight = (int)std::round(imgLeft + (double)srcW * curPar.zoomCur / curPar.ZOOM_BASE);
            const int imgBottom = (int)std::round(imgTop + (double)srcH * curPar.zoomCur / curPar.ZOOM_BASE);

            Cood slideNext = curPar.slideCur;
            if (mousePos.x >= imgLeft && mousePos.x < imgRight && mousePos.y >= imgTop && mousePos.y < imgBottom) {
                const double halfDiffW_new = (winWidth - (double)srcW * zoomNext / curPar.ZOOM_BASE) / 2.0;
                const double halfDiffH_new = (winHeight - (double)srcH * zoomNext / curPar.ZOOM_BASE) / 2.0;
                const double srcX = ((double)mousePos.x - curPar.slideCur.x - halfDiffW_old) * curPar.ZOOM_BASE / curPar.zoomCur;
                const double srcY = ((double)mousePos.y - curPar.slideCur.y - halfDiffH_old) * curPar.ZOOM_BASE / curPar.zoomCur;
                slideNext.x = (int)std::round(mousePos.x - halfDiffW_new - srcX * zoomNext / curPar.ZOOM_BASE);
                slideNext.y = (int)std::round(mousePos.y - halfDiffH_new - srcY * zoomNext / curPar.ZOOM_BASE);
            }
            curPar.slideTarget = clampSlideForZoom(slideNext, zoomNext);
        };

        switch (operateAction.action) {
        case ActionENUM::preImg: {
            if (imgFileList.size() <= 1)
                break;

            if (GlobalVar::settingParameter.switchImageAnimationMode) {// 开动画时才需要
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                drawCanvas(srcImg, mainCanvas); //先更新无额外按钮UI的原图
                drawExifInfo(mainCanvas);
            }
            
            // 播放过的实况图，状态会变成静态图，切走前恢复一下
            if (curPar.imageAssetPtr->format == ImageFormat::Still && !curPar.imageAssetPtr->frames.empty()) {
                curPar.imageAssetPtr->format = ImageFormat::Animated;
            }

            if (--curFileIdx < 0)
                curFileIdx = (int)imgFileList.size() - 1;
            curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + imgFileList.size() - 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToPreAnimationVertical();      // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToPreAnimationHorizontal();    // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
            if (slideshowPlaying)
                scheduleSlideshowNext();
        } break;

        case ActionENUM::nextImg: {
            if (imgFileList.size() <= 1)
                break;

            if (GlobalVar::settingParameter.switchImageAnimationMode) {// 开动画时才需要
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                drawCanvas(srcImg, mainCanvas); //先更新无额外按钮UI的原图
                drawExifInfo(mainCanvas);
            }

            // 播放过的实况图，状态会变成静态图，切走前恢复一下
            if (curPar.imageAssetPtr->format == ImageFormat::Still && !curPar.imageAssetPtr->frames.empty()) {
                curPar.imageAssetPtr->format = ImageFormat::Animated;
            }

            if (++curFileIdx >= (int)imgFileList.size())
                curFileIdx = 0;
            curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToNextAnimationVertical();   // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToNextAnimationHorizontal(); // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
            if (slideshowPlaying)
                scheduleSlideshowNext();
        } break;

        case ActionENUM::firstImg: {
            if (imgFileList.size() == 1 or curFileIdx == 0)
                break;

            if (GlobalVar::settingParameter.switchImageAnimationMode) {// 开动画时才需要
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                drawCanvas(srcImg, mainCanvas); //先更新无额外按钮UI的原图
                drawExifInfo(mainCanvas);
            }

            // 播放过的实况图，状态会变成静态图，切走前恢复一下
            if (curPar.imageAssetPtr->format == ImageFormat::Still && !curPar.imageAssetPtr->frames.empty()) {
                curPar.imageAssetPtr->format = ImageFormat::Animated;
            }

            curFileIdx = 0;
            curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + imgFileList.size() - 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToPreAnimationVertical();      // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToPreAnimationHorizontal();    // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
        } break;

        case ActionENUM::finalImg: {
            if (imgFileList.size() == 1 or curFileIdx == ((int)imgFileList.size() - 1))
                break;

            if (GlobalVar::settingParameter.switchImageAnimationMode) {// 开动画时才需要
                cv::Mat srcImg;
                if (curPar.imageAssetPtr->format == ImageFormat::None || curPar.imageAssetPtr->format == ImageFormat::Still)
                    srcImg = curPar.imageAssetPtr->primaryFrame;
                else
                    srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];

                drawCanvas(srcImg, mainCanvas); //先更新无额外按钮UI的原图
                drawExifInfo(mainCanvas);
            }

            // 播放过的实况图，状态会变成静态图，切走前恢复一下
            if (curPar.imageAssetPtr->format == ImageFormat::Still && !curPar.imageAssetPtr->frames.empty()) {
                curPar.imageAssetPtr->format = ImageFormat::Animated;
            }

            curFileIdx = (int)imgFileList.size() - 1;
            curPar.imageAssetPtr = imgDB.getCheckedPtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToNextAnimationVertical();   // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToNextAnimationHorizontal(); // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
        } break;

        case ActionENUM::slide: {
            curPar.slideTarget = clampSlideForZoom({
                curPar.slideTarget.x + operateAction.x,
                curPar.slideTarget.y + operateAction.y
                }, curPar.zoomTarget);
        } break;

        case ActionENUM::toggleSlideshow: {
            if (!SlideshowPolicy::canPlay(imgFileList.size())) {
                stopSlideshow();
                break;
            }
            slideshowPlaying = !slideshowPlaying;
            if (slideshowPlaying)
                scheduleSlideshowNext();
            else
                slideshowNextAt = {};
        } break;

        case ActionENUM::toggleExif: {
            showExif = !showExif;
            imageInfoScrollOffset = 0;
        } break;

        case ActionENUM::zoomIn: {
            if (curPar.zoomIndex < curPar.zoomList.size() - 1) {
                curPar.zoomIndex++;

                auto zoomNext = curPar.zoomList[curPar.zoomIndex];
                if (curPar.zoomTarget && zoomNext != curPar.zoomTarget) {
                    computeZoomSlide(zoomNext);
                }
                curPar.zoomTarget = zoomNext;
                smoothShift = true;
                showZoomIndicator();
            }
        } break;

        case ActionENUM::zoomOut: {
            if (curPar.zoomIndex > 0) {
                curPar.zoomIndex--;

                auto zoomNext = curPar.zoomList[curPar.zoomIndex];
                if (curPar.zoomTarget && zoomNext != curPar.zoomTarget) {
                    computeZoomSlide(zoomNext);
                }
                curPar.zoomTarget = zoomNext;
                smoothShift = true;
                showZoomIndicator();
            }
        } break;

        case ActionENUM::zoomPercent: {
            const int percent = std::clamp(
                operateAction.value1,
                ZoomEditPolicy::MIN_PERCENT,
                ZoomEditPolicy::MAX_PERCENT);
            const auto zoomNext = std::max<int64_t>(1, static_cast<int64_t>(std::llround(
                percent * CurImageParameter::ZOOM_BASE / 100.0)));
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget)
                computeZoomSlide(zoomNext);
            curPar.selectZoomTarget(zoomNext);
            smoothShift = true;
            showZoomIndicator();
        } break;

        case ActionENUM::zoomFix: {
            if (curPar.zoomIndex == curPar.zoomIndex100percent)
                curPar.zoomIndex = curPar.zoomIndexFix;
            else
                curPar.zoomIndex = curPar.zoomIndex100percent;

            auto zoomNext = curPar.zoomList[curPar.zoomIndex];
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget) {
                computeZoomSlide(zoomNext);
            }
            curPar.zoomTarget = zoomNext;
            smoothShift = true;
        } break;

        case ActionENUM::zoomFit: {
            curPar.zoomIndex = curPar.zoomIndexFix;
            const auto zoomNext = curPar.zoomList[curPar.zoomIndex];
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget)
                computeZoomSlide(zoomNext);
            curPar.zoomTarget = zoomNext;
            smoothShift = true;
        } break;

        case ActionENUM::zoomActual: {
            curPar.zoomIndex = curPar.zoomIndex100percent;
            const auto zoomNext = curPar.zoomList[curPar.zoomIndex];
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget)
                computeZoomSlide(zoomNext);
            curPar.zoomTarget = zoomNext;
            smoothShift = true;
        } break;

        case ActionENUM::rotateLeft: {
            if (GlobalVar::settingParameter.isAllowRotateAnimation) {
                rotateLeftAnimation();
            }
            curPar.rotation = (curPar.rotation + 1) & 0b11;
            curPar.slideTargetRotateLeft();
            if (presentationMode)
                applyPresentationImageLayout();
            else if (framedWindowAnchored)
                applyAnchoredWindowImageLayout();
            else
                curPar.updateZoomList(winWidth, winHeight);
            persistCurrentRotation();
        } break;

        case ActionENUM::rotateRight: {
            if (GlobalVar::settingParameter.isAllowRotateAnimation) {
                rotateRightAnimation();
            }
            curPar.rotation = (curPar.rotation + 4 - 1) & 0b11;
            curPar.slideTargetRotateRight();
            if (presentationMode)
                applyPresentationImageLayout();
            else if (framedWindowAnchored)
                applyAnchoredWindowImageLayout();
            else
                curPar.updateZoomList(winWidth, winHeight);
            persistCurrentRotation();
        } break;

        case ActionENUM::flipHorizontal: {
            curPar.flipHorizontal = !curPar.flipHorizontal;
        } break;

        case ActionENUM::flipVertical: {
            curPar.flipVertical = !curPar.flipVertical;
        } break;

        case ActionENUM::toggleFavorite: {
            if (!hasCurrentImagePath())
                break;
            const auto& path = imgFileList[curFileIdx];
            if (favoritePaths.contains(path))
                favoritePaths.erase(path);
            else
                favoritePaths.insert(path);
        } break;

        case ActionENUM::deleteImg: {
            if (imgFileList.empty() || curFileIdx < 0 || curFileIdx >= (int)imgFileList.size()) { break; }

            std::wstring_view target = imgFileList[curFileIdx];
            if (target == m_wndCaption || !std::filesystem::exists(target)) {
                break;
            }

            bool shouldDelete = true;
            if (GlobalVar::settingParameter.isNoteBeforeDelete) {
                auto tips = std::format(L"{}\n\n{}", getUIStringW(7), target);
                shouldDelete = MessageBoxW(m_hWnd, tips.c_str(), getUIStringW(1),
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
            }

            if (!shouldDelete)
                break;

            std::wstring pathBuffer(target);
            pathBuffer.push_back(L'\0'); // SHFileOperation 需要双零终止

            SHFILEOPSTRUCTW fileOp{};
            fileOp.hwnd = m_hWnd;
            fileOp.wFunc = FO_DELETE;
            fileOp.pFrom = pathBuffer.c_str();
            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

            int opResult = SHFileOperationW(&fileOp);
            if (opResult != 0 || fileOp.fAnyOperationsAborted) {
                DWORD lastError = opResult != 0 ? (DWORD)opResult : GetLastError();
                auto errMsg = std::format(L"{} 0x{:08X}", getUIStringW(8), lastError);
                MessageBoxW(m_hWnd, errMsg.c_str(), getUIStringW(1), MB_OK | MB_ICONERROR);
                break;
            }

            rotationStore.erase(target);
            rotationStore.save();

            imgFileList.erase(imgFileList.begin() + curFileIdx);

            if (imgFileList.empty()) {
                imgFileList.emplace_back(m_wndCaption);
                curFileIdx = 0;
                imgDB.put(m_wndCaption, { ImageFormat::Still,
                    imgDB.getHomeMat(GetDpiForWindow(m_hWnd)), {}, {}, getUIString(32) });
            }
            else if (curFileIdx >= (int)imgFileList.size()) {
                curFileIdx = (int)imgFileList.size() - 1;
            }

            curPar.imageAssetPtr = imgDB.getCheckedPtr(
                imgFileList[curFileIdx],
                imgFileList[(curFileIdx + 1) % imgFileList.size()]);
            initCurrentImageParameters();
            if (!hasCurrentImagePath())
                applyHomeWindowSize();
        } break;

        case ActionENUM::requestExit: {
            PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        } break;
        }

        if (curPar.zoomCur != curPar.zoomTarget || curPar.slideCur != curPar.slideTarget) {
            if (GlobalVar::settingParameter.isAllowZoomAnimation && smoothShift) {
                static auto animationStart = std::chrono::steady_clock::now();
                static int64_t zoomInit = 0;
                static int64_t zoomTargetInit = 0;
                static Cood slideInit{}, slideTargetInit{};

                if (zoomTargetInit != curPar.zoomTarget || slideTargetInit != curPar.slideTarget) {
                    animationStart = std::chrono::steady_clock::now();
                    zoomInit = curPar.zoomCur;
                    zoomTargetInit = curPar.zoomTarget;
                    slideInit = curPar.slideCur;
                    slideTargetInit = curPar.slideTarget;
                }

                const double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - animationStart).count();
                const double progress = elapsedMs / ZoomPolicy::ANIMATION_DURATION_MS;
                if (progress >= 1.0) {
                    curPar.zoomCur = curPar.zoomTarget;
                    curPar.slideCur = curPar.slideTarget;
                    smoothShift = false;
                }
                else {
                    const double eased = ZoomPolicy::easeSmoothStep(progress);
                    curPar.zoomCur = (int64_t)std::llround(
                        zoomInit + (zoomTargetInit - zoomInit) * eased);
                    curPar.slideCur.x = (int)std::lround(
                        slideInit.x + (slideTargetInit.x - slideInit.x) * eased);
                    curPar.slideCur.y = (int)std::lround(
                        slideInit.y + (slideTargetInit.y - slideInit.y) * eased);
                }
            }
            else {
                curPar.zoomCur = curPar.zoomTarget;
                curPar.slideCur = curPar.slideTarget;
            }
        }

        cv::Mat srcImg;
        if (curPar.imageAssetPtr->format == ImageFormat::None ||
            curPar.imageAssetPtr->format == ImageFormat::Still) {
            srcImg = curPar.imageAssetPtr->primaryFrame;
        }
        else {
            srcImg = curPar.imageAssetPtr->frames[curPar.curFrameIdx];
            curPar.curFrameDelay = curPar.imageAssetPtr->frameDurations[curPar.curFrameIdx];
        }

        drawCanvas(srcImg, mainCanvas);
        drawExifInfo(mainCanvas);
        drawExtraUI(mainCanvas);
        drawZoomIndicator(mainCanvas);

        const bool pausedAnimation = curPar.imageAssetPtr->format == ImageFormat::Animated &&
            curPar.isAnimationPause;
        std::wstring fileSize;
        for (const auto& row : currentImageInfoModel().basic) {
            if (row.label == "文件大小" || row.label == "Size") {
                fileSize = jarkUtils::utf8ToWstring(row.value);
                break;
            }
        }
        WindowTitlePresentation::Model titleModel{
            .state = pausedAnimation ? std::wstring(getUIStringW(9)) : std::wstring{},
            .current = pausedAnimation ? curPar.curFrameIdx + 1 : curFileIdx + 1,
            .total = pausedAnimation ? curPar.curFrameIdxMax + 1 : static_cast<int>(imgFileList.size()),
            .zoomPercent = ZoomPolicy::displayPercent(curPar.zoomCur, curPar.ZOOM_BASE),
            .pixelWidth = curPar.width,
            .pixelHeight = curPar.height,
            .fileSize = std::move(fileSize),
            .fileName = hasCurrentImagePath() ?
                std::filesystem::path(imgFileList[curFileIdx]).filename().wstring() : m_wndCaption,
            .rotation = curPar.rotation ? std::wstring(curPar.rotation == 1 ? getUIStringW(10) :
                (curPar.rotation == 3 ? getUIStringW(11) : getUIStringW(12))) : std::wstring{},
        };
        const auto title = WindowTitlePresentation::build(titleModel);
        SetWindowTextW(m_hWnd, title.c_str());

        updateMainCanvas();

        if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause == false) {
            if (delayRemain <= 0)
                delayRemain = curPar.curFrameDelay;

            auto nowTimestamp = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(nowTimestamp - lastTimestamp);
            lastTimestamp = nowTimestamp;

            if (frameDuration > elapsed)
                std::this_thread::sleep_for(frameDuration - elapsed);

            delayRemain -= elapsed.count();
            if (delayRemain <= 0) {
                delayRemain = curPar.curFrameDelay;
                curPar.curFrameIdx++;
                if (curPar.curFrameIdx > curPar.curFrameIdxMax) {
                    curPar.curFrameIdx = 0;

                    // 动态帧播放完，若有主图，则是当前实况图像
                    if (!curPar.imageAssetPtr->primaryFrame.empty()) {
                        curPar.imageAssetPtr->format = ImageFormat::Still;
                        initCurrentImageParameters();
                        operateQueue.push({ ActionENUM::refresh });
                    }
                }
            }
        }
    }

    void OnRequestExitOtherWindows() {
        Printer::requestExit();
        Setting::requestExit();
    }
};

void test();

static int runDecodeProbe(const std::wstring& imagePath, const std::wstring& resultPath) {
    ImageDatabase imageDatabase;
    const cv::Mat errorTips = imageDatabase.getErrorTipsMat();
    const ImageAsset asset = imageDatabase.myLoader(imagePath);

    const cv::Mat* decodedFrame = nullptr;
    if (!asset.primaryFrame.empty()) {
        decodedFrame = &asset.primaryFrame;
    }
    else if (!asset.frames.empty()) {
        decodedFrame = &asset.frames.front();
    }

    const bool isErrorPlaceholder = decodedFrame != nullptr
        && decodedFrame->data == errorTips.data
        && decodedFrame->cols == errorTips.cols
        && decodedFrame->rows == errorTips.rows;
    const bool success = asset.format != ImageFormat::None
        && decodedFrame != nullptr
        && !decodedFrame->empty()
        && !isErrorPlaceholder;

    std::ofstream result(std::filesystem::path(resultPath), std::ios::trunc);
    if (!result) {
        return 3;
    }

    const char* format = asset.format == ImageFormat::Animated ? "animated"
        : asset.format == ImageFormat::Still ? "still"
        : "none";
    result << (success ? "OK" : "ERROR") << '\t'
        << (decodedFrame ? decodedFrame->cols : 0) << '\t'
        << (decodedFrame ? decodedFrame->rows : 0) << '\t'
        << asset.frames.size() << '\t'
        << format << '\n';
    return success ? 0 : 2;
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
#ifndef NDEBUG
    AllocConsole();
    FILE* stream;
    freopen_s(&stream, "CON", "w", stdout);//重定向标准输出流
    freopen_s(&stream, "CON", "w", stderr);//重定向错误输出流

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    //test();

    // 限制 PPL 默认调度器最多 4 线程, 必须在任何 concurrency::parallel_* 调用之前设置。
    {
        concurrency::SchedulerPolicy policy(2,
            concurrency::MinConcurrency, 1,
            concurrency::MaxConcurrency, 4);
        concurrency::Scheduler::SetDefaultSchedulerPolicy(policy);
    }

    // OpenCV keeps OpenEXR disabled unless this opt-in is present before the
    // first image-codec call. EXR is an advertised YeImageViewer format.
    ::SetEnvironmentVariableW(L"OPENCV_IO_ENABLE_OPENEXR", L"1");
    ::_wputenv_s(L"OPENCV_IO_ENABLE_OPENEXR", L"1");

    Exiv2::enableBMFF();
    ::ImmDisableIME(GetCurrentThreadId()); // 禁用输入法，防止干扰按键操作

    ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    if (!SUCCEEDED(::CoInitialize(nullptr)))
        return 0;

    int argumentCount = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argumentCount);
    if (arguments != nullptr && argumentCount == 4
        && std::wstring_view(arguments[1]) == L"--decode-probe") {
        const int probeResult = runDecodeProbe(arguments[2], arguments[3]);
        ::LocalFree(arguments);
        ::CoUninitialize();
        return probeResult;
    }
    if (arguments != nullptr) {
        ::LocalFree(arguments);
    }

    wstring filePath = lpCmdLine;
    if (!filePath.empty() && filePath.front() == '\"') {
        filePath = filePath.substr(1);
    }
    if (!filePath.empty() && filePath.back() == '\"') {
        filePath.pop_back();
    }

    YeImageViewerApp app(!filePath.empty());
    if (SUCCEEDED(app.InitWindow(hInstance))) {
        app.initOpenFile(filePath);
        app.enterPresentationMode();
        app.DrawScene();
        app.ShowInitialWindow();
        app.Run();
    }
    else {
        MessageBoxW(NULL, getUIStringW(13), getUIStringW(14), MB_ICONERROR);
    }

    ::CoUninitialize();
    return 0;
}

void test() {
    std::ifstream file("D:\\Downloads\\test\\22.wp2", std::ios::binary);
    auto buf = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});

    exit(0);
}
