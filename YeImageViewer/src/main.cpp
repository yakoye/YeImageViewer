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
#include "WheelInput.h"

#include "D3D11App.h"
#include <ppl.h>
#include <concrt.h>
#include <shellapi.h>

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
std::wstring_view appVersion = L"v1.36.17";
constinit int appVersionCode = 13617; // 主版本*10000 + 次版本*100 + 修订版本

std::wstring_view RepositoryLink = L"https://github.com/yakoye/YeImageViewer";


static constexpr auto generate_zoom_list() {
    // 原始缩放级别数组（2^10 到 2^22）
    constexpr std::array<int64_t, 13> base = {
        1 << 10, 1 << 11, 1 << 12, 1 << 13, 1 << 14,
        1 << 15, 1 << 16, 1 << 17, 1 << 18, 1 << 19,
        1 << 20, 1 << 21, 1 << 22
    };
    constexpr double baseScale = 1.148698354997035;// std::pow(2.0, 0.2);

    std::array<int64_t, 5 * base.size() - 4> result{};

    size_t index = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        result[index++] = base[i];

        if (i < base.size() - 1) {
            result[index++] = (int64_t)(base[i] * baseScale);
            result[index++] = (int64_t)(base[i] * baseScale * baseScale);
            result[index++] = (int64_t)(base[i] * baseScale * baseScale * baseScale);
            result[index++] = (int64_t)(base[i] * baseScale * baseScale * baseScale * baseScale);
        }
    }
    return result;
}


struct CurImageParameter {
    static constexpr auto ZOOM_LIST = generate_zoom_list();
    static constexpr int64_t ZOOM_BASE = (1 << 16); // 100%缩放

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
            curFrameIdxMax = imageAssetPtr->format == ImageFormat::Animated ? (int)imageAssetPtr->frames.size() - 1 : 1;

            if (imageAssetPtr->format == ImageFormat::Animated) {
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
            int64_t zoomFitWindow = std::min(winWidth * ZOOM_BASE / displayWidth, winHeight * ZOOM_BASE / displayHeight);
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
    bool showExif = false;
    Cood mousePos, mousePressPos;
    ImageDatabase imgDB;
    RotationStore rotationStore;
    std::unordered_set<std::wstring> favoritePaths;
    bool presentationMode = false;
    bool framedWindowAnchored = false;
    bool presentationClickCandidate = false;
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
    CurImageParameter curPar;
    ExtraUIRes extraUIRes;
    std::chrono::steady_clock::time_point lastClickTimestamp{}, lastWinResizeTimestamp{};

    YeImageViewerApp() {
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

    void initCurrentImageParameters() {
        const bool isRealImage = hasCurrentImagePath();
        const int savedRotation = isRealImage ? rotationStore.get(imgFileList[curFileIdx]) : 0;
        curPar.Init(winWidth, winHeight, savedRotation, isRealImage);
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
        curPar.slideCur = curPar.slideTarget = { 0, 0 };
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

    void enterPresentationMode() {
        if (presentationMode || !hasCurrentImagePath())
            return;

        presentationWindowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE));
        presentationWindowedExtendedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE));
        framedWindowAnchored = false;
        presentationMode = true;

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
        framedWindowAnchored = true;
        presentationClickCandidate = false;
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

        curFileIdx = -1;
        imgFileList.clear();
        imgDB.clear();

        if (filePath.empty()) {
            imgFileList.emplace_back(m_wndCaption);
            curFileIdx = 0;
            imgDB.put(m_wndCaption, { ImageFormat::Still, imgDB.getHomeMat(), {}, {}, getUIString(32) });
            curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[curFileIdx]);
            initCurrentImageParameters();
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
            std::sort(fileNameList.begin(), fileNameList.end(), [](std::wstring_view a, std::wstring_view b) -> bool {
                return StrCmpLogicalW(a.data(), b.data()) < 0; });

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
                imgDB.put(m_wndCaption, { ImageFormat::Still, imgDB.getHomeMat(), {}, {}, getUIString(32) });
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

        curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
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
                mouseIsPressing = false;
                operateQueue.push({ ActionENUM::requestExit });
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

            if (cursorPos == CursorPos::leftEdge || cursorPos == CursorPos::toolbarPrevious)
                operateQueue.push({ ActionENUM::preImg });
            else if (cursorPos == CursorPos::rightEdge || cursorPos == CursorPos::toolbarNext)
                operateQueue.push({ ActionENUM::nextImg });
            else if (cursorPos == CursorPos::toolbarRotateLeft)
                operateQueue.push({ ActionENUM::rotateLeft });
            else if (cursorPos == CursorPos::toolbarRotateRight)
                operateQueue.push({ ActionENUM::rotateRight });
            else if (cursorPos == CursorPos::toolbarFlipHorizontal)
                operateQueue.push({ ActionENUM::flipHorizontal });
            else if (cursorPos == CursorPos::toolbarFlipVertical)
                operateQueue.push({ ActionENUM::flipVertical });
            else if (cursorPos == CursorPos::toolbarZoomFit)
                operateQueue.push({ ActionENUM::zoomFit });
            else if (cursorPos == CursorPos::toolbarZoomActual)
                operateQueue.push({ ActionENUM::zoomActual });
            else if (cursorPos == CursorPos::toolbarFullscreen)
                operateQueue.push({ ActionENUM::toggleFullScreen });
            else if (cursorPos == CursorPos::toolbarFavorite)
                operateQueue.push({ ActionENUM::toggleFavorite });
            else if (cursorPos == CursorPos::toolbarCopy)
                operateQueue.push({ ActionENUM::copyImage });
            else if (cursorPos == CursorPos::toolbarDelete)
                operateQueue.push({ ActionENUM::deleteImg });
            else if (cursorPos == CursorPos::toolbarSetting)
                operateQueue.push({ ActionENUM::setting, 0 });
            else if (cursorPos == CursorPos::toolbarZoomOut)
                operateQueue.push({ ActionENUM::zoomOut });
            else if (cursorPos == CursorPos::toolbarZoomIn)
                operateQueue.push({ ActionENUM::zoomIn });
            else if (presentationMode && cursorPos == CursorPos::presentationClose)
                operateQueue.push({ ActionENUM::requestExit });
            else if (cursorPos == CursorPos::centerTop) {
                handleAnimationControl(x, y);
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
            case CursorPos::toolbarZoomIn:
            case CursorPos::toolbarPrevious:
            case CursorPos::toolbarNext:
            case CursorPos::toolbar:
                extraUIFlag = ShowExtraUI::bottomToolbar;
                break;
            }

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
        cursorPosLast = cursorPos = CursorPos::centerArea;
        extraUIFlag = ShowExtraUI::none;
        mouseIsPressing = false;
        operateQueue.push({ ActionENUM::refresh });
    }

    void OnMouseWheel(UINT nFlags, short zDelta, int x, int y) override {
        const int panStep = std::max(1, (winWidth + winHeight) / 16);
        const auto modifiedWheel = WheelInput::resolve(nFlags, zDelta, panStep);
        switch (modifiedWheel.intent) {
        case WheelInput::Intent::PanVertical:
            operateQueue.push({ ActionENUM::slide, 0, modifiedWheel.verticalDelta });
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
        if (presentationMode) {
            exitPresentationMode();
            return;
        }
        const auto action = EscapeBehavior::resolve(
            jarkUtils::IsFullScreen(m_hWnd), IsZoomed(m_hWnd),
            GlobalVar::settingParameter.escapeClosesImage);
        switch (action) {
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

    void OnKeyDown(WPARAM keyValue) override {
        if (ctrlIsPressing) {
            switch (keyValue)
            {
            case 'O': { // Ctrl + O  打开图片
                wstring filePath = jarkUtils::SelectFile(m_hWnd);
                if (!filePath.empty()) {
                    initOpenFile(filePath);
                    operateQueue.push({ ActionENUM::refresh });
                }
                ctrlIsPressing = false; // 上面弹出窗口导致收不到CTRL键释放的消息
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
        switch ((ContextMenu)wParam) {
        case ContextMenu::openNewImage: {
            wstring filePath = jarkUtils::SelectFile(m_hWnd);
            if (!filePath.empty()) {
                initOpenFile(filePath);
                operateQueue.push({ ActionENUM::refresh });
            }
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
        const intUnion* srcPtr = (intUnion*)srcImg.ptr();
        const int srcW = srcImg.cols;

        intUnion srcPx = srcPtr[srcW * srcY + srcX];

        if (isLowZoom && srcY > 0 && srcX > 0) { // 简单临近像素平均
            intUnion px1 = srcPtr[srcW * (srcY - 1) + srcX - 1];
            intUnion px2 = srcPtr[srcW * (srcY - 1) + srcX];
            intUnion px3 = srcPtr[srcW * srcY + srcX - 1];
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

    static void blendInfoPanel(cv::Mat& canvas, cv::Rect rect, uint32_t color) {
        rect &= cv::Rect{ 0, 0, canvas.cols, canvas.rows };
        for (int y = rect.y; y < rect.y + rect.height; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(canvas.ptr(y));
            for (int x = rect.x; x < rect.x + rect.width; ++x)
                row[x] = ImageInfoPresentation::blendBgra(row[x], color);
        }
    }

    const ImageInfoPresentation::Model& currentImageInfoModel() {
        const ImageAsset* asset = curPar.imageAssetPtr.get();
        const uint32_t language = GlobalVar::settingParameter.UI_LANG;
        if (asset != imageInfoAssetCache || language != imageInfoLanguageCache) {
            imageInfoAssetCache = asset;
            imageInfoLanguageCache = language;
            imageInfoModelCache = ImageInfoPresentation::build(
                asset ? asset->exifInfo : std::string_view{}, language == 0);
        }
        return imageInfoModelCache;
    }

    void drawFramedImageInfoCard(cv::Mat& canvas) {
        if (canvas.cols < 160 || canvas.rows < 160)
            return;

        const auto& model = currentImageInfoModel();
        const UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
        const auto scaled = [dpi](int logical) {
            return TextRenderingPolicy::scaledPixelSize(logical, dpi);
            };
        const int margin = scaled(12);
        const int fontSize = scaled(ImageInfoPresentation::LOGICAL_FONT_SIZE);
        const int headerHeight = scaled(ImageInfoPresentation::LOGICAL_HEADER_HEIGHT);
        const int sectionHeight = scaled(ImageInfoPresentation::LOGICAL_SECTION_HEIGHT);
        const int rowHeight = scaled(ImageInfoPresentation::LOGICAL_ROW_HEIGHT);
        const int footerHeight = scaled(ImageInfoPresentation::LOGICAL_FOOTER_HEIGHT);
        const int panelWidth = std::min(scaled(ImageInfoPresentation::LOGICAL_PANEL_WIDTH),
            canvas.cols - margin * 2);
        const int basicHeight = headerHeight + sectionHeight +
            static_cast<int>(model.basic.size()) * rowHeight + footerHeight + scaled(16);
        const int availableHeight = canvas.rows - margin * 2;
        int detailRows = 0;
        if (!model.details.empty() && availableHeight > basicHeight + sectionHeight + rowHeight)
            detailRows = std::min(static_cast<int>(model.details.size()),
                (availableHeight - basicHeight - sectionHeight - 1) / rowHeight);
        const int panelHeight = std::min(availableHeight, basicHeight +
            (detailRows > 0 ? 1 + sectionHeight + detailRows * rowHeight : 0));
        const cv::Rect panel{ margin, margin, panelWidth, panelHeight };

        blendInfoPanel(canvas, panel, ImageInfoPresentation::PANEL_BACKGROUND);
        cv::rectangle(canvas, panel,
            jarkUtils::to_cv_scalar(ImageInfoPresentation::PANEL_BORDER), 1);
        cv::rectangle(canvas, { panel.x, panel.y, panel.width, scaled(3) },
            jarkUtils::to_cv_scalar(ImageInfoPresentation::PANEL_ACCENT), -1);

        textDrawer.setSize(fontSize);
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        textDrawer.putAlignLeft(canvas,
            { panel.x + scaled(16), panel.y, panel.width / 2, headerHeight },
            chinese ? "图像信息" : "Image information",
            ImageInfoPresentation::TEXT_PRIMARY);
        const std::string position = std::format("{} / {}", curFileIdx + 1, imgFileList.size());
        textDrawer.putAlignCenter(canvas,
            { panel.x + panel.width - scaled(96), panel.y, scaled(80), headerHeight },
            position.c_str(), ImageInfoPresentation::TEXT_MUTED);

        int y = panel.y + headerHeight;
        const int labelWidth = scaled(94);
        const int contentX = panel.x + scaled(16);
        const int contentWidth = panel.width - scaled(32);
        const auto drawSection = [&](const char* title,
            const std::vector<ImageInfoPresentation::Row>& rows, int rowLimit, int& sectionY) {
                textDrawer.putAlignLeft(canvas,
                    { contentX, sectionY, contentWidth, sectionHeight }, title,
                    ImageInfoPresentation::TEXT_MUTED);
                sectionY += sectionHeight;
                for (int index = 0; index < rowLimit; ++index) {
                    const auto& row = rows[index];
                    textDrawer.putAlignLeft(canvas,
                        { contentX, sectionY, labelWidth, rowHeight }, row.label.c_str(),
                        ImageInfoPresentation::TEXT_MUTED);
                    textDrawer.putAlignLeft(canvas,
                        { contentX + labelWidth, sectionY, contentWidth - labelWidth, rowHeight },
                        row.value.c_str(), ImageInfoPresentation::TEXT_SECONDARY);
                    sectionY += rowHeight;
                }
            };

        drawSection(chinese ? "基本信息" : "BASIC",
            model.basic, static_cast<int>(model.basic.size()), y);
        if (detailRows > 0) {
            cv::line(canvas, { contentX, y }, { contentX + contentWidth, y },
                jarkUtils::to_cv_scalar(ImageInfoPresentation::PANEL_BORDER), 1);
            ++y;
            drawSection(chinese ? "拍摄信息" : "CAPTURE", model.details, detailRows, y);
        }

        const int footerY = panel.y + panel.height - footerHeight;
        cv::line(canvas, { panel.x, footerY }, { panel.x + panel.width, footerY },
            jarkUtils::to_cv_scalar(ImageInfoPresentation::PANEL_BORDER), 1);
        const cv::Rect keyRect{ contentX, footerY + scaled(7), scaled(28), footerHeight - scaled(14) };
        cv::rectangle(canvas, keyRect,
            jarkUtils::to_cv_scalar(ImageInfoPresentation::PANEL_BORDER), 1);
        textDrawer.putAlignCenter(canvas, keyRect, "C", ImageInfoPresentation::TEXT_SECONDARY);
        textDrawer.putAlignLeft(canvas,
            { keyRect.x + keyRect.width + scaled(8), footerY, contentWidth - keyRect.width, footerHeight },
            chinese ? "复制全部信息" : "Copy all information",
            ImageInfoPresentation::TEXT_MUTED);
    }

    void drawExifInfo(cv::Mat& canvas) {
        if (!showExif)
            return;

        if (!presentationMode) {
            drawFramedImageInfoCard(canvas);
            return;
        }

        const int padding = 10;
        const int areaWidth = (canvas.cols - 2 * padding) / 4;
        cv::Rect rect{ padding, padding, std::max(areaWidth, 400), canvas.rows - 2 * padding };
        rect &= cv::Rect{ 0, 0, canvas.cols, canvas.rows };
        if (rect.empty())
            return;
        const UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 96;
        const int fontSize = TextRenderingPolicy::legacyImmersiveExifPixelSize(dpi);
        if (textDrawer.getSize() != fontSize)
            textDrawer.setSize(fontSize);
        textDrawer.putAlignLeft(canvas, rect, curPar.imageAssetPtr->exifInfo.c_str(),
            GlobalVar::currentTheme.FG, true, false);
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
        const OverlayLayout::Rect& target, uint32_t tint = 0) {
        if (source.empty() || target.width <= 0 || target.height <= 0)
            return;
        const int iconSize = std::max(10, std::min({ target.width - 8, target.height - 8,
            OverlayLayout::scaled(OverlayLayout::BASE_ICON_SIZE,
                OverlayLayout::toolbarScale(canvas.cols)) }));
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

    void drawViewerTopBar(cv::Mat& canvas) {
        const int barHeight = std::min(58, canvas.rows / 4);
        for (int y = 0; y < barHeight; ++y) {
            const uint32_t alpha = static_cast<uint32_t>(210 * (barHeight - y) / barHeight);
            blendInfoPanel(canvas, { 0, y, canvas.cols, 1 }, alpha << 24);
        }

        textDrawer.setSize(14);
        const std::string position = std::format("{} / {}", curFileIdx + 1, imgFileList.size());
        std::string fileName = jarkUtils::wstringToUtf8(appName);
        if (hasCurrentImagePath())
            fileName = jarkUtils::wstringToUtf8(std::filesystem::path(
                imgFileList[curFileIdx]).filename().wstring());
        textDrawer.putAlignLeft(canvas, { 16, 8, 64, 30 }, position.c_str(), 0xFFE8EAF0u);
        textDrawer.putAlignLeft(canvas, { 78, 8, std::max(40, canvas.cols - 390), 30 },
            fileName.c_str(), 0xFFB8BECCu);

        const auto& model = currentImageInfoModel();
        std::string dimensions = std::format("{} × {}", curPar.width, curPar.height);
        std::string fileSize;
        for (const auto& row : model.basic) {
            if (row.label == "分辨率" || row.label == "Dimensions")
                dimensions = row.value;
            else if (row.label == "文件大小" || row.label == "Size")
                fileSize = row.value;
        }
        const int closeReserve = presentationMode ? 66 : 12;
        const int right = canvas.cols - closeReserve;
        const int sizeWidth = fileSize.empty() ? 0 : 104;
        if (sizeWidth > 0) {
            const OverlayLayout::Rect badge{ right - sizeWidth, 10, sizeWidth - 8, 26 };
            auto surface = roundedSurface(badge.width, badge.height, 6, 0x0FFFFFFFu);
            jarkUtils::overlayImg(canvas, surface, badge.x, badge.y);
            textDrawer.putAlignCenter(canvas, toCvRect(badge), fileSize.c_str(), 0xFFB8BECCu);
        }
        const OverlayLayout::Rect dimensionBadge{ right - sizeWidth - 116, 10, 108, 26 };
        auto surface = roundedSurface(dimensionBadge.width, dimensionBadge.height, 6, 0x0FFFFFFFu);
        jarkUtils::overlayImg(canvas, surface, dimensionBadge.x, dimensionBadge.y);
        textDrawer.putAlignCenter(canvas, toCvRect(dimensionBadge), dimensions.c_str(), 0xFFB8BECCu);
    }

    const char* toolbarTooltip() const {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        switch (cursorPos) {
        case CursorPos::toolbarPrevious: return chinese ? "上一张" : "Previous";
        case CursorPos::toolbarNext: return chinese ? "下一张" : "Next";
        case CursorPos::toolbarRotateLeft: return chinese ? "左旋转 90°" : "Rotate left";
        case CursorPos::toolbarRotateRight: return chinese ? "右旋转 90°" : "Rotate right";
        case CursorPos::toolbarFlipHorizontal: return chinese ? "左右镜像" : "Flip horizontal";
        case CursorPos::toolbarFlipVertical: return chinese ? "上下镜像" : "Flip vertical";
        case CursorPos::toolbarZoomFit: return chinese ? "适应窗口" : "Fit to window";
        case CursorPos::toolbarZoomActual: return chinese ? "实际大小 (1:1)" : "Actual size (1:1)";
        case CursorPos::toolbarFullscreen: return presentationMode ?
            (chinese ? "退出沉浸" : "Exit immersive") : (chinese ? "沉浸显示" : "Immersive view");
        case CursorPos::toolbarFavorite: return chinese ? "收藏图片" : "Favorite";
        case CursorPos::toolbarCopy: return chinese ? "复制图像" : "Copy image";
        case CursorPos::toolbarDelete: return chinese ? "删除图片" : "Delete image";
        case CursorPos::toolbarSetting: return chinese ? "设置" : "Settings";
        case CursorPos::toolbarZoomOut: return chinese ? "缩小" : "Zoom out";
        case CursorPos::toolbarZoomIn: return chinese ? "放大" : "Zoom in";
        default: return nullptr;
        }
    }

    void drawToolbarButton(cv::Mat& canvas, const OverlayLayout::Rect& rect,
        const cv::Mat& icon, CursorPos expectedCursor, bool active = false,
        bool danger = false) {
        const bool hovered = cursorPos == expectedCursor;
        if (hovered || active) {
            const uint32_t background = active ? 0x383B82F6u :
                (danger ? 0x1FEF4444u : 0x12FFFFFFu);
            auto surface = roundedSurface(rect.width, rect.height,
                std::max(4, rect.width / 3), background);
            jarkUtils::overlayImg(canvas, surface, rect.x, rect.y);
        }
        drawOverlayIcon(canvas, icon, rect, active ? 0xFF60A5FAu : 0);
        if (active) {
            cv::circle(canvas, { rect.x + rect.width / 2, rect.y + rect.height - 4 },
                2, jarkUtils::to_cv_scalar(0xFF60A5FAu), -1, cv::LINE_AA);
        }
    }

    void drawSideButton(cv::Mat& canvas, const OverlayLayout::Rect& rect,
        const cv::Mat& icon, bool hovered) {
        auto surface = roundedSurface(rect.width, rect.height, 11,
            hovered ? 0xD10D0F14u : 0xB30D0F14u, 0x17FFFFFFu);
        jarkUtils::overlayImg(canvas, surface, rect.x, rect.y);
        drawOverlayIcon(canvas, icon, rect);
    }

    void drawViewerToolbar(cv::Mat& canvas) {
        const auto toolbar = OverlayLayout::toolbarRect(canvas.cols, canvas.rows);
        auto pill = roundedSurface(toolbar.width, toolbar.height,
            std::max(8, toolbar.height / 3), 0xD10D0F14u, 0x17FFFFFFu);
        jarkUtils::overlayImg(canvas, pill, toolbar.x, toolbar.y);

        const int scale = OverlayLayout::toolbarScale(canvas.cols);
        for (const int baseX : { 77, 229, 346, 498 }) {
            const int x = toolbar.x + OverlayLayout::scaled(
                OverlayLayout::BASE_TOOLBAR_PADDING + baseX, scale);
            const int half = OverlayLayout::scaled(10, scale);
            cv::line(canvas, { x, toolbar.y + toolbar.height / 2 - half },
                { x, toolbar.y + toolbar.height / 2 + half },
                cv::Scalar(255, 255, 255, 26), 1, cv::LINE_AA);
        }

        const bool favorite = hasCurrentImagePath() && favoritePaths.contains(imgFileList[curFileIdx]);
        drawToolbarButton(canvas, OverlayLayout::toolbarPreviousRect(canvas.cols, canvas.rows),
            extraUIRes.leftArrow, CursorPos::toolbarPrevious);
        drawToolbarButton(canvas, OverlayLayout::toolbarNextRect(canvas.cols, canvas.rows),
            extraUIRes.rightArrow, CursorPos::toolbarNext);
        drawToolbarButton(canvas, OverlayLayout::rotateLeftRect(canvas.cols, canvas.rows),
            extraUIRes.leftRotate, CursorPos::toolbarRotateLeft);
        drawToolbarButton(canvas, OverlayLayout::rotateRightRect(canvas.cols, canvas.rows),
            extraUIRes.rightRotate, CursorPos::toolbarRotateRight);
        drawToolbarButton(canvas, OverlayLayout::flipHorizontalRect(canvas.cols, canvas.rows),
            extraUIRes.flipHorizontal, CursorPos::toolbarFlipHorizontal, curPar.flipHorizontal);
        drawToolbarButton(canvas, OverlayLayout::flipVerticalRect(canvas.cols, canvas.rows),
            extraUIRes.flipVertical, CursorPos::toolbarFlipVertical, curPar.flipVertical);
        drawToolbarButton(canvas, OverlayLayout::zoomFitRect(canvas.cols, canvas.rows),
            extraUIRes.fitWindow, CursorPos::toolbarZoomFit,
            curPar.zoomIndex == curPar.zoomIndexFix);
        drawToolbarButton(canvas, OverlayLayout::zoomActualRect(canvas.cols, canvas.rows),
            extraUIRes.actualSize, CursorPos::toolbarZoomActual,
            curPar.zoomIndex == curPar.zoomIndex100percent);
        drawToolbarButton(canvas, OverlayLayout::fullscreenRect(canvas.cols, canvas.rows),
            extraUIRes.fullscreen, CursorPos::toolbarFullscreen, presentationMode);
        drawToolbarButton(canvas, OverlayLayout::favoriteRect(canvas.cols, canvas.rows),
            extraUIRes.favorite, CursorPos::toolbarFavorite, favorite);
        drawToolbarButton(canvas, OverlayLayout::copyImageRect(canvas.cols, canvas.rows),
            extraUIRes.copy, CursorPos::toolbarCopy);
        drawToolbarButton(canvas, OverlayLayout::deleteImageRect(canvas.cols, canvas.rows),
            extraUIRes.deleteImage, CursorPos::toolbarDelete, false, true);
        drawToolbarButton(canvas, OverlayLayout::settingsRect(canvas.cols, canvas.rows),
            extraUIRes.setting, CursorPos::toolbarSetting);
        drawToolbarButton(canvas, OverlayLayout::zoomOutRect(canvas.cols, canvas.rows),
            extraUIRes.zoomOut, CursorPos::toolbarZoomOut);
        drawToolbarButton(canvas, OverlayLayout::zoomInRect(canvas.cols, canvas.rows),
            extraUIRes.zoomIn, CursorPos::toolbarZoomIn);

        textDrawer.setSize(12);
        const std::string zoomText = std::format("{}%", (curPar.zoomCur * 100 +
            CurImageParameter::ZOOM_BASE / 2) / CurImageParameter::ZOOM_BASE);
        textDrawer.putAlignCenter(canvas,
            toCvRect(OverlayLayout::zoomTextRect(canvas.cols, canvas.rows)),
            zoomText.c_str(), 0xFFB8BECCu);

        if (const char* tooltip = toolbarTooltip()) {
            OverlayLayout::Rect hovered{};
            switch (cursorPos) {
            case CursorPos::toolbarPrevious: hovered = OverlayLayout::toolbarPreviousRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarNext: hovered = OverlayLayout::toolbarNextRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarRotateLeft: hovered = OverlayLayout::rotateLeftRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarRotateRight: hovered = OverlayLayout::rotateRightRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFlipHorizontal: hovered = OverlayLayout::flipHorizontalRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFlipVertical: hovered = OverlayLayout::flipVerticalRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomFit: hovered = OverlayLayout::zoomFitRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomActual: hovered = OverlayLayout::zoomActualRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFullscreen: hovered = OverlayLayout::fullscreenRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarFavorite: hovered = OverlayLayout::favoriteRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarCopy: hovered = OverlayLayout::copyImageRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarDelete: hovered = OverlayLayout::deleteImageRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarSetting: hovered = OverlayLayout::settingsRect(canvas.cols, canvas.rows); break;
            case CursorPos::toolbarZoomOut: hovered = OverlayLayout::zoomOutRect(canvas.cols, canvas.rows); break;
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

        // The reference header is persistent; only the navigation controls use
        // the deliberately small hover-reveal regions requested for the viewer.
        drawViewerTopBar(canvas);

        switch (extraUIFlag) {
        case ShowExtraUI::bottomToolbar: {
            const auto previous = OverlayLayout::previousImageIconRect(canvasWidth, canvasHeight);
            const auto next = OverlayLayout::nextImageIconRect(canvasWidth, canvasHeight);
            drawSideButton(canvas, previous, extraUIRes.leftArrow,
                cursorPos == CursorPos::leftEdge);
            drawSideButton(canvas, next, extraUIRes.rightArrow,
                cursorPos == CursorPos::rightEdge);
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

    void updateMainCanvas() {
        PresentCanvas(mainCanvas.ptr(), mainCanvas.cols, mainCanvas.rows, (int)mainCanvas.step);
    }


    int64_t delayRemain = 0;
    const std::chrono::milliseconds frameDuration{ 10 };
    std::chrono::steady_clock::time_point lastTimestamp = std::chrono::steady_clock::now();


    void DrawScene() {
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
                    imgDB.put(m_wndCaption, { ImageFormat::Still, imgDB.getHomeMat(), {}, {}, getUIString(32) });
                    curPar.imageAssetPtr = imgDB.getSafePtr(currentPath, currentPath);
                }
                else {
                    const auto& nextPath = imgFileList[(curFileIdx + 1) % imgFileList.size()];
                    curPar.imageAssetPtr = imgDB.getSafePtr(currentPath, nextPath);
                }

                initCurrentImageParameters();
                operateQueue.push({ ActionENUM::refresh });
            }
        }

        auto operateAction = operateQueue.get();
        if (operateAction.action == ActionENUM::none &&
            curPar.zoomCur == curPar.zoomTarget &&
            curPar.slideCur == curPar.slideTarget &&
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
            curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + imgFileList.size() - 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToPreAnimationVertical();      // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToPreAnimationHorizontal();    // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
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
            curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
            initCurrentImageParameters();

            if (GlobalVar::settingParameter.switchImageAnimationMode == 1)
                mainCanvasSlideToNextAnimationVertical();   // 竖直滑动
            else if (GlobalVar::settingParameter.switchImageAnimationMode == 2)
                mainCanvasSlideToNextAnimationHorizontal(); // 水平滑动

            lastTimestamp = std::chrono::steady_clock::now();
            delayRemain = 0;
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
            curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + imgFileList.size() - 1) % imgFileList.size()]);
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
            curPar.imageAssetPtr = imgDB.getSafePtr(imgFileList[curFileIdx], imgFileList[(curFileIdx + 1) % imgFileList.size()]);
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

        case ActionENUM::toggleExif: {
            showExif = !showExif;
        } break;

        case ActionENUM::zoomIn: {
            if (curPar.zoomIndex < curPar.zoomList.size() - 1) {
                curPar.zoomIndex++;

                auto zoomNext = curPar.zoomList[curPar.zoomIndex];
                if (curPar.zoomTarget && zoomNext != curPar.zoomTarget) {
                    computeZoomSlide(zoomNext);
                }
                curPar.zoomTarget = zoomNext;
                smoothShift = !curPar.imageAssetPtr->svgRenderer;
            }
        } break;

        case ActionENUM::zoomOut: {
            // 不宜缩太小
            if (curPar.zoomTarget <= curPar.ZOOM_BASE && (curPar.zoomTarget * std::min(curPar.width, curPar.height) / curPar.ZOOM_BASE) < 4)
                break;

            if (curPar.zoomIndex > 0) {
                curPar.zoomIndex--;

                auto zoomNext = curPar.zoomList[curPar.zoomIndex];
                if (curPar.zoomTarget && zoomNext != curPar.zoomTarget) {
                    computeZoomSlide(zoomNext);
                }
                curPar.zoomTarget = zoomNext;
                smoothShift = !curPar.imageAssetPtr->svgRenderer;
            }
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
            smoothShift = !curPar.imageAssetPtr->svgRenderer;
        } break;

        case ActionENUM::zoomFit: {
            curPar.zoomIndex = curPar.zoomIndexFix;
            const auto zoomNext = curPar.zoomList[curPar.zoomIndex];
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget)
                computeZoomSlide(zoomNext);
            curPar.zoomTarget = zoomNext;
            smoothShift = !curPar.imageAssetPtr->svgRenderer;
        } break;

        case ActionENUM::zoomActual: {
            curPar.zoomIndex = curPar.zoomIndex100percent;
            const auto zoomNext = curPar.zoomList[curPar.zoomIndex];
            if (curPar.zoomTarget && zoomNext != curPar.zoomTarget)
                computeZoomSlide(zoomNext);
            curPar.zoomTarget = zoomNext;
            smoothShift = !curPar.imageAssetPtr->svgRenderer;
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
                imgDB.put(m_wndCaption, { ImageFormat::Still, imgDB.getHomeMat(), {}, {}, getUIString(32) });
            }
            else if (curFileIdx >= (int)imgFileList.size()) {
                curFileIdx = (int)imgFileList.size() - 1;
            }

            curPar.imageAssetPtr = imgDB.getSafePtr(
                imgFileList[curFileIdx],
                imgFileList[(curFileIdx + 1) % imgFileList.size()]);
            initCurrentImageParameters();
        } break;

        case ActionENUM::requestExit: {
            PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        } break;
        }

        if (curPar.zoomCur != curPar.zoomTarget || curPar.slideCur != curPar.slideTarget) {
            if (GlobalVar::settingParameter.isAllowZoomAnimation && smoothShift) { // 简单缩放动画
                const int progressMax = 1 << 8;
                static int progressCnt = progressMax;
                static int64_t zoomInit = 0;
                static int64_t zoomTargetInit = 0;
                static Cood slideInit{}, slideTargetInit{};

                //未开始进行动画 或 动画未完成就有新缩放操作
                if (progressCnt >= progressMax || zoomTargetInit != curPar.zoomTarget || slideTargetInit != curPar.slideTarget) {
                    progressCnt = 1;
                    zoomInit = curPar.zoomCur;
                    zoomTargetInit = curPar.zoomTarget;
                    slideInit = curPar.slideCur;
                    slideTargetInit = curPar.slideTarget;
                }
                else {
                    auto addDelta = ((progressMax - progressCnt) / 4);
                    if (addDelta <= 1) {
                        progressCnt = progressMax;
                        curPar.zoomCur = curPar.zoomTarget;
                        curPar.slideCur = curPar.slideTarget;
                        smoothShift = false;
                    }
                    else {
                        progressCnt += addDelta;
                        curPar.zoomCur = zoomInit + (curPar.zoomTarget - zoomInit) * progressCnt / progressMax;
                        const double t = (double)progressCnt / progressMax;
                        curPar.slideCur.x = (int)std::round(slideInit.x + (curPar.slideTarget.x - slideInit.x) * t);
                        curPar.slideCur.y = (int)std::round(slideInit.y + (curPar.slideTarget.y - slideInit.y) * t);
                    }
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

        if (curPar.imageAssetPtr->format == ImageFormat::Animated && curPar.isAnimationPause) {
            wstring str = std::format(L"{} [{}/{}] {}% {}  ",
                getUIStringW(9),
                curPar.curFrameIdx + 1, curPar.curFrameIdxMax + 1,
                curPar.zoomCur * 100ULL / curPar.ZOOM_BASE,
                imgFileList[curFileIdx]);
            if (curPar.rotation)
                str += (curPar.rotation == 1 ? getUIStringW(10) : (curPar.rotation == 3 ? getUIStringW(11) : getUIStringW(12)));
            SetWindowTextW(m_hWnd, str.c_str());
        }
        else {
            wstring str = std::format(L" [{}/{}] {}% {}  ",
                curFileIdx + 1, imgFileList.size(),
                curPar.zoomCur * 100ULL / curPar.ZOOM_BASE,
                imgFileList[curFileIdx]);
            if (curPar.rotation)
                str += (curPar.rotation == 1 ? getUIStringW(10) : (curPar.rotation == 3 ? getUIStringW(11) : getUIStringW(12)));
            SetWindowTextW(m_hWnd, str.c_str());
        }

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

    YeImageViewerApp app;
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
