#include "LRU.h"
#include "PrintAdjustments.h"
#include "PrintLayout.h"
#include "SVGPreprocessor.h"
#include "YeThumbnailProviderGuids.h"
#include "FileAssociationNaming.h"
#include "MotionPhotoUtils.h"
#include "MonitorPlacement.h"
#include "BackgroundRenderer.h"
#include "BackgroundPolicy.h"
#include "EscapeBehavior.h"
#include "ConfigFile.h"
#include "ExternalEditorConfig.h"
#include "FramePacingPolicy.h"
#include "HomeScreenLayout.h"
#include "ImageInterpolation.h"
#include "InitialWindowLayout.h"
#include "ImageInfoPresentation.h"
#include "WindowTitlePresentation.h"
#include "PresentationLayout.h"
#include "OverlayLayout.h"
#include "LoadingBadge.h"
#include "LivePhotoBadge.h"
#include "FullscreenInfoBar.h"
#include "FileTargetConfig.h"
#include "MotionTiming.h"
#include "AudioClip.h"
#include "JpegQuality.h"
#include "ColorSpaceName.h"
#include "ImageHistogram.h"
#include "DecodeEstimate.h"
#include "ImageViewTransform.h"
#include "RotationStore.h"
#include "RenamePolicy.h"
#include "SettingCommand.h"
#include "SettingLayout.h"
#include "UiLanguage.h"
#include "ShortcutConfig.h"
#include "ViewerOptions.h"
#include "TextRenderingPolicy.h"
#include "ToolbarCommand.h"
#include "WheelInput.h"
#include "SlideshowPolicy.h"
#include "ZoomPolicy.h"
#include "ZoomEditPolicy.h"
#include "StbImageDecoder.h"
#include "SvgRenderer.h"
#include "SystemFont.h"

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thumbcache.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failedTests = 0;
int passedTests = 0;

std::vector<uint8_t> readFile(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }

    const auto fileSize = file.tellg();
    if (fileSize <= 0) {
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        return {};
    }
    return buffer;
}

void passOrFail(std::string_view name, bool passed) {
    if (passed) {
        ++passedTests;
        std::cout << "PASS " << name << '\n';
    }
    else {
        ++failedTests;
        std::cerr << "FAIL " << name << '\n';
    }
}

void expectVideoSize(std::string_view name, std::string_view metadata, size_t expected) {
    const size_t actual = MotionPhotoUtils::getVideoSize(metadata);
    if (actual == expected) {
        ++passedTests;
        std::cout << "PASS " << name << '\n';
        return;
    }

    ++failedTests;
    std::cerr << "FAIL " << name << ": expected " << expected << ", got " << actual << '\n';
}

void expectHdrChannelOrder() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    std::vector<uint8_t> hdr(header.begin(), header.end());
    hdr.insert(hdr.end(), {
        255, 0, 0, 128, // Red in Radiance RGBE order.
        0, 0, 255, 128  // Blue in Radiance RGBE order.
    });

    const auto image = StbImageDecoder::decode(hdr);
    const std::vector<uint8_t> expected{
        0, 0, 255, 255, // Red in the viewer's BGRA order.
        255, 0, 0, 255  // Blue in the viewer's BGRA order.
    };

    if (image.width == 2 && image.height == 1 && image.bgra == expected) {
        ++passedTests;
        std::cout << "PASS Radiance HDR RGB to BGRA channel order\n";
        return;
    }

    ++failedTests;
    std::cerr << "FAIL Radiance HDR RGB to BGRA channel order\n";
}

void expectRealHdrChannelOrder(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        ++failedTests;
        std::cerr << "FAIL real HDR fixture could not be opened\n";
        return;
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        ++failedTests;
        std::cerr << "FAIL real HDR fixture is empty\n";
        return;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        ++failedTests;
        std::cerr << "FAIL real HDR fixture could not be read\n";
        return;
    }

    const auto image = StbImageDecoder::decode(buffer);
    if (image.width != 2560 || image.height != 1600 || image.bgra.size() != 2560ULL * 1600ULL * 4ULL) {
        ++failedTests;
        std::cerr << "FAIL real HDR fixture dimensions or decoded buffer size\n";
        return;
    }

    uint64_t blueSum = 0;
    uint64_t redSum = 0;
    for (size_t i = 0; i < image.bgra.size(); i += 4) {
        blueSum += image.bgra[i];
        redSum += image.bgra[i + 2];
    }

    const uint64_t pixelCount = static_cast<uint64_t>(image.width) * image.height;
    if (redSum > blueSum + pixelCount * 10) {
        ++passedTests;
        std::cout << "PASS real HDR fixture preserves red and blue channels\n";
        return;
    }

    ++failedTests;
    std::cerr << "FAIL real HDR fixture red and blue channels are swapped\n";
}

void expectSvgRerendersAtDisplayResolution(std::string_view path) {
    const auto source = readFile(path);
    const auto renderer = SvgRenderer::create(source);
    if (!renderer) {
        passOrFail("SVG fixture loads for viewport rendering", false);
        return;
    }

    const int nativeWidth = (int)std::lround(renderer->width());
    const int nativeHeight = (int)std::lround(renderer->height());
    const int scale = 4;
    const auto native = renderer->renderToBitmap(nativeWidth, nativeHeight);
    const auto enlarged = renderer->renderToBitmap(nativeWidth * scale, nativeHeight * scale);
    const auto viewport = renderer->renderViewport(
        nativeWidth * scale, nativeHeight * scale,
        { (float)scale, 0.0f, 0.0f, (float)scale, 0.0f, 0.0f });

    bool valid = nativeWidth == 280 && nativeHeight == 288 &&
        !native.empty() && !enlarged.empty() && !viewport.empty() &&
        enlarged.bgra.size() == viewport.bgra.size();
    if (!valid) {
        passOrFail("SVG fixture renders at the requested viewport resolution", false);
        return;
    }

    size_t viewportDifference = 0;
    size_t nearestDifference = 0;
    for (int y = 0; y < enlarged.height; ++y) {
        for (int x = 0; x < enlarged.width; ++x) {
            const size_t highOffset = (static_cast<size_t>(y) * enlarged.width + x) * 4;
            const size_t lowOffset = (static_cast<size_t>(y / scale) * native.width + x / scale) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                viewportDifference += enlarged.bgra[highOffset + channel] != viewport.bgra[highOffset + channel];
                nearestDifference += enlarged.bgra[highOffset + channel] != native.bgra[lowOffset + channel];
            }
        }
    }

    passOrFail("SVG viewport is rerendered instead of enlarging cached pixels",
        viewportDifference < enlarged.bgra.size() / 100 &&
        nearestDifference > enlarged.bgra.size() / 100);
}

void expectBilinearEnlargement() {
    const uint8_t blackAndWhite[]{ 0, 0, 0, 255, 255, 255 };
    const uint32_t gray = ImageInterpolation::sampleBilinearBgra(
        blackAndWhite, 2, 1, 6, 3, 0.5f, 0.0f);
    passOrFail("enlarged raster edges use bilinear gray transitions",
        (gray & 0x00FFFFFFu) == 0x00808080u && (gray >> 24) == 255);

    const uint8_t transparentBlueAndOpaqueRed[]{
        255, 0, 0, 0,
        0, 0, 255, 255
    };
    const uint32_t alphaEdge = ImageInterpolation::sampleBilinearBgra(
        transparentBlueAndOpaqueRed, 2, 1, 8, 4, 0.5f, 0.0f);
    passOrFail("transparent bilinear edges avoid dark or colored fringes",
        (alphaEdge & 0xFFu) == 0 &&
        ((alphaEdge >> 8) & 0xFFu) == 0 &&
        ((alphaEdge >> 16) & 0xFFu) == 255 &&
        ((alphaEdge >> 24) & 0xFFu) == 128);
}

void expectBackgroundRendering() {
    constexpr uint32_t theme = 0xFF202020u;

    passOrFail("invalid background setting falls back to transparency grid",
        BackgroundRenderer::normalizeMode(99) == BackgroundMode::Transparent);
    passOrFail("transparent background alternates checkerboard cells",
        BackgroundRenderer::canvasPixel(BackgroundMode::Transparent, false, 0, 0,
            theme) == BackgroundRenderer::GRID_LIGHT &&
        BackgroundRenderer::canvasPixel(BackgroundMode::Transparent, false, 12, 0,
            theme) == BackgroundRenderer::GRID_DARK &&
        BackgroundRenderer::GRID_WIDTH == 12 &&
        BackgroundRenderer::GRID_DARK == 0xFFEBEBEBu &&
        BackgroundRenderer::GRID_LIGHT == 0xFFF5F5F5u);
    passOrFail("white and black backgrounds use exact opaque colors",
        BackgroundRenderer::canvasPixel(BackgroundMode::White, false, 0, 0,
            theme) == 0xFFFFFFFFu &&
        BackgroundRenderer::canvasPixel(BackgroundMode::Black, false, 0, 0,
            theme) == 0xFF000000u);
    passOrFail("frosted glass exposes the DWM backdrop when active",
        BackgroundRenderer::canvasPixel(BackgroundMode::FrostedGlass, true, 0, 0,
            theme) == 0x00000000u &&
        BackgroundRenderer::canvasPixel(BackgroundMode::FrostedGlass, false, 0, 0,
            theme) == theme);

    constexpr uint32_t halfTransparentColor = 0x80804020u;
    passOrFail("frosted-glass image pixels are premultiplied for DWM",
        BackgroundRenderer::compositeBgra(halfTransparentColor,
            BackgroundMode::FrostedGlass, true, 0, 0,
            theme) == 0x80402010u);
    passOrFail("semi-transparent pixels blend correctly over white and black",
        BackgroundRenderer::compositeBgra(halfTransparentColor,
            BackgroundMode::White, false, 0, 0,
            theme) == 0xFFBF9F8Fu &&
        BackgroundRenderer::compositeBgra(halfTransparentColor,
            BackgroundMode::Black, false, 0, 0,
            theme) == 0xFF402010u);
    passOrFail("presentation canvas uses the layered alpha surface",
        BackgroundPolicy::usesPerPixelAlphaSurface());
    passOrFail("configured background only changes the image area during presentation",
        BackgroundPolicy::imageAreaMode(BackgroundMode::White) == BackgroundMode::White &&
        BackgroundPolicy::imageAreaMode(BackgroundMode::Black) == BackgroundMode::Black);
    passOrFail("immersive canvas stays sixty percent black regardless of the configured background",
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::White, true, true, false, 0, 0,
            theme) == BackgroundPolicy::PRESENTATION_TINT &&
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::Transparent, true, true, false, 0, 0,
            theme) == BackgroundPolicy::PRESENTATION_TINT &&
        BackgroundPolicy::PRESENTATION_TINT == 0x99000000u);
    passOrFail("immersive canvas falls back to the opaque theme without alpha composition",
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::White, true, false, false, 0, 0,
            theme) == theme);
    passOrFail("framed canvas follows the configured background instead of a fixed gray",
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::White, false, true, false, 0, 0,
            theme) == 0xFFFFFFFFu &&
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::Black, false, true, false, 0, 0,
            theme) == 0xFF000000u);
    passOrFail("frosted glass clears the canvas only once the DWM acrylic backdrop is live",
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::FrostedGlass, false, true, true, 0, 0,
            theme) == 0x00000000u &&
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::FrostedGlass, false, true, false, 0, 0,
            theme) == theme);
    passOrFail("only an explicit frosted-glass choice outside presentation asks DWM for acrylic",
        BackgroundPolicy::requestsFrostedGlass(false, BackgroundMode::FrostedGlass) &&
        !BackgroundPolicy::requestsFrostedGlass(true, BackgroundMode::FrostedGlass) &&
        !BackgroundPolicy::requestsFrostedGlass(false, BackgroundMode::White) &&
        !BackgroundPolicy::requestsFrostedGlass(false, BackgroundMode::Transparent));
    passOrFail("framed transparent background tiles the checkerboard across the whole window",
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::Transparent, false, true, false, 0, 0,
            theme) == BackgroundRenderer::GRID_LIGHT &&
        BackgroundPolicy::windowCanvasPixel(BackgroundMode::Transparent, false, true, false,
            BackgroundRenderer::GRID_WIDTH, 0, theme) == BackgroundRenderer::GRID_DARK);
    passOrFail("only the framed checkerboard needs per-pixel window canvas evaluation",
        BackgroundPolicy::usesUniformWindowCanvas(BackgroundMode::White, false) &&
        BackgroundPolicy::usesUniformWindowCanvas(BackgroundMode::Transparent, true) &&
        !BackgroundPolicy::usesUniformWindowCanvas(BackgroundMode::Transparent, false));
}

void expectOverlayLayout() {
    constexpr int width = 800;
    constexpr int height = 600;
    constexpr auto toolbarPrevious = OverlayLayout::toolbarPreviousRect(width, height);
    constexpr auto toolbarPlayPause = OverlayLayout::toolbarPlayPauseRect(width, height);
    constexpr auto toolbarNext = OverlayLayout::toolbarNextRect(width, height);
    constexpr auto toolbar = OverlayLayout::toolbarRect(width, height);
    constexpr auto close = OverlayLayout::presentationCloseRect(width, height);
    constexpr auto zoomIndicator = OverlayLayout::zoomIndicatorRect(width, height);

    const auto hitCenter = [&](OverlayLayout::Rect rect) {
        return OverlayLayout::hitTest(width, height,
            rect.x + rect.width / 2, rect.y + rect.height / 2);
    };
    passOrFail("viewer toolbar is centered with the reference proportions",
        toolbar.width == OverlayLayout::BASE_TOOLBAR_WIDTH &&
        toolbar.height == OverlayLayout::BASE_TOOLBAR_HEIGHT &&
        toolbar.x == (width - toolbar.width) / 2 &&
        toolbar.y + toolbar.height == height - OverlayLayout::BASE_TOOLBAR_BOTTOM_MARGIN &&
        toolbarPrevious.width == OverlayLayout::BASE_BUTTON_SIZE &&
        toolbarNext.width == OverlayLayout::BASE_BUTTON_SIZE);
    // 播放键在工具栏里大致居中即可，不再要求严格对齐窗口中心：右侧比左侧多一个
    // 按钮，想严格居中就得在左边留一块空白，那比偏十几像素难看得多。
    constexpr int playCenterOffset = 265 + OverlayLayout::BASE_BUTTON_SIZE / 2 +
        OverlayLayout::BASE_TOOLBAR_PADDING;
    passOrFail("previous, slideshow, and next form the centered primary toolbar group",
        std::abs(playCenterOffset - OverlayLayout::BASE_TOOLBAR_WIDTH / 2) <= 24 &&
        toolbarPlayPause.x + toolbarPlayPause.width / 2 > toolbar.x + toolbar.width / 3 &&
        toolbarPlayPause.x + toolbarPlayPause.width / 2 < toolbar.x + toolbar.width * 2 / 3 &&
        toolbarPrevious.x < toolbarPlayPause.x && toolbarPlayPause.x < toolbarNext.x);
    // 新按钮插在「适应窗口」和「实际大小」中间，三者依次排开且不重叠
    constexpr auto fitWindowButton = OverlayLayout::zoomFitRect(width, height);
    constexpr auto fitImageButton = OverlayLayout::fitImageRect(width, height);
    constexpr auto actualSizeButton = OverlayLayout::zoomActualRect(width, height);
    passOrFail("the fit-window, fit-image, and actual-size buttons sit side by side",
        fitWindowButton.x + fitWindowButton.width <= fitImageButton.x &&
        fitImageButton.x + fitImageButton.width <= actualSizeButton.x);

    // 进程是 PerMonitorHighDPIAware，Windows 不做拉伸，所以浮动控件必须自己按 DPI
    // 放大才能保持同样的观感尺寸。此前 scale 只看窗口宽度且封顶 100%。
    constexpr int highDpi = 192;
    constexpr auto toolbarHighDpi = OverlayLayout::toolbarRect(width * 2, height * 2, highDpi);
    constexpr auto closeHighDpi = OverlayLayout::presentationCloseRect(width * 2, height * 2, highDpi);
    constexpr auto zoomIndicatorHighDpi = OverlayLayout::zoomIndicatorRect(width * 2, height * 2, highDpi);
    constexpr auto buttonHighDpi = OverlayLayout::toolbarPreviousRect(width * 2, height * 2, highDpi);
    passOrFail("toolbar, close button, and zoom indicator double their geometry at 192 DPI",
        toolbarHighDpi.width == OverlayLayout::BASE_TOOLBAR_WIDTH * 2 &&
        toolbarHighDpi.height == OverlayLayout::BASE_TOOLBAR_HEIGHT * 2 &&
        buttonHighDpi.width == OverlayLayout::BASE_BUTTON_SIZE * 2 &&
        closeHighDpi.width == OverlayLayout::PRESENTATION_CLOSE_SIZE * 2 &&
        zoomIndicatorHighDpi.height == OverlayLayout::ZOOM_INDICATOR_HEIGHT * 2);
    passOrFail("a narrow high-DPI window still shrinks the toolbar to fit",
        OverlayLayout::toolbarRect(600, 400, highDpi).width <= 600 &&
        OverlayLayout::toolbarRect(600, 400, highDpi).width <
            OverlayLayout::BASE_TOOLBAR_WIDTH * 2);
    passOrFail("the default DPI keeps the existing ninety-six DPI layout untouched",
        OverlayLayout::toolbarRect(width, height, OverlayLayout::BASE_DPI).width == toolbar.width &&
        OverlayLayout::toolbarRect(width, height).width == OverlayLayout::BASE_TOOLBAR_WIDTH &&
        OverlayLayout::presentationCloseRect(width, height).width ==
            OverlayLayout::PRESENTATION_CLOSE_SIZE);
    passOrFail("high-DPI hit testing follows the scaled toolbar geometry",
        OverlayLayout::hitTest(width * 2, height * 2,
            buttonHighDpi.x + buttonHighDpi.width / 2,
            buttonHighDpi.y + buttonHighDpi.height / 2,
            highDpi) == OverlayLayout::Hit::ToolbarPreviousImage);

    // 两侧翻页按钮默认关闭：关着的时候那片区域必须照旧落回原来的命中结果。
    constexpr auto edgePrevious = OverlayLayout::edgePreviousRect(width, height);
    constexpr auto edgeNext = OverlayLayout::edgeNextRect(width, height);
    const auto edgeHit = [&](OverlayLayout::Rect rect, bool enabled) {
        return OverlayLayout::hitTest(width, height,
            rect.x + rect.width / 2, rect.y + rect.height / 2,
            OverlayLayout::BASE_DPI, enabled);
    };
    passOrFail("edge paging buttons are upright rectangles centred on both sides",
        edgePrevious.width == OverlayLayout::BASE_EDGE_ARROW_WIDTH &&
        edgePrevious.height == OverlayLayout::BASE_EDGE_ARROW_HEIGHT &&
        edgePrevious.height > edgePrevious.width &&
        edgePrevious.x == OverlayLayout::BASE_EDGE_ARROW_MARGIN &&
        edgePrevious.y + edgePrevious.height / 2 == height / 2 &&
        edgeNext.x + edgeNext.width == width - OverlayLayout::BASE_EDGE_ARROW_MARGIN &&
        OverlayLayout::edgePreviousRect(width * 2, height * 2, highDpi).width ==
            OverlayLayout::BASE_EDGE_ARROW_WIDTH * 2);

    // 热区比按钮本身大，而且贴着窗口边缘，鼠标扫到边上就能命中。
    constexpr auto edgePreviousHit = OverlayLayout::edgePreviousHitRect(width, height);
    constexpr auto edgeNextHit = OverlayLayout::edgeNextHitRect(width, height);
    passOrFail("edge paging hit areas reach the window edge and exceed the drawn button",
        edgePreviousHit.x == 0 &&
        edgeNextHit.x + edgeNextHit.width == width &&
        edgePreviousHit.width > edgePrevious.width &&
        edgePreviousHit.height > edgePrevious.height &&
        edgePreviousHit.y + edgePreviousHit.height / 2 == height / 2);
    passOrFail("edge paging responds only while the option is enabled",
        edgeHit(edgePrevious, true) == OverlayLayout::Hit::EdgePreviousImage &&
        edgeHit(edgeNext, true) == OverlayLayout::Hit::EdgeNextImage &&
        edgeHit(edgePrevious, false) != OverlayLayout::Hit::EdgePreviousImage &&
        edgeHit(edgeNext, false) != OverlayLayout::Hit::EdgeNextImage);
    passOrFail("viewer toolbar uses a flat rounded surface without square-corner borders",
        OverlayLayout::TOOLBAR_BORDER == 0x00000000u);
    passOrFail("toolbar keeps one clean visual weight for icons and zoom percentage",
        OverlayLayout::BASE_ICON_SIZE == 20 &&
        OverlayLayout::TOOLBAR_TEXT_SIZE == 16 &&
        OverlayLayout::TOOLBAR_TEXT_BOLD_OFFSET == 0 &&
        OverlayLayout::ICON_STROKE_EXPANSION == 0);
    passOrFail("compact plus and minus retain the same 20 pixel visual size as other icons",
        OverlayLayout::toolbarIconSize(width,
            OverlayLayout::settingsRect(width, height)) == OverlayLayout::BASE_ICON_SIZE &&
        OverlayLayout::toolbarIconSize(width,
            OverlayLayout::zoomOutRect(width, height), true) == OverlayLayout::BASE_ICON_SIZE &&
        OverlayLayout::toolbarIconSize(width,
            OverlayLayout::zoomInRect(width, height), true) == OverlayLayout::BASE_ICON_SIZE);
    passOrFail("toolbar hit testing maps every reference action",
        hitCenter(OverlayLayout::toolbarPreviousRect(width, height)) == OverlayLayout::Hit::ToolbarPreviousImage &&
        hitCenter(OverlayLayout::toolbarPlayPauseRect(width, height)) == OverlayLayout::Hit::ToolbarPlayPause &&
        hitCenter(OverlayLayout::toolbarNextRect(width, height)) == OverlayLayout::Hit::ToolbarNextImage &&
        hitCenter(OverlayLayout::rotateLeftRect(width, height)) == OverlayLayout::Hit::RotateLeft &&
        hitCenter(OverlayLayout::rotateRightRect(width, height)) == OverlayLayout::Hit::RotateRight &&
        hitCenter(OverlayLayout::flipHorizontalRect(width, height)) == OverlayLayout::Hit::FlipHorizontal &&
        hitCenter(OverlayLayout::flipVerticalRect(width, height)) == OverlayLayout::Hit::FlipVertical &&
        hitCenter(OverlayLayout::zoomFitRect(width, height)) == OverlayLayout::Hit::ZoomFit &&
        hitCenter(OverlayLayout::fitImageRect(width, height)) == OverlayLayout::Hit::FitImage &&
        hitCenter(OverlayLayout::zoomActualRect(width, height)) == OverlayLayout::Hit::ZoomActual &&
        hitCenter(OverlayLayout::fullscreenRect(width, height)) == OverlayLayout::Hit::Fullscreen &&
        hitCenter(OverlayLayout::settingsRect(width, height)) == OverlayLayout::Hit::Settings &&
        hitCenter(OverlayLayout::zoomOutRect(width, height)) == OverlayLayout::Hit::ZoomOut &&
        hitCenter(OverlayLayout::zoomTextRect(width, height)) == OverlayLayout::Hit::ZoomText &&
        hitCenter(OverlayLayout::zoomInRect(width, height)) == OverlayLayout::Hit::ZoomIn);
    passOrFail("every visible toolbar button routes to its production command",
        ToolbarCommand::resolve(OverlayLayout::Hit::ToolbarPreviousImage) == ToolbarCommand::Command::PreviousImage &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ToolbarPlayPause) == ToolbarCommand::Command::PlayPause &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ToolbarNextImage) == ToolbarCommand::Command::NextImage &&
        ToolbarCommand::resolve(OverlayLayout::Hit::RotateLeft) == ToolbarCommand::Command::RotateLeft &&
        ToolbarCommand::resolve(OverlayLayout::Hit::RotateRight) == ToolbarCommand::Command::RotateRight &&
        ToolbarCommand::resolve(OverlayLayout::Hit::FlipHorizontal) == ToolbarCommand::Command::FlipHorizontal &&
        ToolbarCommand::resolve(OverlayLayout::Hit::FlipVertical) == ToolbarCommand::Command::FlipVertical &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ZoomFit) == ToolbarCommand::Command::ZoomFit &&
        ToolbarCommand::resolve(OverlayLayout::Hit::FitImage) == ToolbarCommand::Command::FitImage &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ZoomActual) == ToolbarCommand::Command::ZoomActual &&
        ToolbarCommand::resolve(OverlayLayout::Hit::Fullscreen) == ToolbarCommand::Command::Fullscreen &&
        ToolbarCommand::resolve(OverlayLayout::Hit::Settings) == ToolbarCommand::Command::Settings &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ZoomOut) == ToolbarCommand::Command::ZoomOut &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ZoomText) == ToolbarCommand::Command::EditZoom &&
        ToolbarCommand::resolve(OverlayLayout::Hit::ZoomIn) == ToolbarCommand::Command::ZoomIn);
    passOrFail("editable zoom percentage has a practical click and text area",
        OverlayLayout::zoomTextRect(width, height).width == 50 &&
        OverlayLayout::zoomTextRect(width, height).x >
            OverlayLayout::zoomOutRect(width, height).x +
            OverlayLayout::zoomOutRect(width, height).width &&
        OverlayLayout::zoomTextRect(width, height).x +
            OverlayLayout::zoomTextRect(width, height).width <
            OverlayLayout::zoomInRect(width, height).x);
    passOrFail("redundant favorite, copy, and delete actions stay out of the primary toolbar",
        !OverlayLayout::showsRedundantFileActions());
    const auto toolbarReveal = OverlayLayout::toolbarRevealRect(width, height);
    passOrFail("toolbar reveal region includes the padded lower strip",
        toolbarReveal.x == toolbar.x - OverlayLayout::BASE_TOOLBAR_REVEAL_SIDE_PADDING &&
        toolbarReveal.width == toolbar.width + OverlayLayout::BASE_TOOLBAR_REVEAL_SIDE_PADDING * 2 &&
        toolbarReveal.y == toolbar.y - OverlayLayout::BASE_TOOLBAR_REVEAL_TOP_PADDING &&
        toolbarReveal.y + toolbarReveal.height == height &&
        OverlayLayout::hitTest(width, height, toolbarReveal.x + 2,
            toolbarReveal.y + 2) == OverlayLayout::Hit::Toolbar &&
        OverlayLayout::hitTest(width, height, width / 2, height - 2) == OverlayLayout::Hit::Toolbar &&
        OverlayLayout::hitTest(width, height, 5, 590) == OverlayLayout::Hit::None &&
        OverlayLayout::hitTest(width, height, 40, 250) == OverlayLayout::Hit::None &&
        OverlayLayout::hitTest(width, height, 20, height / 2) == OverlayLayout::Hit::None &&
        OverlayLayout::hitTest(width, height, width - 20, height / 2) == OverlayLayout::Hit::None);
    constexpr auto narrowToolbar = OverlayLayout::toolbarRect(300, 600);
    passOrFail("toolbar scales down without leaving narrow windows",
        narrowToolbar.width <= 284 && narrowToolbar.x >= 0 &&
        narrowToolbar.x + narrowToolbar.width <= 300);
    passOrFail("presentation close button stays in the upper-right corner",
        close.x == 746 && close.y == 12 && close.width == 42 && close.height == 42 &&
        OverlayLayout::hitTest(width, height, 767, 33) == OverlayLayout::Hit::PresentationClose);
    passOrFail("presentation close button does not depend on the optional SVG resource",
        OverlayLayout::shouldDrawPresentationClose(true, false, true) &&
        OverlayLayout::shouldDrawPresentationClose(true, false, false) &&
        OverlayLayout::shouldDrawPresentationClose(false, false, false) &&
        !OverlayLayout::shouldDrawPresentationClose(false, true, true));
    passOrFail("top image information bar is removed from framed and presentation modes",
        !OverlayLayout::usesTopInfoBar());
    passOrFail("zoom percentage indicator stays in the lower-left safe margin",
        zoomIndicator.x == OverlayLayout::ZOOM_INDICATOR_MARGIN &&
        zoomIndicator.y + zoomIndicator.height == height - OverlayLayout::ZOOM_INDICATOR_MARGIN &&
        zoomIndicator.width == OverlayLayout::ZOOM_INDICATOR_WIDTH &&
        zoomIndicator.height == OverlayLayout::ZOOM_INDICATOR_HEIGHT);
}

// 四个缩放/窗口命令的规则。它们以前散在 main.cpp 里，既测不到又容易改漏一处——
// 「适应图片」就曾经只改了窗口没改缩放，窗口放大了画面没跟上，四周露出一圈背景。
void expectZoomCommands() {
    constexpr int64_t base = 10000;   // 与 CurImageParameter::ZOOM_BASE 一致

    // ── 适应窗口：大图缩进窗口 ─────────────────────────────────────────
    // 9000x9000 的图放进 800x600 的窗口，短边说了算：600/9000
    passOrFail("fit-to-window shrinks a large image until the short side fits",
        ZoomPolicy::fitWindowZoom(9000, 9000, 800, 600, base) == 600 * base / 9000 &&
        ZoomPolicy::fitWindowZoom(4000, 1000, 800, 600, base) == 800 * base / 4000);

    // ── 适应窗口：小图不放大 ───────────────────────────────────────────
    // 这是这次改的规矩：把 100x100 撑满 2000 像素宽的窗口只会糊成一片
    passOrFail("fit-to-window never upscales an image smaller than the window",
        ZoomPolicy::fitWindowZoom(100, 100, 2000, 1500, base) == base &&
        ZoomPolicy::fitWindowZoom(799, 599, 800, 600, base) == base &&
        // 正好一样大也是 100%，不多不少
        ZoomPolicy::fitWindowZoom(800, 600, 800, 600, base) == base);

    // 一边比窗口大、另一边比窗口小时仍然要缩，只是以大的那边为准
    passOrFail("fit-to-window still shrinks when only one side overflows",
        ZoomPolicy::fitWindowZoom(4000, 100, 800, 600, base) == 800 * base / 4000);

    // ── 适应窗口：旋转后按旋转的样子算 ─────────────────────────────────
    // 4000x1000 竖过来是 1000x4000，该按高度算
    passOrFail("fit-to-window measures the rotated shape, not the stored one",
        ZoomPolicy::fitWindowZoomRotated(4000, 1000, 0, 800, 600, base) ==
            ZoomPolicy::fitWindowZoom(4000, 1000, 800, 600, base) &&
        ZoomPolicy::fitWindowZoomRotated(4000, 1000, 1, 800, 600, base) ==
            ZoomPolicy::fitWindowZoom(1000, 4000, 800, 600, base) &&
        ZoomPolicy::fitWindowZoomRotated(4000, 1000, 2, 800, 600, base) ==
            ZoomPolicy::fitWindowZoom(4000, 1000, 800, 600, base) &&
        ZoomPolicy::fitWindowZoomRotated(4000, 1000, 3, 800, 600, base) ==
            ZoomPolicy::fitWindowZoom(1000, 4000, 800, 600, base));

    // ── 适应窗口：烂输入不能把程序带崩 ─────────────────────────────────
    // 解码失败、窗口还没就绪时这些值真的会是 0
    passOrFail("fit-to-window falls back to 100% instead of dividing by zero",
        ZoomPolicy::fitWindowZoom(0, 100, 800, 600, base) == base &&
        ZoomPolicy::fitWindowZoom(100, 0, 800, 600, base) == base &&
        ZoomPolicy::fitWindowZoom(100, 100, 0, 600, base) == base &&
        ZoomPolicy::fitWindowZoom(100, 100, 800, 0, base) == base &&
        ZoomPolicy::fitWindowZoom(-1, -1, -1, -1, base) == base);

    // 极端比例也不能算出 0 —— 0 会让画面彻底消失
    passOrFail("fit-to-window never returns a zoom of zero",
        ZoomPolicy::fitWindowZoom(4000000, 10, 800, 600, base) >= 1);

    // ── 适应图片：客户区正好等于图片，四边零留白 ───────────────────────
    // 这是「适应图片」零留白的根据：窗口按 scale 定，缩放也必须是同一个 scale
    {
        const auto small = InitialWindowLayout::calculateFitImage(640, 480, 2560, 1392);
        const bool exactlyImage = small.clientWidth == 640 && small.clientHeight == 480 &&
            std::abs(small.scale - 1.0) < 1e-9;

        const auto huge = InitialWindowLayout::calculateFitImage(9000, 9000, 2560, 1392);
        // 客户区必须等于「图片 × scale」，否则按 scale 设缩放就会和窗口对不上
        const bool matchesScale =
            huge.clientWidth == static_cast<int>(std::lround(9000 * huge.scale)) &&
            huge.clientHeight == static_cast<int>(std::lround(9000 * huge.scale));
        // 不能超出工作区的 90%
        const bool withinWorkArea = huge.clientWidth <= 2560 * 90 / 100 + 1 &&
            huge.clientHeight <= 1392 * 90 / 100 + 1;
        // 正方形图片得到正方形窗口，不是 4:3
        const bool keepsAspect = huge.clientWidth == huge.clientHeight;

        passOrFail("fit-window-to-image sizes the client area to the image itself",
            exactlyImage && matchesScale && withinWorkArea && keepsAspect && huge.scale < 1.0);
    }

    // 极扁的图也要保持比例，别被工作区的宽高比带偏
    {
        const auto wide = InitialWindowLayout::calculateFitImage(8000, 500, 2560, 1392);
        const double sourceAspect = 8000.0 / 500.0;
        const double windowAspect = static_cast<double>(wide.clientWidth) / wide.clientHeight;
        passOrFail("fit-window-to-image keeps the aspect ratio of a very wide image",
            std::abs(sourceAspect - windowAspect) / sourceAspect < 0.02);
    }

    // ── 预览：缩略图要画在真图将要占据的那个矩形里 ─────────────────────
    // 系统给 SVG 的缩略图是 995x1024，而 SVG 自己是 280x288，尺寸毫无关系。
    // 不折算的话，换成真图的那一刻画面会突然缩小三倍。
    {
        const int sourceWidth = 9000;
        const int previewWidth = 1024;
        const int64_t zoomForSource = ZoomPolicy::fitWindowZoom(9000, 9000, 750, 750, base);
        const int64_t zoomForPreview =
            ZoomPolicy::previewZoom(zoomForSource, sourceWidth, previewWidth);

        // 两者画出来的宽度必须一致（允许一个像素的取整误差）
        const double sourceOnScreen = sourceWidth * static_cast<double>(zoomForSource) / base;
        const double previewOnScreen = previewWidth * static_cast<double>(zoomForPreview) / base;
        passOrFail("a preview thumbnail is drawn in the rectangle the real image will occupy",
            std::abs(sourceOnScreen - previewOnScreen) <= 1.0);
    }

    // 缩略图比真图大（SVG 那种）同样要成立
    {
        const int64_t zoomForSource = ZoomPolicy::fitWindowZoom(280, 288, 750, 750, base);
        const int64_t zoomForPreview = ZoomPolicy::previewZoom(zoomForSource, 280, 995);
        const double sourceOnScreen = 280 * static_cast<double>(zoomForSource) / base;
        const double previewOnScreen = 995 * static_cast<double>(zoomForPreview) / base;
        passOrFail("the same holds when the thumbnail is larger than the real image",
            std::abs(sourceOnScreen - previewOnScreen) <= 1.0);
    }

    passOrFail("preview folding leaves a normal image untouched",
        ZoomPolicy::previewZoom(1234, 0, 100) == 1234 &&
        ZoomPolicy::previewZoom(1234, 100, 0) == 1234 &&
        ZoomPolicy::previewZoom(1234, 500, 500) == 1234);

    // ── 报给用户的百分比：预览期间要按真图口径 ─────────────────────────
    // 否则标题上会先显示 73% 再跳到 8%，而画面其实一动没动
    {
        const int64_t zoomForSource = ZoomPolicy::fitWindowZoom(9000, 9000, 750, 750, base);
        const double previewToSource = 9000.0 / 1024.0;
        const int64_t zoomForPreview = ZoomPolicy::previewZoom(zoomForSource, 9000, 1024);
        passOrFail("the reported percentage stays the same across the preview-to-real swap",
            ZoomPolicy::reportedPercent(zoomForPreview, previewToSource, base) ==
            ZoomPolicy::reportedPercent(zoomForSource, 1.0, base));
    }

    passOrFail("the reported percentage is unchanged when there is no preview",
        ZoomPolicy::reportedPercent(base, 1.0, base) == 100 &&
        ZoomPolicy::reportedPercent(base / 2, 1.0, base) == 50 &&
        // 折算系数非法时按原值报，不能报出 0 或负数
        ZoomPolicy::reportedPercent(base, 0.0, base) == 100 &&
        ZoomPolicy::reportedPercent(base, -1.0, base) == 100);

    // ── 实际大小：永远是 100%，与窗口无关 ──────────────────────────────
    // 这条没有专门的函数，用显示百分比把契约钉住：ZOOM_BASE 就是 100%
    passOrFail("actual size means exactly one hundred percent",
        ZoomPolicy::displayPercent(base, base) == 100);
}

void expectZoomPolicy() {
    constexpr int64_t zoomBase = 1 << 16;
    const auto levels = ZoomPolicy::buildLevels(zoomBase);
    const auto actual = std::find(levels.begin(), levels.end(), zoomBase);
    bool downMatches = actual != levels.end();
    bool upMatches = actual != levels.end();
    constexpr std::array expectedDown{
        87, 76, 66, 57, 50, 43, 38, 33, 28, 25, 21, 19, 16,
        14, 12, 11, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
    constexpr std::array expectedUp{
        115, 132, 152, 175, 201, 231, 266, 306, 352, 405, 465,
        535, 615, 708, 813, 936, 1076, 1238, 1423, 1637, 1882, 2162 };
    if (actual != levels.end()) {
        const auto index = static_cast<std::size_t>(std::distance(levels.begin(), actual));
        for (std::size_t offset = 0; offset < expectedDown.size(); ++offset) {
            if (index <= offset) {
                downMatches = false;
                break;
            }
            const int percent = static_cast<int>(std::llround(
                levels[index - offset - 1] * 100.0 / zoomBase));
            downMatches = downMatches && percent == expectedDown[offset];
        }
        for (std::size_t offset = 0; offset < expectedUp.size(); ++offset) {
            if (index + offset + 1 >= levels.size()) {
                upMatches = false;
                break;
            }
            const int percent = static_cast<int>(std::llround(
                levels[index + offset + 1] * 100.0 / zoomBase));
            upMatches = upMatches && std::abs(percent - expectedUp[offset]) <= 2;
        }
    }
    passOrFail("zoom buttons and wheel share the Picasa-style geometric levels",
        downMatches && upMatches &&
        std::llround(levels.front() * 100.0 / zoomBase) == ZoomPolicy::MIN_PERCENT &&
        std::llround(levels.back() * 100.0 / zoomBase) == ZoomPolicy::MAX_PERCENT);
    passOrFail("zoom animation uses monotonic smooth-step timing",
        ZoomPolicy::ANIMATION_DURATION_MS == 220 &&
        ZoomPolicy::easeSmoothStep(0.0) == 0.0 &&
        ZoomPolicy::easeSmoothStep(0.25) < ZoomPolicy::easeSmoothStep(0.5) &&
        ZoomPolicy::easeSmoothStep(0.5) < ZoomPolicy::easeSmoothStep(0.75) &&
        ZoomPolicy::easeSmoothStep(1.0) == 1.0);
    passOrFail("zoom labels round the settled 115 percent target consistently",
        ZoomPolicy::displayPercent(std::llround(zoomBase * 1.15), zoomBase) == 115);
    passOrFail("zoom percentage indicator holds and then fades completely",
        ZoomPolicy::indicatorAlpha(-1) == 0 &&
        ZoomPolicy::indicatorAlpha(0) == 255 &&
        ZoomPolicy::indicatorAlpha(ZoomPolicy::INDICATOR_HOLD_MS) == 255 &&
        ZoomPolicy::indicatorAlpha(ZoomPolicy::INDICATOR_HOLD_MS +
            ZoomPolicy::INDICATOR_FADE_MS / 2) > 0 &&
        ZoomPolicy::indicatorAlpha(ZoomPolicy::INDICATOR_TOTAL_MS) == 0);
    passOrFail("canvas presentation is synchronized to the display refresh",
        FramePacingPolicy::usesDisplaySynchronizedPresent());
}

void expectZoomEditPolicy() {
    std::string text = "98";
    passOrFail("first typed digit replaces the selected zoom percentage",
        ZoomEditPolicy::appendDigit(text, '1', true) && text == "1");
    passOrFail("zoom editor appends digits up to its visible numeric limit",
        ZoomEditPolicy::appendDigit(text, '5', false) &&
        ZoomEditPolicy::appendDigit(text, '0', false) && text == "150");
    passOrFail("zoom editor accepts exact values and clamps supported bounds",
        ZoomEditPolicy::parsePercent("150") == 150 &&
        ZoomEditPolicy::parsePercent("0") == ZoomEditPolicy::MIN_PERCENT &&
        ZoomEditPolicy::parsePercent("99999") == ZoomEditPolicy::MAX_PERCENT);
    passOrFail("zoom editor rejects empty or non-numeric values",
        !ZoomEditPolicy::parsePercent("").has_value() &&
        !ZoomEditPolicy::parsePercent("12x").has_value());
}

void expectSlideshowPolicy() {
    passOrFail("slideshow advances every three seconds only with multiple images",
        SlideshowPolicy::INTERVAL_MS == 3000 &&
        !SlideshowPolicy::canPlay(1) && SlideshowPolicy::canPlay(2) &&
        !SlideshowPolicy::shouldAdvance(false, 3, true) &&
        !SlideshowPolicy::shouldAdvance(true, 1, true) &&
        !SlideshowPolicy::shouldAdvance(true, 3, false) &&
        SlideshowPolicy::shouldAdvance(true, 3, true));
}

// 按 libjpeg 的公式造一张量化表，用来验证反推是否自洽
static JpegQuality::Table makeLibjpegTable(int quality, int id = 0) {
    const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    JpegQuality::Table table;
    table.id = id;
    const auto& standard = id == 0 ?
        JpegQuality::STANDARD_LUMINANCE : JpegQuality::STANDARD_CHROMINANCE;
    for (int i = 0; i < 64; ++i)
        table.values[i] = std::clamp((standard[i] * scale + 50) / 100, 1, 255);
    return table;
}

void expectJpegQuality() {
    // 自洽：按 libjpeg 公式造表，再反推应当回到原质量（取整误差 ±1）
    bool roundTrip = true;
    for (int q : { 20, 30, 40, 50, 60, 70, 75, 80, 85, 90, 95 }) {
        const auto table = makeLibjpegTable(q);
        const auto scale = JpegQuality::estimateScale(table, JpegQuality::STANDARD_LUMINANCE);
        if (!scale) { roundTrip = false; break; }
        if (std::abs(JpegQuality::scaleToQuality(*scale) - q) > 1) { roundTrip = false; break; }
    }
    passOrFail("jpeg quality round-trips through libjpeg's table scaling", roundTrip);

    // 质量 100 时整张表被夹成全 1，这种情况精确判定而不走反推
    passOrFail("jpeg quality reports 100 for an all-ones table",
        JpegQuality::isAllOnes(makeLibjpegTable(100)) &&
        !JpegQuality::isAllOnes(makeLibjpegTable(90)));

    // 有效项太少就不给结论，不能硬编一个数
    JpegQuality::Table saturated;
    saturated.values.fill(255);
    passOrFail("jpeg quality declines to guess when the table is saturated",
        !JpegQuality::estimateScale(saturated, JpegQuality::STANDARD_LUMINANCE).has_value());

    // 不是 JPEG 就没有量化表
    const std::array<uint8_t, 8> png{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    passOrFail("jpeg quality returns nothing for non-JPEG data",
        !JpegQuality::estimate(png).has_value() &&
        JpegQuality::parseTables(png).empty());

    // 真实 JPEG 的标记扫描：造一个只含 SOI + DQT + SOS 的最小文件
    std::vector<uint8_t> minimal{ 0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x43, 0x00 };
    const auto table = makeLibjpegTable(85);
    for (int i = 0; i < 64; ++i)
        minimal.push_back(static_cast<uint8_t>(table.values[i]));
    minimal.insert(minimal.end(), { 0xFF, 0xDA, 0x00, 0x02 });
    const auto parsed = JpegQuality::parseTables(minimal);
    const auto estimated = JpegQuality::estimate(minimal);
    passOrFail("jpeg quality reads the quantization table out of a DQT segment",
        parsed.size() == 1 && parsed[0].id == 0 && !parsed[0].sixteenBit &&
        estimated.has_value() && std::abs(*estimated - 85) <= 1);
}

void expectColorSpaceName() {
    // EXIF ColorSpace 的三种取值
    passOrFail("color space maps the EXIF ColorSpace values",
        ColorSpaceName::fromExifColorSpace("1") == "sRGB" &&
        ColorSpaceName::fromExifColorSpace("2") == "Adobe RGB" &&
        // 65535 是「未校准」，等于没说，不能把它当成一个色彩空间显示出来
        ColorSpaceName::fromExifColorSpace("65535").empty());

    // ICC v2 的 desc 标签：128 字节头 + 标签数 + 一条 12 字节标签项 + textDescriptionType
    const char* name = "Adobe RGB (1998)";
    const uint32_t asciiCount = 17;   // 含结尾 0
    std::vector<uint8_t> icc(128, 0);
    const auto pushBE32 = [](std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
        };
    pushBE32(icc, 1);              // 标签数
    pushBE32(icc, 0x64657363u);    // 'desc'
    const uint32_t dataOffset = 144;
    pushBE32(icc, dataOffset);
    pushBE32(icc, 12 + asciiCount);
    icc.resize(dataOffset, 0);
    pushBE32(icc, 0x64657363u);    // type 'desc'
    pushBE32(icc, 0);              // reserved
    pushBE32(icc, asciiCount);
    for (const char* p = name; *p; ++p)
        icc.push_back(static_cast<uint8_t>(*p));
    icc.push_back(0);
    // 头部的声明长度必须与实际一致，否则解析会拒绝跳偏移
    icc[0] = static_cast<uint8_t>(icc.size() >> 24);
    icc[1] = static_cast<uint8_t>(icc.size() >> 16);
    icc[2] = static_cast<uint8_t>(icc.size() >> 8);
    icc[3] = static_cast<uint8_t>(icc.size());

    passOrFail("color space reads the description out of an ICC v2 profile",
        ColorSpaceName::fromIccProfile(icc) == "Adobe RGB (1998)");

    // ICC 优先于 EXIF：相机导出 Adobe RGB 时 EXIF 常写 65535，只看 EXIF 会显示不出来
    passOrFail("color space prefers the ICC description over EXIF",
        ColorSpaceName::resolve(icc, "65535") == "Adobe RGB (1998)" &&
        ColorSpaceName::resolve({}, "1") == "sRGB");

    // InteropIndex 是 Adobe RGB 的另一处线索
    passOrFail("color space falls back to the EXIF interoperability index",
        ColorSpaceName::resolve({}, "65535", "R03") == "Adobe RGB" &&
        ColorSpaceName::resolve({}, "65535", "R98") == "sRGB");

    // 什么线索都没有时返回空——不猜
    passOrFail("color space stays empty when nothing identifies it",
        ColorSpaceName::resolve({}, "").empty() &&
        ColorSpaceName::fromIccProfile({}).empty());

    // 截断的 ICC 不能让解析越界或吐出乱码
    std::vector<uint8_t> truncated(icc.begin(), icc.begin() + 140);
    passOrFail("color space survives a truncated ICC profile",
        ColorSpaceName::fromIccProfile(truncated).empty());
}

void expectImageHistogram() {
    // 抽样步长：小图全取，大图按目标采样数收敛
    passOrFail("histogram samples every pixel only while the image is small",
        ImageHistogram::sampleStride(100, 100) == 1 && ImageHistogram::sampleStride(500, 500) == 1 &&
        ImageHistogram::sampleStride(9000, 9000) > 1 &&
        static_cast<uint64_t>(9000 / ImageHistogram::sampleStride(9000, 9000)) *
            (9000 / ImageHistogram::sampleStride(9000, 9000)) <= ImageHistogram::TARGET_SAMPLES * 2);

    // BT.601 权重：纯绿的亮度应远高于纯蓝
    passOrFail("histogram weights luminance by BT.601",
        ImageHistogram::lumaOf(0, 255, 0) == 149 && ImageHistogram::lumaOf(0, 0, 255) == 76 &&
        ImageHistogram::lumaOf(255, 0, 0) == 29 && ImageHistogram::lumaOf(255, 255, 255) == 255);

    ImageHistogram::Bins bins;
    ImageHistogram::accumulate(bins, 10, 20, 30);
    ImageHistogram::accumulate(bins, 10, 20, 30);
    ImageHistogram::accumulate(bins, 200, 200, 200);
    passOrFail("histogram counts each sampled pixel once per channel",
        bins.sampled == 3 && bins.blue[10] == 2 && bins.green[20] == 2 &&
        bins.red[30] == 2 && bins.blue[200] == 1);

    // 峰值归一化会被单一色块压平：一个 bin 占绝大多数时，
    // 其余形状必须还能看见，否则直方图等于没显示
    std::array<uint32_t, ImageHistogram::HISTOGRAM_BINS> flooded{};
    flooded[0] = 1'000'000;        // 纯色背景
    flooded[100] = 1000;           // 真正关心的分布
    flooded[101] = 800;
    flooded[102] = 600;
    const auto naive = ImageHistogram::normalize(flooded, 64, 0);   // 不裁离群值
    const auto clipped = ImageHistogram::normalize(flooded, 64, 3); // 裁掉最高 3 个
    passOrFail("histogram clips outlier bins so the rest of the shape stays visible",
        naive[100] == 0 && clipped[100] == 64 && clipped[101] > 0);

    // 全零直方图不能除零
    std::array<uint32_t, ImageHistogram::HISTOGRAM_BINS> zero{};
    const auto zeroed = ImageHistogram::normalize(zero, 64);
    passOrFail("histogram handles an all-zero channel without dividing by zero",
        std::ranges::all_of(zeroed, [](int v) { return v == 0; }));

    // 只有一个 bin 非零时，裁离群值会把峰值裁成 0，必须退回真实峰值
    std::array<uint32_t, ImageHistogram::HISTOGRAM_BINS> single{};
    single[128] = 500;
    const auto singleHeights = ImageHistogram::normalize(single, 64, 3);
    passOrFail("histogram falls back to the real peak when clipping empties it",
        singleHeights[128] == 64);

    // 256 个 bin 摊到窄面板上，细峰不能被漏掉
    std::vector<int> heights(ImageHistogram::HISTOGRAM_BINS, 0);
    heights[200] = 50;
    const auto columns = ImageHistogram::resampleToWidth(heights, 64);
    passOrFail("histogram resampling keeps a narrow peak visible",
        columns.size() == 64 &&
        std::ranges::any_of(columns, [](int v) { return v == 50; }));
    passOrFail("histogram resampling tolerates a zero width",
        ImageHistogram::resampleToWidth(heights, 0).empty());
}

void expectHistogramFromPixels() {
    // 三通道 BGRA：造一张左半纯蓝、右半纯红的图，统计结果应当各占一半
    constexpr int width = 64, height = 32, channels = 4;
    std::vector<uint8_t> buffer(static_cast<std::size_t>(width) * height * channels, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* p = buffer.data() + (static_cast<std::size_t>(y) * width + x) * channels;
            if (x < width / 2) { p[0] = 255; p[1] = 0; p[2] = 0; }   // 蓝
            else { p[0] = 0; p[1] = 0; p[2] = 255; }                 // 红
            p[3] = 255;
        }
    }
    const auto bins = ImageHistogram::accumulateFrom(width, height, channels,
        [&](int y) { return buffer.data() + static_cast<std::size_t>(y) * width * channels; });

    const uint64_t half = static_cast<uint64_t>(width) * height / 2;
    passOrFail("histogram accumulates a real pixel buffer channel by channel",
        bins.sampled == static_cast<uint64_t>(width) * height &&
        bins.blue[255] == half && bins.blue[0] == half &&
        bins.red[255] == half && bins.red[0] == half &&
        bins.green[0] == static_cast<uint64_t>(width) * height);

    // 单通道灰度：三个通道应填同一个值，亮度等于该值
    std::vector<uint8_t> gray(static_cast<std::size_t>(width) * height, 128);
    const auto grayBins = ImageHistogram::accumulateFrom(width, height, 1,
        [&](int y) { return gray.data() + static_cast<std::size_t>(y) * width; });
    passOrFail("histogram treats a single-channel buffer as gray",
        grayBins.blue[128] == grayBins.sampled &&
        grayBins.green[128] == grayBins.sampled &&
        grayBins.red[128] == grayBins.sampled &&
        grayBins.luma[128] == grayBins.sampled);

    // 空输入与非法通道数不能越界访问
    passOrFail("histogram rejects degenerate buffer descriptions",
        ImageHistogram::accumulateFrom(0, 32, 4, [&](int) { return buffer.data(); }).empty() &&
        ImageHistogram::accumulateFrom(64, 0, 4, [&](int) { return buffer.data(); }).empty() &&
        ImageHistogram::accumulateFrom(64, 32, 0, [&](int) { return buffer.data(); }).empty() &&
        ImageHistogram::accumulateFrom(64, 32, 4,
            [](int) -> const uint8_t* { return nullptr; }).empty());
}

void expectInfoPanelFields() {
    const std::string raw =
        "路径: D:\\photo\\IMG_0001.jpg\n"
        "大小: 4.2 MB\n"
        "分辨率: 6000x4000\n"
        "色彩空间: 65535\n"
        "型号: ILCE-7S\n";

    // 解析好的色彩空间与质量因子应当出现在基本信息里
    const auto model = ImageInfoPresentation::build(raw, UiLanguage::SIMPLIFIED, "RGB · 24bpp",
        "Adobe RGB (1998)", 85);
    const auto hasBasic = [&model](std::string_view label, std::string_view value) {
        return std::ranges::any_of(model.basic, [&](const ImageInfoPresentation::Row& row) {
            return row.label == label && row.value == value;
            });
        };
    passOrFail("info panel shows the resolved color space and quality factor",
        hasBasic("色彩空间", "Adobe RGB (1998)") && hasBasic("质量因子", "约 85"));

    // 同一份 EXIF 换成繁體，标签要跟着换成台湾用词，不能还是简体那套
    const auto traditional = ImageInfoPresentation::build(raw, UiLanguage::TRADITIONAL,
        "RGB · 24bpp", "Adobe RGB (1998)", 85);
    const auto hasTraditional = [&traditional](std::string_view label) {
        return std::ranges::any_of(traditional.basic,
            [&](const ImageInfoPresentation::Row& row) { return row.label == label; });
        };
    passOrFail("the info panel uses Taiwanese wording in traditional Chinese",
        hasTraditional("檔案名稱") && hasTraditional("解析度") &&
        hasTraditional("色彩空間") && hasTraditional("品質因子") &&
        std::ranges::any_of(traditional.details,
            [](const ImageInfoPresentation::Row& row) { return row.label == "相機"; }));

    // EXIF 原始值 65535 不能再作为「色彩空间」重复列一遍——
    // 同一个标签出现两次、其中一个还是看不懂的数字，只会让人犯疑
    passOrFail("info panel drops the raw EXIF color space once it is resolved",
        std::ranges::none_of(model.details, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间";
            }));

    // 两项都很短，紧凑面板也要能看到
    const auto compact = ImageInfoPresentation::compactRows(model, UiLanguage::SIMPLIFIED);
    passOrFail("info panel surfaces both fields in the compact layout",
        std::ranges::any_of(compact, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间"; }) &&
        std::ranges::any_of(compact, [](const ImageInfoPresentation::Row& row) {
            return row.label == "质量因子"; }));

    // 估不出质量因子（0）时不能显示「约 0」；色彩空间为空时同理
    const auto bare = ImageInfoPresentation::build(raw, UiLanguage::SIMPLIFIED, "RGB · 24bpp", {}, 0);
    passOrFail("info panel omits the fields when nothing identifies them",
        std::ranges::none_of(bare.basic, [](const ImageInfoPresentation::Row& row) {
            return row.label == "质量因子" || row.label == "色彩空间"; }) &&
        // 这时 EXIF 原始值该照旧出现在照片信息里，不能一起丢掉
        std::ranges::any_of(bare.details, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间"; }));

    // 英文界面用英文标签
    const auto english = ImageInfoPresentation::build(raw, UiLanguage::ENGLISH, "RGB · 24bpp", "sRGB", 92);
    passOrFail("info panel labels the fields in English when the UI is English",
        std::ranges::any_of(english.basic, [](const ImageInfoPresentation::Row& row) {
            return row.label == "Color space" && row.value == "sRGB"; }) &&
        std::ranges::any_of(english.basic, [](const ImageInfoPresentation::Row& row) {
            return row.label == "Quality" && row.value == "~92"; }));
}

void expectInfoPanelOpacity() {
    using namespace ViewerOptions;

    // 默认档保持加这个选项之前的 alpha，升级后观感不变
    passOrFail("info panel keeps its original alpha on the default level",
        infoPanelAlpha(DEFAULT_INFO_PANEL_OPACITY) == 0xD1u &&
        DEFAULT_INFO_PANEL_OPACITY == InfoPanelOpacity::Strong);

    passOrFail("info panel opacity levels increase monotonically",
        infoPanelAlpha(InfoPanelOpacity::Light) < infoPanelAlpha(InfoPanelOpacity::Medium) &&
        infoPanelAlpha(InfoPanelOpacity::Medium) < infoPanelAlpha(InfoPanelOpacity::Strong) &&
        infoPanelAlpha(InfoPanelOpacity::Strong) < infoPanelAlpha(InfoPanelOpacity::Opaque) &&
        infoPanelAlpha(InfoPanelOpacity::Opaque) == 0xFFu);

    // 只换 alpha，颜色本身不能动
    passOrFail("info panel opacity replaces only the alpha byte",
        withPanelAlpha(0xD10A0E1Au, InfoPanelOpacity::Opaque) == 0xFF0A0E1Au &&
        withPanelAlpha(0xD10A0E1Au, InfoPanelOpacity::Light) == 0x8C0A0E1Au);

    std::array<uint32_t, 1024> storage{};
    reset(storage.data(), storage.size());
    passOrFail("info panel options default after a reset",
        infoPanelOpacity(storage.data()) == DEFAULT_INFO_PANEL_OPACITY &&
        infoHistogramEnabled(storage.data()) == DEFAULT_INFO_HISTOGRAM);

    setInfoPanelOpacity(storage.data(), InfoPanelOpacity::Light);
    setInfoHistogramEnabled(storage.data(), false);
    passOrFail("info panel options round-trip through storage",
        infoPanelOpacity(storage.data()) == InfoPanelOpacity::Light &&
        !infoHistogramEnabled(storage.data()));

    // 越界值回落到默认，不能把野值透出去
    storage[INFO_PANEL_OPACITY_INDEX] = 99u;
    storage[INFO_HISTOGRAM_INDEX] = 7u;
    initialize(storage.data(), storage.size());
    passOrFail("info panel options fall back when storage holds out-of-range values",
        infoPanelOpacity(storage.data()) == DEFAULT_INFO_PANEL_OPACITY &&
        infoHistogramEnabled(storage.data()) == DEFAULT_INFO_HISTOGRAM);

    // 版本 1 的设置文件升级到 2：只补新字段，用户改过的既有配置必须保留
    std::array<uint32_t, 1024> upgraded{};
    reset(upgraded.data(), upgraded.size());
    setOpenMode(upgraded.data(), OpenMode::FitImage);
    setDoubleClickAction(upgraded.data(), DoubleClickAction::None);
    setEdgeArrowsEnabled(upgraded.data(), true);
    upgraded[VERSION_INDEX] = 1u;
    upgraded[INFO_PANEL_OPACITY_INDEX] = 0u;   // 版本 1 里这两格是空的
    upgraded[INFO_HISTOGRAM_INDEX] = 0u;
    initialize(upgraded.data(), upgraded.size());
    passOrFail("info panel options are seeded incrementally without wiping older settings",
        upgraded[VERSION_INDEX] == STORAGE_VERSION &&
        infoPanelOpacity(upgraded.data()) == DEFAULT_INFO_PANEL_OPACITY &&
        infoHistogramEnabled(upgraded.data()) == DEFAULT_INFO_HISTOGRAM &&
        openMode(upgraded.data()) == OpenMode::FitImage &&
        doubleClickAction(upgraded.data()) == DoubleClickAction::None &&
        edgeArrowsEnabled(upgraded.data()));
}

void expectLivePhotoOptions() {
    using namespace ViewerOptions;
    std::array<uint32_t, 1024> storage{};
    reset(storage.data(), storage.size());
    passOrFail("live photo autoplay is muted by default, matching macOS Photos",
        !livePhotoAutoSound(storage.data()) && !DEFAULT_LIVE_PHOTO_SOUND);

    setLivePhotoAutoSound(storage.data(), true);
    passOrFail("live photo autoplay sound round-trips through storage",
        livePhotoAutoSound(storage.data()));

    storage[LIVE_PHOTO_SOUND_INDEX] = 5u;
    initialize(storage.data(), storage.size());
    passOrFail("live photo autoplay sound falls back when storage holds an out-of-range value",
        livePhotoAutoSound(storage.data()) == DEFAULT_LIVE_PHOTO_SOUND);

    // 版本 2 升级到 3：只补新字段。这一格故意放个 1，证明迁移确实写了默认值，
    // 而不是碰巧原本就是 0。用户改过的既有配置必须原样保留。
    std::array<uint32_t, 1024> upgraded{};
    reset(upgraded.data(), upgraded.size());
    setOpenMode(upgraded.data(), OpenMode::FitImage);
    setInfoPanelOpacity(upgraded.data(), InfoPanelOpacity::Light);
    setInfoHistogramEnabled(upgraded.data(), false);
    upgraded[VERSION_INDEX] = 2u;
    upgraded[LIVE_PHOTO_SOUND_INDEX] = 1u;
    initialize(upgraded.data(), upgraded.size());
    passOrFail("version 2 settings survive while new fields are seeded",
        upgraded[VERSION_INDEX] == STORAGE_VERSION && STORAGE_VERSION == 4u &&
        livePhotoAutoSound(upgraded.data()) == DEFAULT_LIVE_PHOTO_SOUND &&
        fullscreenInfoBar(upgraded.data()) == DEFAULT_FULLSCREEN_INFO &&
        openMode(upgraded.data()) == OpenMode::FitImage &&
        infoPanelOpacity(upgraded.data()) == InfoPanelOpacity::Light &&
        !infoHistogramEnabled(upgraded.data()));

    // 版本 3 升级到 4：只补全屏信息条这一格，版本 3 之前的配置一律不动。
    // 这一格故意先填 1，证明迁移确实写了默认值，而不是碰巧原本就是 0。
    std::array<uint32_t, 1024> fromVersion3{};
    reset(fromVersion3.data(), fromVersion3.size());
    setLivePhotoAutoSound(fromVersion3.data(), true);
    setDoubleClickAction(fromVersion3.data(), DoubleClickAction::None);
    fromVersion3[VERSION_INDEX] = 3u;
    fromVersion3[FULLSCREEN_INFO_INDEX] = 7u;
    initialize(fromVersion3.data(), fromVersion3.size());
    passOrFail("the fullscreen info bar is seeded without wiping version 3 settings",
        fullscreenInfoBar(fromVersion3.data()) == DEFAULT_FULLSCREEN_INFO &&
        livePhotoAutoSound(fromVersion3.data()) &&
        doubleClickAction(fromVersion3.data()) == DoubleClickAction::None);

    // 新增的两个枚举值必须能存下来：双击「下一张」、打开方式「图片适应窗口」
    std::array<uint32_t, 1024> added{};
    reset(added.data(), added.size());
    setDoubleClickAction(added.data(), DoubleClickAction::NextImage);
    setOpenMode(added.data(), OpenMode::FitImageInWindow);
    initialize(added.data(), added.size());
    passOrFail("double-click next image and fit-in-window open mode round-trip",
        doubleClickAction(added.data()) == DoubleClickAction::NextImage &&
        openMode(added.data()) == OpenMode::FitImageInWindow &&
        !opensImmersive(OpenMode::FitImageInWindow));

    // 已经是版本 3 的文件：用户打开的声音不能被迁移逻辑冲掉
    std::array<uint32_t, 1024> current{};
    reset(current.data(), current.size());
    setLivePhotoAutoSound(current.data(), true);
    initialize(current.data(), current.size());
    passOrFail("a version 3 settings file keeps the user's live photo sound choice",
        livePhotoAutoSound(current.data()));
}

void expectMotionTiming() {
    using namespace MotionTiming;
    passOrFail("frame duration follows the stream frame rate",
        frameMsFromRate(30, 1) == 33 && frameMsFromRate(60, 1) == 17 &&
        frameMsFromRate(30000, 1001) == 33 && frameMsFromRate(24, 1) == 42 &&
        frameMsFromRate(0, 0) == FALLBACK_FRAME_MS && frameMsFromRate(30, 0) == FALLBACK_FRAME_MS);

    passOrFail("30 fps timestamps give per-frame durations that add up to real time",
        durationsFromTimestamps({ 0, 33, 67, 100 }, 33) == std::vector<int>{ 33, 34, 33, 33 });
    passOrFail("60 fps clips are timed at 60 fps instead of the old fixed 33 ms",
        durationsFromTimestamps({ 0, 17, 33, 50 }, 33) == std::vector<int>{ 17, 16, 17, 17 });
    passOrFail("missing timestamps fall back to the stream frame duration",
        durationsFromTimestamps({ 0, NO_TIMESTAMP, 67, 100 }, 33) == std::vector<int>{ 33, 33, 33, 33 });
    passOrFail("backwards or huge timestamp jumps are not trusted",
        durationsFromTimestamps({ 0, 33, 20, 53 }, 30) == std::vector<int>{ 33, 30, 33, 33 } &&
        durationsFromTimestamps({ 0, 5000 }, 40) == std::vector<int>{ 40, 40 });
    passOrFail("degenerate timestamp lists stay well formed",
        durationsFromTimestamps({}, 33).empty() &&
        durationsFromTimestamps({ 7 }, 25) == std::vector<int>{ 25 });

    const std::vector<int> durations{ 33, 34, 33 };
    passOrFail("frame start times accumulate along the timeline",
        frameStartMs(durations, 0) == 0 && frameStartMs(durations, 1) == 33 &&
        frameStartMs(durations, 2) == 67 && frameStartMs(durations, 3) == 100 &&
        totalMs(durations) == 100);
    passOrFail("the timeline picks the frame covering the elapsed time",
        frameIndexAt(durations, -5) == 0 && frameIndexAt(durations, 0) == 0 &&
        frameIndexAt(durations, 32) == 0 && frameIndexAt(durations, 33) == 1 &&
        frameIndexAt(durations, 66) == 1 && frameIndexAt(durations, 67) == 2 &&
        frameIndexAt(durations, 99) == 2);
    passOrFail("the timeline reports the end once all frames have been shown",
        frameIndexAt(durations, 100) == -1 && frameIndexAt({}, 0) == -1);

    // 1 kHz 单声道：一个采样正好 1 ms，偏移量一眼看得出来
    const std::vector<int16_t> ramp{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    auto late = ramp;
    alignAudio(late, 1000, 1, 3, 100);
    passOrFail("audio that starts after the first frame is padded with silence",
        late.size() == 13 && late[0] == 0 && late[2] == 0 && late[3] == 1 && late[12] == 10);
    auto early = ramp;
    alignAudio(early, 1000, 1, -3, 100);
    passOrFail("audio that starts before the first frame has its head trimmed",
        early.size() == 7 && early.front() == 4 && early.back() == 10);
    auto longer = ramp;
    alignAudio(longer, 1000, 1, 0, 5);
    passOrFail("audio stops together with the last frame",
        longer.size() == 5 && longer.back() == 5);
    std::vector<int16_t> stereo{ 1, -1, 2, -2, 3, -3, 4, -4 };
    alignAudio(stereo, 1000, 2, -1, 100);
    passOrFail("alignment moves whole sample frames so stereo channels stay paired",
        stereo.size() == 6 && stereo[0] == 2 && stereo[1] == -2);
    auto afterEnd = ramp;
    alignAudio(afterEnd, 1000, 1, 100, 100);
    auto implausible = ramp;
    alignAudio(implausible, 1000, 1, -(MAX_AUDIO_LEAD_MS + 1), 100);
    passOrFail("audio that cannot overlap the picture is dropped rather than misplaced",
        afterEnd.empty() && implausible.empty());

    AudioClip clip;
    clip.sampleRate = 48000;
    clip.channels = 2;
    clip.samples.resize(48000 * 2 * 3);
    passOrFail("an audio clip reports its length in milliseconds",
        clip.durationMs() == 3000 && clip.frameCount() == 144000 && !clip.empty() && AudioClip{}.empty());
}

void expectLivePhotoBadge() {
    using namespace LivePhotoBadge;
    passOrFail("the LIVE badge text width follows the loading badge estimate",
        logicalTextWidth(L"实况") == 28 && logicalTextWidth(L"LIVE") == 28 && logicalWidth(28) == 66);

    const Rect image{ 100, 50, 400, 300 };
    const Rect placed = place(image, 800, 600, 96, 28);
    passOrFail("the LIVE badge sits just inside the image's top-left corner",
        placed.x == 112 && placed.y == 62 && placed.width == 66 && placed.height == 24);

    const Rect zoomed = place({ -500, -300, 2000, 1500 }, 800, 600, 96, 28);
    passOrFail("a zoomed-in image keeps the badge at the window's top-left",
        zoomed.x == 12 && zoomed.y == 12);

    const Rect offRight = place({ 790, 590, 400, 300 }, 800, 600, 96, 28);
    passOrFail("the badge is kept inside the window when the image is panned away",
        offRight.x == 800 - 66 - 12 && offRight.y == 600 - 24 - 12);

    const Rect hiDpi = place(image, 1600, 1200, 192, 28);
    passOrFail("the badge scales with DPI",
        hiDpi.width == 132 && hiDpi.height == 48 && hiDpi.x == 124 && hiDpi.y == 74);

    passOrFail("a window too small for the badge draws and hits nothing",
        place(image, 60, 40, 96, 28).empty() && !place(image, 60, 40, 96, 28).contains(0, 0));

    passOrFail("the badge hit area is its own rectangle",
        placed.contains(112, 62) && placed.contains(177, 85) &&
        !placed.contains(178, 62) && !placed.contains(112, 86) && !placed.contains(111, 70));
}

void expectLoadingBadge() {
    // 估不出耗时（尺寸没查到）：只出文字，不能凭空编个数字
    passOrFail("loading badge omits the countdown when no estimate is available",
        LoadingBadge::compose("原图加载中", 0, 500) == "原图加载中" &&
        LoadingBadge::compose("原图加载中", -1, 500) == "原图加载中");

    // 倒计时按毫秒三位显示，数字才明显在动
    passOrFail("loading badge counts down with millisecond precision",
        LoadingBadge::compose("原图加载中", 2800, 0) == "原图加载中  2.800s" &&
        LoadingBadge::compose("原图加载中", 2800, 947) == "原图加载中  1.853s" &&
        LoadingBadge::compose("原图加载中", 2800, 2799) == "原图加载中  0.001s");

    // 估短了就收起数字：继续显示 0.000 是在撒谎，解码根本没有进度回调
    passOrFail("loading badge drops the countdown once the estimate is exhausted",
        LoadingBadge::compose("原图加载中", 2800, 2800) == "原图加载中" &&
        LoadingBadge::compose("原图加载中", 2800, 9000) == "原图加载中");
}

void expectDecodeEstimate() {
    DecodeEstimate::Model model;

    // 输出字节数 = 宽 × 高 × 位深 ÷ 8。位深缺失或离谱时按 32 位兜底，宁可高估。
    passOrFail("decode estimate derives output bytes from dimensions and bit depth",
        DecodeEstimate::outputBytes(9000, 9000, 48) == 486'000'000LL &&
        DecodeEstimate::outputBytes(100, 100, 0) == 40'000LL &&
        DecodeEstimate::outputBytes(100, 100, 999) == 40'000LL &&
        DecodeEstimate::outputBytes(0, 100, 24) == 0LL);

    // 静态速率表按实测最慢值取。moon_81M.png 实测 2793 ms，估值应落在同一量级且不低估。
    const int64_t moonBytes = DecodeEstimate::outputBytes(9000, 9000, 48);
    const int64_t moonEstimate = model.estimateMs(L"D:\\x\\moon.png", moonBytes);
    passOrFail("decode estimate for a 486 MB PNG lands near the measured 2793 ms",
        moonEstimate >= 2400 && moonEstimate <= 3400);

    // JPEG 快两个数量级，不能套用 PNG 的速率
    const int64_t jpegBytes = DecodeEstimate::outputBytes(8191, 8193, 24);
    const int64_t jpegEstimate = model.estimateMs(L"D:\\x\\big.jpg", jpegBytes);
    passOrFail("decode estimate separates JPEG throughput from PNG",
        jpegEstimate >= 60 && jpegEstimate <= 220 && jpegEstimate * 8 < moonEstimate);

    // 扩展名大小写不敏感
    passOrFail("decode estimate matches extensions case-insensitively",
        model.estimateMs(L"D:\\x\\big.JPG", jpegBytes) == jpegEstimate);

    // 没有尺寸就没有估值，不能编
    passOrFail("decode estimate yields nothing without dimensions",
        model.estimateMs(L"D:\\x\\big.jpg", 0) == 0);

    // 回采真实耗时后，后续估值应向实测收敛
    const int64_t before = model.estimateMs(L"D:\\x\\a.png", moonBytes);
    for (int i = 0; i < 12; ++i)
        model.record(L"D:\\x\\a.png", moonBytes, 1200);   // 实测比静态表快得多
    const int64_t after = model.estimateMs(L"D:\\x\\a.png", moonBytes);
    passOrFail("decode estimate converges toward observed throughput",
        before > 2000 && after >= 1100 && after <= 1400 && after < before);

    // 计时噪声样本要丢掉，否则速率会被拉飞
    DecodeEstimate::Model noisy;
    const int64_t baseline = noisy.estimateMs(L"D:\\x\\b.png", moonBytes);
    for (int i = 0; i < 12; ++i)
        noisy.record(L"D:\\x\\b.png", moonBytes, 1);      // 1 ms 显然是噪声
    passOrFail("decode estimate ignores implausibly short samples",
        noisy.estimateMs(L"D:\\x\\b.png", moonBytes) == baseline);
}

void expectImageViewTransform() {
    const auto identity = ImageViewTransform::displayToSource(2, 3, 10, 8, 0, false, false);
    const auto horizontal = ImageViewTransform::displayToSource(2, 3, 10, 8, 0, true, false);
    const auto vertical = ImageViewTransform::displayToSource(2, 3, 10, 8, 0, false, true);
    const auto clockwise = ImageViewTransform::displayToSource(2, 3, 8, 10, 1, false, false);
    const auto combined = ImageViewTransform::displayToSource(2, 3, 8, 10, 1, true, true);
    passOrFail("image view transforms preserve, flip, and rotate source coordinates",
        identity.x == 2 && identity.y == 3 &&
        horizontal.x == 7 && horizontal.y == 3 &&
        vertical.x == 2 && vertical.y == 4 &&
        clockwise.x == 6 && clockwise.y == 2 &&
        combined.x == 3 && combined.y == 5);
}

void expectViewerOptions() {
    std::array<uint32_t, 777> storage{};
    ViewerOptions::initialize(storage.data(), storage.size());
    passOrFail("viewer options start from the documented defaults",
        storage[ViewerOptions::MAGIC_INDEX] == ViewerOptions::STORAGE_MAGIC &&
        !ViewerOptions::edgeArrowsEnabled(storage.data()) &&
        ViewerOptions::dragMovesWindow(storage.data()) &&
        ViewerOptions::doubleClickAction(storage.data()) ==
            ViewerOptions::DoubleClickAction::ToggleFullscreen &&
        ViewerOptions::openMode(storage.data()) ==
            ViewerOptions::OpenMode::ImmersivePreview);

    // 新配置区不能和快捷键区重叠，否则改一个会串掉另一个。
    passOrFail("viewer options live clear of the shortcut storage range",
        ViewerOptions::STORAGE_BASE >
            ShortcutConfig::BINDING_BASE_INDEX + ShortcutConfig::DEFAULT_BINDINGS.size());

    ViewerOptions::setEdgeArrowsEnabled(storage.data(), true);
    ViewerOptions::setDoubleClickAction(storage.data(),
        ViewerOptions::DoubleClickAction::ToggleMaximize);
    ViewerOptions::setOpenMode(storage.data(), ViewerOptions::OpenMode::FitImage);
    ViewerOptions::setDragMovesWindow(storage.data(), false);
    ViewerOptions::initialize(storage.data(), storage.size());
    passOrFail("valid viewer options survive revalidation",
        ViewerOptions::edgeArrowsEnabled(storage.data()) &&
        !ViewerOptions::dragMovesWindow(storage.data()) &&
        ViewerOptions::doubleClickAction(storage.data()) ==
            ViewerOptions::DoubleClickAction::ToggleMaximize &&
        ViewerOptions::openMode(storage.data()) == ViewerOptions::OpenMode::FitImage);

    // 设置文件里这块区域可能是旧版留下的任意字节，越界值必须回落到默认。
    storage[ViewerOptions::DOUBLE_CLICK_INDEX] = 999u;
    storage[ViewerOptions::OPEN_MODE_INDEX] = 999u;
    storage[ViewerOptions::EDGE_ARROWS_INDEX] = 7u;
    ViewerOptions::initialize(storage.data(), storage.size());
    passOrFail("out-of-range viewer options fall back to defaults",
        ViewerOptions::doubleClickAction(storage.data()) ==
            ViewerOptions::DoubleClickAction::ToggleFullscreen &&
        ViewerOptions::openMode(storage.data()) ==
            ViewerOptions::OpenMode::ImmersivePreview &&
        !ViewerOptions::edgeArrowsEnabled(storage.data()));

    std::array<uint32_t, 777> legacy{};
    ViewerOptions::initialize(legacy.data(), legacy.size());
    passOrFail("a settings file predating viewer options is seeded rather than left blank",
        legacy[ViewerOptions::VERSION_INDEX] == ViewerOptions::STORAGE_VERSION &&
        ViewerOptions::openMode(legacy.data()) == ViewerOptions::OpenMode::ImmersivePreview);

    passOrFail("dragging moves the window only when the image cannot pan",
        ViewerOptions::dragShouldMoveWindow(true, false, false) &&
        !ViewerOptions::dragShouldMoveWindow(true, false, true) &&
        !ViewerOptions::dragShouldMoveWindow(true, true, false) &&
        !ViewerOptions::dragShouldMoveWindow(false, false, false));

    passOrFail("only the immersive preview mode opens without a window frame",
        ViewerOptions::opensImmersive(ViewerOptions::OpenMode::ImmersivePreview) &&
        !ViewerOptions::opensImmersive(ViewerOptions::OpenMode::FitImage) &&
        !ViewerOptions::opensImmersive(ViewerOptions::OpenMode::RememberLastSize));
}

void expectInitialWindowLayout() {
    const auto fitted = InitialWindowLayout::calculateFitImage(640, 480, 1920, 1080);
    passOrFail("the fit-image window matches the picture one to one when it fits",
        fitted.scale == 1.0 && fitted.clientWidth == 640 && fitted.clientHeight == 480);

    const auto fittedLarge = InitialWindowLayout::calculateFitImage(3840, 2160, 1920, 1080);
    passOrFail("an oversized picture shrinks to the work-area cap while keeping its ratio",
        fittedLarge.clientWidth == 1728 && fittedLarge.clientHeight == 972 &&
        fittedLarge.scale < 1.0);

    const auto fittedTall = InitialWindowLayout::calculateFitImage(400, 4000, 1920, 1080);
    passOrFail("a very tall picture is capped by height and stays proportional",
        fittedTall.clientHeight == 972 &&
        fittedTall.clientWidth == std::lround(400.0 * 972.0 / 4000.0));

    const auto small = InitialWindowLayout::calculate(640, 480, 1920, 1080);
    passOrFail("the hidden startup seed keeps small source dimensions intact",
        small.scale == 1.0 && small.renderedImageWidth == 640 && small.renderedImageHeight == 480 &&
        small.clientWidth == 1296 && small.clientHeight == 972);

    const auto tiny = InitialWindowLayout::calculate(100, 50, 1920, 1080);
    passOrFail("the hidden startup seed keeps tiny source dimensions intact",
        tiny.scale == 1.0 && tiny.renderedImageWidth == 100 && tiny.renderedImageHeight == 50 &&
        tiny.clientWidth == 1296 && tiny.clientHeight == 972);

    const auto large = InitialWindowLayout::calculate(3000, 2000, 1920, 1080);
    passOrFail("the hidden startup seed uses the fallback 4:3 ratio",
        std::abs(large.scale - 1296.0 / 3000.0) < 0.000001 &&
        large.renderedImageWidth == 1296 && large.renderedImageHeight == 864 &&
        large.clientWidth == 1296 && large.clientHeight == 972);

    const auto portrait = InitialWindowLayout::calculate(2000, 3000, 1920, 1080);
    passOrFail("the hidden startup seed can fit a portrait image",
        std::abs(portrait.scale - 972.0 / 3000.0) < 0.000001 &&
        portrait.renderedImageWidth == 648 && portrait.renderedImageHeight == 972 &&
        portrait.clientWidth == 1296 && portrait.clientHeight == 972);

    const auto square = InitialWindowLayout::calculate(2000, 2000, 1920, 1080);
    passOrFail("the hidden startup seed can fit a square image",
        square.renderedImageWidth == 972 && square.renderedImageHeight == 972 &&
        square.clientWidth == 1296 && square.clientHeight == 972);

    const auto portraitMonitor = InitialWindowLayout::calculate(1000, 1000, 1080, 1920);
    passOrFail("the hidden seed caps its 4:3 ratio on a portrait monitor",
        portraitMonitor.clientWidth == 972 && portraitMonitor.clientHeight == 729 &&
        portraitMonitor.renderedImageWidth == 729 && portraitMonitor.renderedImageHeight == 729);
}

void expectPresentationLayout() {
    const auto small = PresentationLayout::calculate(671, 477, 1920, 1020, 120);
    passOrFail("Picasa-sized small images use logical 100 percent at monitor DPI",
        std::abs(small.scale - 1.25) < 0.000001 &&
        small.renderedWidth == 839 && small.renderedHeight == 596 &&
        small.imageLeft == 540 && small.imageTop == 192 &&
        small.bottomReservedPixels == 40 && !small.portrait);

    const auto wide = PresentationLayout::calculate(1514, 857, 1920, 1020, 120);
    passOrFail("large landscape images fit the measured Picasa preview height",
        wide.renderedWidth == 1486 && wide.renderedHeight == 841 &&
        wide.imageLeft == 217 && wide.imageTop == 69 &&
        !wide.portrait && !wide.longLandscape && !wide.extendsBelowViewport);

    const auto exactLandscapeSixteenNine = PresentationLayout::calculate(1600, 900, 1920, 1020, 120);
    const auto panoramic = PresentationLayout::calculate(4000, 1000, 1920, 1020, 120);
    passOrFail("only landscapes wider than 16 by 9 use readable centered panorama mode",
        !exactLandscapeSixteenNine.longLandscape &&
        exactLandscapeSixteenNine.renderedWidth == 1495 &&
        exactLandscapeSixteenNine.renderedHeight == 841 &&
        panoramic.longLandscape && panoramic.renderedWidth == 3364 &&
        panoramic.renderedHeight == 841 && panoramic.imageLeft == -722 &&
        panoramic.initialSlideX == 0);

    const auto tall = PresentationLayout::calculate(723, 1130, 1920, 1020, 120);
    passOrFail("ordinary portrait photos up to 16 by 9 open fully visible",
        std::abs(tall.scale - 980.0 / 1130.0) < 0.000001 &&
        tall.renderedWidth == 627 && tall.renderedHeight == 980 &&
        tall.imageLeft == 646 && tall.imageTop == 0 &&
        tall.initialSlideY == -20 && tall.portrait && !tall.longPortrait &&
        !tall.extendsBelowViewport);

    const auto suppliedPortrait = PresentationLayout::calculate(3060, 4080, 1920, 1020, 120);
    passOrFail("the supplied 3 by 4 portrait photo opens fully visible",
        suppliedPortrait.renderedWidth == 735 && suppliedPortrait.renderedHeight == 980 &&
        suppliedPortrait.imageTop == 0 && !suppliedPortrait.longPortrait &&
        !suppliedPortrait.extendsBelowViewport);

    const auto exactSixteenNine = PresentationLayout::calculate(900, 1600, 1920, 1020, 120);
    const auto longPortrait = PresentationLayout::calculate(900, 1601, 1920, 1020, 120);
    passOrFail("only portraits taller than 16 by 9 use readable top-aligned long-image mode",
        !exactSixteenNine.longPortrait && exactSixteenNine.renderedHeight == 980 &&
        longPortrait.longPortrait && longPortrait.renderedWidth == 1125 &&
        longPortrait.renderedHeight == 2001 && longPortrait.imageTop == 0 &&
        longPortrait.extendsBelowViewport);

    const auto veryWideLongPortrait = PresentationLayout::calculate(2000, 4001, 1920, 1020, 120);
    passOrFail("long portraits shrink only when their width exceeds the preview limit",
        std::abs(veryWideLongPortrait.scale - 1728.0 / 2000.0) < 0.000001 &&
        veryWideLongPortrait.renderedWidth == 1728 &&
        veryWideLongPortrait.renderedHeight == 3457 &&
        veryWideLongPortrait.longPortrait);

    const auto smallWindow = PresentationLayout::calculateWindowed(small, 1920, 1020);
    const auto wideWindow = PresentationLayout::calculateWindowed(wide, 1920, 1020);
    const auto tallWindow = PresentationLayout::calculateWindowed(tall, 1920, 1020);
    passOrFail("leaving presentation mode wraps the window client around the displayed image",
        smallWindow.clientWidth == 839 && smallWindow.clientHeight == 596 &&
        wideWindow.clientWidth == 1486 && wideWindow.clientHeight == 841);
    passOrFail("a portrait returning to the framed window remains top aligned when capped",
        tallWindow.clientWidth == 627 && tallWindow.clientHeight == 918 &&
        tallWindow.initialSlideY == 31);
    passOrFail("the current image layout determines the next framed window",
        tallWindow.clientWidth != smallWindow.clientWidth &&
        tallWindow.clientHeight != smallWindow.clientHeight &&
        tallWindow.clientWidth <= 1920 * PresentationLayout::WINDOWED_MAX_PERCENT / 100 &&
        tallWindow.clientHeight <= 1020 * PresentationLayout::WINDOWED_MAX_PERCENT / 100);
}

void expectMonitorPlacement() {
    const std::vector<MonitorPlacement::Monitor> monitors{
        { L"\\\\.\\DISPLAY1", { 0, 0, 1920, 1040 }, true },
        { L"\\\\.\\DISPLAY2", { -1280, 0, 0, 1024 }, false },
    };

    const auto remembered = MonitorPlacement::select(monitors, true, L"\\\\.\\display2");
    passOrFail("remembered monitor selection is device-name based and case-insensitive",
        remembered.index == 1 && remembered.matchedRememberedMonitor);

    const auto disabled = MonitorPlacement::select(monitors, false, L"\\\\.\\DISPLAY2");
    const auto disconnected = MonitorPlacement::select(monitors, true, L"\\\\.\\DISPLAY9");
    passOrFail("disabled or disconnected monitor memory falls back to the primary monitor",
        disabled.index == 0 && !disabled.matchedRememberedMonitor &&
        disconnected.index == 0 && !disconnected.matchedRememberedMonitor);

    const auto cursorOpen = MonitorPlacement::selectForImageOpen(
        monitors, 1, true, L"\\\\.\\DISPLAY1");
    const auto missingCursor = MonitorPlacement::selectForImageOpen(
        monitors, MonitorPlacement::NO_MONITOR, true, L"\\\\.\\DISPLAY2");
    passOrFail("image launch prefers the monitor under the mouse cursor",
        cursorOpen.index == 1 && !cursorOpen.matchedRememberedMonitor &&
        missingCursor.index == 1 && missingCursor.matchedRememberedMonitor);

    const MonitorPlacement::Rect secondaryWindow{ -1180, 100, -380, 700 };
    const auto relative = MonitorPlacement::toRelative(secondaryWindow, monitors[1].workArea);
    const auto restored = MonitorPlacement::restore(relative, monitors[1].workArea);
    passOrFail("window coordinates round-trip on a monitor left of the primary display",
        relative.left == 100 && relative.top == 100 && relative.right == 900 && relative.bottom == 700 &&
        restored.left == secondaryWindow.left && restored.top == secondaryWindow.top &&
        restored.right == secondaryWindow.right && restored.bottom == secondaryWindow.bottom);

    const auto clamped = MonitorPlacement::restore({ 1800, 900, 3000, 1800 }, monitors[0].workArea);
    passOrFail("restored windows are clamped inside the selected monitor work area",
        clamped.left == 720 && clamped.top == 140 && clamped.right == 1920 && clamped.bottom == 1040);
}

void expectToolbarIcons(const std::vector<std::string>& paths) {
    bool allValid = paths.size() == 19;   // 新增「适应图片」
    for (const auto& path : paths) {
        const auto source = readFile(path);
        const auto renderer = SvgRenderer::create(source);
        if (!renderer) {
            allValid = false;
            continue;
        }
        const auto bitmap = renderer->renderToBitmap(OverlayLayout::BASE_ICON_SIZE, OverlayLayout::BASE_ICON_SIZE);
        size_t visiblePixels = 0;
        for (size_t offset = 0; offset + 3 < bitmap.bgra.size(); offset += 4) {
            visiblePixels += bitmap.bgra[offset + 3] > 0;
        }
        allValid = allValid && bitmap.width == OverlayLayout::BASE_ICON_SIZE &&
            bitmap.height == OverlayLayout::BASE_ICON_SIZE && visiblePixels > 8;
    }
    passOrFail("all reference viewer overlay SVG resources render correctly", allValid);
}

uint16_t readLittleEndian16(const std::vector<uint8_t>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size())
        return 0;
    return static_cast<uint16_t>(bytes[offset] | bytes[offset + 1] << 8);
}

uint32_t readLittleEndian32(const std::vector<uint8_t>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size())
        return 0;
    return static_cast<uint32_t>(bytes[offset]) |
        static_cast<uint32_t>(bytes[offset + 1]) << 8 |
        static_cast<uint32_t>(bytes[offset + 2]) << 16 |
        static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

void expectApplicationIcons(const std::vector<std::string>& paths) {
    constexpr std::array expectedSizes{ 16, 24, 32, 36, 48, 64, 96, 128, 256 };
    bool allValid = paths.size() == 3;
    std::vector<uint8_t> reference;
    for (const auto& path : paths) {
        const auto bytes = readFile(path);
        if (reference.empty())
            reference = bytes;
        allValid = allValid && bytes == reference && bytes.size() > 6 + expectedSizes.size() * 16 &&
            readLittleEndian16(bytes, 0) == 0 && readLittleEndian16(bytes, 2) == 1 &&
            readLittleEndian16(bytes, 4) == expectedSizes.size();
        for (std::size_t index = 0; allValid && index < expectedSizes.size(); ++index) {
            const std::size_t entry = 6 + index * 16;
            const int width = bytes[entry] == 0 ? 256 : bytes[entry];
            const int height = bytes[entry + 1] == 0 ? 256 : bytes[entry + 1];
            const uint32_t dataSize = readLittleEndian32(bytes, entry + 8);
            const uint32_t dataOffset = readLittleEndian32(bytes, entry + 12);
            allValid = allValid && width == expectedSizes[index] && height == expectedSizes[index] &&
                dataSize > 0 && dataOffset >= 6 + expectedSizes.size() * 16 &&
                static_cast<std::size_t>(dataOffset) + dataSize <= bytes.size();
        }
    }
    passOrFail("application icon resources share all nine transparent Windows sizes", allValid);
}

void expectRotationPersistence() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto databasePath = std::filesystem::temp_directory_path() /
        (L"YeImageViewerRotationStore-" + std::to_wstring(unique) + L".db");
    const std::wstring originalPath = L"C:\\Images\\Example.PNG";
    const std::wstring samePathDifferentCase = L"c:\\images\\example.png";

    RotationStore first(databasePath);
    first.set(originalPath, 1);
    const bool firstSave = first.save();

    RotationStore afterRestart(databasePath);
    const bool firstReload = afterRestart.load();
    passOrFail("rotation survives a store restart and path matching is case-insensitive",
        firstSave && firstReload && afterRestart.get(samePathDifferentCase) == 1);

    afterRestart.set(samePathDifferentCase, 3);
    const bool secondSave = afterRestart.save();
    RotationStore afterSecondRestart(databasePath);
    passOrFail("updated rotation survives a second restart",
        secondSave && afterSecondRestart.load() && afterSecondRestart.get(originalPath) == 3);

    afterSecondRestart.set(originalPath, 0);
    const bool resetSave = afterSecondRestart.save();
    RotationStore afterReset(databasePath);
    passOrFail("returning to the original orientation removes the persisted override",
        resetSave && afterReset.load() && afterReset.get(originalPath) == 0 && afterReset.size() == 0);

    // 旋转记录搬进设置文件的文本区之后，「坏文件」的样子变了：不再是魔数对不上，
    // 而是文本区里都是读不懂的行。要求不变——读不出来就当没有，绝不能返回旧值。
    afterReset.set(originalPath, 2);
    const bool beforeCorruption = afterReset.save();
    {
        std::ofstream corrupt(databasePath, std::ios::binary | std::ios::trunc);
        corrupt << "not a rotation database";
    }
    RotationStore corrupted(databasePath);
    passOrFail("a truncated settings file yields no rotations instead of stale data",
        beforeCorruption && corrupted.load() && corrupted.get(originalPath) == 0 &&
        corrupted.size() == 0);

    {
        // 头还在、文本区是垃圾：逐行跳过，同样一条都不认
        std::vector<char> head(static_cast<std::size_t>(ConfigFile::HEAD_SIZE), 'Z');
        std::ofstream garbage(databasePath, std::ios::binary | std::ios::trunc);
        garbage.write(head.data(), ConfigFile::HEAD_SIZE);
        garbage << "Rot0=" << "\r\n"
                << "Rot1=9|whatever" << "\r\n"
                << "a line without any equals sign" << "\r\n"

                << "Rot2=1|" << "\r\n";
    }
    RotationStore garbled(databasePath);
    passOrFail("unreadable rotation lines are skipped rather than trusted",
        garbled.load() && garbled.size() == 0 && garbled.get(originalPath) == 0);

    std::error_code ignored;
    std::filesystem::remove(databasePath, ignored);
}

void expectRenamePolicy() {
    const std::filesystem::path source = LR"(D:\images\holiday.photo.png)";
    const auto target = RenamePolicy::buildTargetPath(source, L"summer trip");
    passOrFail("rename keeps the current image extension",
        target == std::filesystem::path(LR"(D:\images\summer trip.png)"));
    passOrFail("rename trims surrounding whitespace before validation",
        RenamePolicy::trim(L"  summer trip  ") == L"summer trip");
    passOrFail("rename accepts a normal Unicode file name",
        RenamePolicy::validate(L"夏日照片", L".png") == RenamePolicy::ValidationError::None);
    passOrFail("rename rejects an empty file name",
        RenamePolicy::validate(L"", L".png") == RenamePolicy::ValidationError::Empty);
    passOrFail("rename rejects Windows-invalid characters",
        RenamePolicy::validate(L"bad:name", L".png") == RenamePolicy::ValidationError::InvalidCharacter);
    passOrFail("rename rejects a trailing dot",
        RenamePolicy::validate(L"bad.", L".png") == RenamePolicy::ValidationError::TrailingDotOrSpace);
    passOrFail("rename rejects reserved Windows device names",
        RenamePolicy::validate(L"CON.notes", L".png") == RenamePolicy::ValidationError::ReservedName);
    passOrFail("rename rejects an overlong file component",
        RenamePolicy::validate(std::wstring(252, L'a'), L".png") == RenamePolicy::ValidationError::TooLong);

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto testDirectory = std::filesystem::temp_directory_path() /
        (L"YeImageViewerRename-" + std::to_wstring(unique));
    const auto original = testDirectory / L"original.png";
    const auto renamed = testDirectory / L"renamed-by-production.png";
    const auto occupied = testDirectory / L"occupied.png";
    std::error_code ignored;
    std::filesystem::create_directories(testDirectory, ignored);
    {
        std::ofstream fixture(original, std::ios::binary);
        fixture << "rename regression fixture";
    }
    const auto renamedResult = RenamePolicy::renameFile(original, L"renamed-by-production");
    passOrFail("production rename moves a real file and preserves its extension",
        renamedResult.error == RenamePolicy::OperationError::None &&
        renamedResult.target == renamed && std::filesystem::exists(renamed) &&
        !std::filesystem::exists(original));
    {
        std::ofstream fixture(occupied, std::ios::binary);
        fixture << "must not be overwritten";
    }
    const auto collisionResult = RenamePolicy::renameFile(renamed, L"occupied");
    passOrFail("production rename refuses to overwrite an existing file",
        collisionResult.error == RenamePolicy::OperationError::AlreadyExists &&
        std::filesystem::exists(renamed) && std::filesystem::file_size(occupied) == 23);
    std::filesystem::remove(renamed, ignored);
    std::filesystem::remove(occupied, ignored);
    std::filesystem::remove(testDirectory, ignored);
}

// 关闭图片已经变成快捷键动作（默认绑 Esc），这里只剩「Esc 没被快捷键占用时干什么」。
void expectEscapeBehavior() {
    passOrFail("an unbound Escape leaves presentation first",
        EscapeBehavior::resolve(true, true, true) == EscapeBehavior::Action::ExitPresentation);
    passOrFail("an unbound Escape exits fullscreen when not presenting",
        EscapeBehavior::resolve(false, true, true) == EscapeBehavior::Action::ExitFullScreen);
    passOrFail("an unbound Escape restores a maximized window",
        EscapeBehavior::resolve(false, false, true) == EscapeBehavior::Action::RestoreWindow);
    passOrFail("an unbound Escape does nothing in a normal window",
        EscapeBehavior::resolve(false, false, false) == EscapeBehavior::Action::Ignore);
}

void expectSettingLayout() {
    const auto hasCenter = [](const SettingLayout::Rect& rect, int contentHeight) {
        const int centerX = rect.x + rect.width / 2;
        const int centerY = rect.y + rect.height / 2;
        return SettingLayout::isInsidePage(rect, contentHeight) &&
            rect.x <= centerX && centerX < rect.x + rect.width &&
            rect.y <= centerY && centerY < rect.y + rect.height;
    };
    passOrFail("settings use the native Windows menu-sized font",
        SettingLayout::FONT_SIZE == 16 &&
        TextRenderingPolicy::LOGICAL_FONT_SIZE == SettingLayout::FONT_SIZE &&
        SettingLayout::ABOUT_TITLE_FONT_SIZE == SettingLayout::FONT_SIZE &&
        SettingLayout::FONT_SIZE * 2 <= SettingLayout::GENERAL_CHECK_BOXES.front().height);
    passOrFail("settings keep the fixed 620 by 620 client size on every tab",
        SettingLayout::CANVAS_WIDTH == 620 &&
        SettingLayout::CANVAS_HEIGHT == 620 &&
        SettingLayout::TAB_WIDTH * 4 == SettingLayout::CANVAS_WIDTH);
    passOrFail("settings controls remain separated inside the fixed canvas",
        SettingLayout::generalControlsAreSeparated());
    bool everySettingControlHasHitTarget = true;
    for (const auto& rect : SettingLayout::GENERAL_CHECK_BOXES)
        everySettingControlHasHitTarget &= hasCenter(rect, SettingLayout::GENERAL_CONTENT_HEIGHT);
    for (const auto& rect : SettingLayout::GENERAL_RADIOS)
        everySettingControlHasHitTarget &= hasCenter(rect, SettingLayout::GENERAL_CONTENT_HEIGHT);
    for (int index = 0; index < 10; ++index) {
        everySettingControlHasHitTarget &=
            hasCenter(SettingLayout::generalEditorName(index), SettingLayout::GENERAL_CONTENT_HEIGHT) &&
            hasCenter(SettingLayout::generalEditorPath(index), SettingLayout::GENERAL_CONTENT_HEIGHT) &&
            hasCenter(SettingLayout::generalEditorRemove(index), SettingLayout::GENERAL_CONTENT_HEIGHT);
    }
    everySettingControlHasHitTarget &= hasCenter(
        SettingLayout::generalEditorAdd(10), SettingLayout::GENERAL_CONTENT_HEIGHT);
    everySettingControlHasHitTarget &= hasCenter(SettingLayout::ASSOCIATION_SEARCH, 600);
    for (int index = 0; index < 4; ++index)
        everySettingControlHasHitTarget &= hasCenter(
            SettingLayout::associationButtonRect(index, 400), 500);
    for (int index = 0; index < 3; ++index)
        everySettingControlHasHitTarget &= hasCenter(
            SettingLayout::shortcutWheelRow(index), SettingLayout::SHORTCUT_CONTENT_HEIGHT);
    everySettingControlHasHitTarget &= hasCenter(
        SettingLayout::SHORTCUT_RESET_BUTTON, SettingLayout::SHORTCUT_CONTENT_HEIGHT);
    for (int index = 0; index < SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT; ++index)
        everySettingControlHasHitTarget &= hasCenter(
            SettingLayout::shortcutKeyboardRow(index), SettingLayout::SHORTCUT_CONTENT_HEIGHT);
    everySettingControlHasHitTarget &= hasCenter(
        SettingLayout::ABOUT_PROJECT_BUTTON, SettingLayout::ABOUT_CONTENT_HEIGHT) &&
        hasCenter(SettingLayout::ABOUT_UPSTREAM_BUTTON, SettingLayout::ABOUT_CONTENT_HEIGHT);
    passOrFail("every settings switch segment button and shortcut row has a tested hit target",
        everySettingControlHasHitTarget);
    bool everySettingControlRoutes = true;
    for (int index = 0; index < static_cast<int>(SettingLayout::GENERAL_CHECK_BOXES.size()); ++index) {
        const auto rect = SettingLayout::GENERAL_CHECK_BOXES[index];
        const auto command = SettingCommand::resolve(0, rect.x + rect.width / 2,
            SettingLayout::TAB_HEIGHT + rect.y + rect.height / 2, 0);
        everySettingControlRoutes &= command.kind == SettingCommand::Kind::GeneralToggle &&
            command.index == index;
    }
    // 这张表不是从 SettingCommand 抄来的，是从 Setting.h 的 generalTabRadioList
    // 数出来的：每组的 stringIDs 第一项是标签，剩下的才是选项。
    //   {20,21,22,23} {24,25,26,27} {28,30,31,86} {36,37,38}
    //   {57,58,59,60,83} {61,62,63,64,84} {65,66,67} {68,69,70}
    //   {73,66,67} {74,75,76,77,78} {79,80,81} {85,66,67}
    // 两边必须相等——先前这条测试直接照抄了 SettingCommand 那张表，于是
    // 「加繁體中文时漏改选项数」这种错两边一起错，测试照样绿。
    constexpr std::array<int, 12> radioOptions{ 3, 3, 3, 2, 4, 4, 2, 2, 2, 4, 2, 2 };
    static_assert(radioOptions.size() == SettingLayout::GENERAL_RADIOS.size());
    for (std::size_t index = 0; index < radioOptions.size(); ++index) {
        if (radioOptions[index] != SettingCommand::GENERAL_RADIO_OPTION_COUNTS[index]) {
            everySettingControlRoutes = false;
            std::cerr << "FAIL general radio row " << index << " has " << radioOptions[index]
                << " labelled options but SettingCommand counts "
                << SettingCommand::GENERAL_RADIO_OPTION_COUNTS[index] << '\n';
        }
    }
    for (int rowIndex = 0; rowIndex < static_cast<int>(SettingLayout::GENERAL_RADIOS.size()); ++rowIndex) {
        const auto row = SettingLayout::GENERAL_RADIOS[rowIndex];
        const int segmentX = row.x + 138;
        const int segmentWidth = (row.width - 138) / radioOptions[rowIndex];
        for (int option = 0; option < radioOptions[rowIndex]; ++option) {
            const auto command = SettingCommand::resolve(0,
                segmentX + option * segmentWidth + segmentWidth / 2,
                SettingLayout::TAB_HEIGHT + row.y + row.height / 2, 0);
            everySettingControlRoutes &= command.kind == SettingCommand::Kind::GeneralRadioOption &&
                command.index == rowIndex && command.option == option;
        }
    }
    {
        const int generalMaxScroll = SettingLayout::maxScrollOffset(
            SettingLayout::generalContentHeight(10));
        for (int index = 0; index < 10; ++index) {
            constexpr std::array<SettingCommand::Kind, 3> editorKinds{
                SettingCommand::Kind::GeneralEditorRename,
                SettingCommand::Kind::GeneralEditorPath,
                SettingCommand::Kind::GeneralEditorRemove };
            const std::array<SettingLayout::Rect, 3> editorRects{
                SettingLayout::generalEditorName(index),
                SettingLayout::generalEditorPath(index),
                SettingLayout::generalEditorRemove(index) };
            for (int part = 0; part < 3; ++part) {
                const auto rect = editorRects[part];
                const int generalScroll = std::clamp(
                    rect.y + rect.height / 2 -
                        SettingLayout::CONTENT_VIEW_HEIGHT / 2,
                    0, generalMaxScroll);
                const auto command = SettingCommand::resolve(0,
                    rect.x + rect.width / 2,
                    SettingLayout::TAB_HEIGHT + rect.y + rect.height / 2 - generalScroll,
                    generalScroll, 0, 0, 10);
                everySettingControlRoutes &= command.kind == editorKinds[part] &&
                    command.index == index;
            }
        }
        const auto add = SettingLayout::generalEditorAdd(10);
        const int generalScroll = generalMaxScroll;
        everySettingControlRoutes &= SettingCommand::resolve(0,
            add.x + add.width / 2,
            SettingLayout::TAB_HEIGHT + add.y + add.height / 2 - generalScroll,
            generalScroll, 0, 0, 10).kind ==
            SettingCommand::Kind::GeneralEditorAdd;
    }
    constexpr int associationCount = 24;
    constexpr int associationButtonsY = 185;
    everySettingControlRoutes &= SettingCommand::resolve(1,
        SettingLayout::ASSOCIATION_SEARCH.x + 10,
        SettingLayout::TAB_HEIGHT + SettingLayout::ASSOCIATION_SEARCH.y + 10,
        0, associationCount, associationButtonsY).kind == SettingCommand::Kind::AssociationSearch;
    for (int index = 0; index < associationCount; ++index) {
        const int column = index % SettingLayout::ASSOCIATION_GRID_COLUMNS;
        const int row = index / SettingLayout::ASSOCIATION_GRID_COLUMNS;
        const auto command = SettingCommand::resolve(1,
            SettingLayout::ASSOCIATION_GRID_X + column *
                (SettingLayout::ASSOCIATION_TAG_WIDTH + SettingLayout::ASSOCIATION_TAG_GAP_X) + 2,
            SettingLayout::TAB_HEIGHT + SettingLayout::ASSOCIATION_GRID_Y + row *
                (SettingLayout::ASSOCIATION_TAG_HEIGHT + SettingLayout::ASSOCIATION_TAG_GAP_Y) + 2,
            0, associationCount, associationButtonsY);
        everySettingControlRoutes &= command.kind == SettingCommand::Kind::AssociationExtension &&
            command.index == index;
    }
    constexpr std::array<SettingCommand::Kind, 4> associationKinds{
        SettingCommand::Kind::AssociationDefaults, SettingCommand::Kind::AssociationAll,
        SettingCommand::Kind::AssociationNone, SettingCommand::Kind::AssociationApply };
    for (int index = 0; index < 4; ++index) {
        const auto rect = SettingLayout::associationButtonRect(index, associationButtonsY);
        everySettingControlRoutes &= SettingCommand::resolve(1, rect.x + rect.width / 2,
            SettingLayout::TAB_HEIGHT + rect.y + rect.height / 2, 0,
            associationCount, associationButtonsY).kind == associationKinds[index];
    }
    for (int index = 0; index < 3; ++index) {
        const auto row = SettingLayout::shortcutWheelRow(index);
        const auto command = SettingCommand::resolve(2, row.x + row.width / 2,
            SettingLayout::TAB_HEIGHT + row.y + row.height / 2, 0);
        everySettingControlRoutes &= command.kind == SettingCommand::Kind::ShortcutWheel &&
            command.index == index;
    }
    {
        const auto reset = SettingLayout::SHORTCUT_RESET_BUTTON;
        everySettingControlRoutes &= SettingCommand::resolve(2,
            reset.x + reset.width / 2,
            SettingLayout::TAB_HEIGHT + reset.y + reset.height / 2, 0).kind ==
            SettingCommand::Kind::ShortcutReset;
    }
    for (int index = 0; index < SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT; ++index) {
        const auto row = SettingLayout::shortcutKeyboardRow(index);
        const auto command = SettingCommand::resolve(2, row.x + row.width / 2,
            SettingLayout::TAB_HEIGHT + row.y + row.height / 2, 0);
        everySettingControlRoutes &= command.kind == SettingCommand::Kind::ShortcutBinding &&
            command.index == index;
    }
    for (int tab = 0; tab < 4; ++tab) {
        const auto command = SettingCommand::resolve(0,
            tab * SettingLayout::TAB_WIDTH + SettingLayout::TAB_WIDTH / 2,
            SettingLayout::TAB_HEIGHT / 2, 0);
        everySettingControlRoutes &= command.kind == SettingCommand::Kind::Tab &&
            command.index == tab;
    }
    everySettingControlRoutes &= SettingCommand::resolve(3,
        SettingLayout::ABOUT_PROJECT_BUTTON.x + SettingLayout::ABOUT_PROJECT_BUTTON.width / 2,
        SettingLayout::TAB_HEIGHT + SettingLayout::ABOUT_PROJECT_BUTTON.y +
            SettingLayout::ABOUT_PROJECT_BUTTON.height / 2, 0).kind ==
            SettingCommand::Kind::AboutProject;
    everySettingControlRoutes &= SettingCommand::resolve(3,
        SettingLayout::ABOUT_UPSTREAM_BUTTON.x + SettingLayout::ABOUT_UPSTREAM_BUTTON.width / 2,
        SettingLayout::TAB_HEIGHT + SettingLayout::ABOUT_UPSTREAM_BUTTON.y +
            SettingLayout::ABOUT_UPSTREAM_BUTTON.height / 2, 0).kind ==
            SettingCommand::Kind::AboutUpstream;
    passOrFail("every Settings tab switch segment association shortcut and About button routes to its production command",
        everySettingControlRoutes);
    passOrFail("general settings pair switches and keep segmented rows full width",
        SettingLayout::GENERAL_CHECK_BOXES[0].y == SettingLayout::GENERAL_CHECK_BOXES[1].y &&
        SettingLayout::GENERAL_CHECK_BOXES[0].x < SettingLayout::GENERAL_CHECK_BOXES[1].x &&
        SettingLayout::GENERAL_RADIOS[0].x == SettingLayout::GENERAL_RADIOS[1].x &&
        SettingLayout::GENERAL_RADIOS[0].width == SettingLayout::GENERAL_RADIOS[1].width &&
        SettingLayout::GENERAL_RADIOS[0].y < SettingLayout::GENERAL_RADIOS[1].y);
    passOrFail("remember-monitor and animation controls have a visible vertical gap",
        SettingLayout::GENERAL_CHECK_BOXES.back().y +
            SettingLayout::GENERAL_CHECK_BOXES.back().height <
            SettingLayout::GENERAL_RADIOS.front().y);
    passOrFail("shortcut settings expose every configurable keyboard action",
        SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT ==
            static_cast<int>(ShortcutConfig::Action::Count) &&
        SettingLayout::shortcutKeyboardRow(0).y <
            SettingLayout::shortcutKeyboardRow(SettingLayout::SHORTCUT_KEYBOARD_ROW_COUNT - 1).y &&
        SettingLayout::shortcutItemsAreSeparated());
    passOrFail("only overflowing settings pages enable a compact scrollbar",
        SettingLayout::maxScrollOffset(SettingLayout::GENERAL_CONTENT_HEIGHT) > 0 &&
        SettingLayout::maxScrollOffset(SettingLayout::ABOUT_CONTENT_HEIGHT) == 0 &&
        SettingLayout::maxScrollOffset(SettingLayout::SHORTCUT_CONTENT_HEIGHT) > 0 &&
        SettingLayout::scrollbarThumbHeight(SettingLayout::SHORTCUT_CONTENT_HEIGHT) >= 32 &&
        SettingLayout::scrollbarThumbHeight(SettingLayout::SHORTCUT_CONTENT_HEIGHT) <
            SettingLayout::CONTENT_VIEW_HEIGHT);
    passOrFail("about build details follow the author and repository buttons stay at the bottom",
        SettingLayout::aboutLayoutIsOrdered() &&
        SettingLayout::ABOUT_HERO_CARD.y + SettingLayout::ABOUT_HERO_CARD.height <
            SettingLayout::ABOUT_PROJECT_BUTTON.y &&
        SettingLayout::ABOUT_PROJECT_BUTTON.y +
            SettingLayout::ABOUT_PROJECT_BUTTON.height ==
            SettingLayout::ABOUT_CONTENT_HEIGHT - SettingLayout::PAGE_PADDING);
    passOrFail("settings scroll offsets are clamped to the content bounds",
        SettingLayout::clampScrollOffset(SettingLayout::SHORTCUT_CONTENT_HEIGHT, -10) == 0 &&
        SettingLayout::clampScrollOffset(SettingLayout::SHORTCUT_CONTENT_HEIGHT, 10000) ==
            SettingLayout::maxScrollOffset(SettingLayout::SHORTCUT_CONTENT_HEIGHT));
}

void expectExternalEditorConfig() {
    std::vector<ExternalEditorConfig::Entry> editors;
    const auto paintPath = L"C:\\Windows\\System32\\mspaint.exe";
    passOrFail("external editor defaults to the executable name when no custom name is supplied",
        ExternalEditorConfig::defaultName(paintPath) == L"mspaint" &&
        ExternalEditorConfig::resolvedName(L"   ", paintPath) == L"mspaint" &&
        ExternalEditorConfig::add(editors, { L"画图", paintPath }));
    passOrFail("external editor menu labels use a space without parentheses",
        ExternalEditorConfig::menuLabel(editors.front(), UiLanguage::SIMPLIFIED) == L"在 画图" &&
        ExternalEditorConfig::menuLabel(editors.front(), UiLanguage::ENGLISH) == L"Open in 画图" &&
        ExternalEditorConfig::menuLabel(editors.front(), UiLanguage::SIMPLIFIED).find(L'（') ==
            std::wstring::npos &&
        ExternalEditorConfig::menuLabel(editors.front(), UiLanguage::SIMPLIFIED).find(L'(') ==
            std::wstring::npos);

    for (int index = 1; index < 10; ++index) {
        ExternalEditorConfig::add(editors,
            { L"编辑器 " + std::to_wstring(index),
                L"C:\\Editors\\editor" + std::to_wstring(index) + L".exe" });
    }
    passOrFail("external editor settings accept ten user-named applications and reject an eleventh",
        editors.size() == ExternalEditorConfig::MAX_EDITORS &&
        !ExternalEditorConfig::add(editors,
            { L"Too many", L"C:\\Editors\\extra.exe" }));

    passOrFail("selecting an existing editor path updates its custom display name without duplication",
        ExternalEditorConfig::add(editors, { L"日常画图", paintPath }) &&
        editors.size() == ExternalEditorConfig::MAX_EDITORS &&
        editors.front().name == L"日常画图");

    const auto tempDirectory = std::filesystem::temp_directory_path() /
        (L"YeImageViewer-ExternalEditors-" + std::to_wstring(GetCurrentProcessId()));
    const auto configFile = tempDirectory / L"YeImageViewer.db";
    std::error_code ignored;
    std::filesystem::create_directories(tempDirectory, ignored);
    const bool saved = ExternalEditorConfig::save(configFile.wstring(), editors);
    const auto loaded = ExternalEditorConfig::load(configFile.wstring());
    passOrFail("external editor names and Unicode executable paths persist in the settings file",
        saved && loaded == editors &&
        std::filesystem::file_size(configFile, ignored) > ConfigFile::HEAD_SIZE);

    // 三份配置挤在同一个文件里，谁保存都不能把别人的行抹掉——
    // 这正是 5 个文件并成 3 个之后最容易翻车的地方。
    FileTargetConfig::Model targets;
    FileTargetConfig::addTarget(targets, L"D:\\相册\\精选");
    FileTargetConfig::addTarget(targets, L"E:\\backup");
    const bool targetsSaved = FileTargetConfig::save(configFile.wstring(), targets);

    RotationStore rotations;
    rotations.setStoragePath(configFile);
    rotations.set(L"D:\\照片\\a 1.png", 1);
    rotations.set(L"D:\\照片\\b.png", 3);
    const bool rotationsSaved = rotations.save();

    // 再存一遍编辑器：它必须把目标和旋转那些行原样带回去
    const bool editorsResaved = ExternalEditorConfig::save(configFile.wstring(), editors);

    RotationStore reloaded;
    reloaded.setStoragePath(configFile);
    const bool rotationsReloaded = reloaded.load();
    const auto targetsReloaded = FileTargetConfig::load(configFile.wstring());
    const auto editorsReloaded = ExternalEditorConfig::load(configFile.wstring());

    passOrFail("editors, copy targets, and rotations share one settings file without clobbering each other",
        targetsSaved && rotationsSaved && editorsResaved && rotationsReloaded &&
        editorsReloaded == editors &&
        targetsReloaded.targets.size() == 2 &&
        targetsReloaded.targets.front() == L"D:\\相册\\精选" &&
        reloaded.get(L"D:\\照片\\a 1.png") == 1 &&
        reloaded.get(L"D:\\照片\\b.png") == 3);

    // 头 4096 字节是设置结构体，文本区只能追加在后面：
    // 写文本不能把已有的设置抹掉，否则用户一配置编辑器，窗口位置和主题就全丢了。
    std::vector<char> head(static_cast<std::size_t>(ConfigFile::HEAD_SIZE), '\0');
    {
        std::ifstream probe(configFile, std::ios::binary);
        probe.read(head.data(), ConfigFile::HEAD_SIZE);
    }
    for (std::size_t index = 0; index < head.size(); ++index)
        head[index] = static_cast<char>('A' + (index % 26));
    {
        std::fstream writer(configFile, std::ios::binary | std::ios::in | std::ios::out);
        writer.write(head.data(), ConfigFile::HEAD_SIZE);
    }
    const bool editorsAfterHead = ExternalEditorConfig::save(configFile.wstring(), editors);
    std::vector<char> headAfter(static_cast<std::size_t>(ConfigFile::HEAD_SIZE), '\0');
    {
        std::ifstream probe(configFile, std::ios::binary);
        probe.read(headAfter.data(), ConfigFile::HEAD_SIZE);
    }
    passOrFail("writing the text section leaves the fixed-size settings block untouched",
        editorsAfterHead && headAfter == head &&
        ExternalEditorConfig::load(configFile.wstring()) == editors);

    std::filesystem::remove(configFile, ignored);
    std::filesystem::remove(tempDirectory, ignored);

    passOrFail("external editor image argument preserves spaces and Unicode with quotes",
        ExternalEditorConfig::quoteImageArgument(L"D:\\Pictures\\测试 图片.png") ==
            L"\"D:\\Pictures\\测试 图片.png\"");
}

void expectHomeScreenLayout() {
    passOrFail("startup is a code-laid-out functional page without a raster hero image",
        !HomeScreenLayout::USES_LEGACY_JARKVIEWER_DIAGRAM &&
        !HomeScreenLayout::USES_RASTER_HERO_IMAGE &&
        HomeScreenLayout::WIDTH == 500 && HomeScreenLayout::HEIGHT == 350 &&
        HomeScreenLayout::hasSeparatedGuideCards());
    passOrFail("startup primary open button and footer stay inside the canvas",
        HomeScreenLayout::isInside(HomeScreenLayout::OPEN_BUTTON) &&
        HomeScreenLayout::isInside(HomeScreenLayout::FOOTER) &&
        HomeScreenLayout::OPEN_BUTTON.y < HomeScreenLayout::GUIDE_CARDS.front().y &&
        HomeScreenLayout::GUIDE_CARDS.front().y +
            HomeScreenLayout::GUIDE_CARDS.front().height < HomeScreenLayout::FOOTER.y);
    passOrFail("startup text is generated at native monitor DPI without post-raster enlargement",
        HomeScreenLayout::nativeCanvas(96).width == 500 &&
        HomeScreenLayout::nativeCanvas(96).height == 350 &&
        HomeScreenLayout::nativeCanvas(144).width == 750 &&
        HomeScreenLayout::nativeCanvas(144).height == 525);
    const int buttonCenterX = HomeScreenLayout::OPEN_BUTTON.x +
        HomeScreenLayout::OPEN_BUTTON.width / 2;
    const int buttonCenterY = HomeScreenLayout::OPEN_BUTTON.y +
        HomeScreenLayout::OPEN_BUTTON.height / 2;
    passOrFail("startup open button hit target follows native and DPI-scaled rendering",
        HomeScreenLayout::hitOpenButton(
            { 0, 0, HomeScreenLayout::WIDTH, HomeScreenLayout::HEIGHT },
            buttonCenterX, buttonCenterY) &&
        HomeScreenLayout::hitOpenButton(
            { 50, 75, HomeScreenLayout::WIDTH * 2, HomeScreenLayout::HEIGHT * 2 },
            50 + buttonCenterX * 2, 75 + buttonCenterY * 2) &&
        !HomeScreenLayout::hitOpenButton(
            { 50, 75, HomeScreenLayout::WIDTH * 2, HomeScreenLayout::HEIGHT * 2 },
            50 + 8, 75 + 8));
}

void expectTextRendering() {
    passOrFail("logical text size scales continuously with monitor DPI",
        TextRenderingPolicy::scaledPixelSize(18, 96) == 18 &&
        TextRenderingPolicy::scaledPixelSize(18, 120) == 23 &&
        TextRenderingPolicy::scaledPixelSize(18, 144) == 27 &&
        TextRenderingPolicy::scaledPixelSize(18, 192) == 36);
    passOrFail("non-adaptive interface text uses native Windows ClearType",
        TextRenderingPolicy::usesNativeClearType(false, true) &&
        !TextRenderingPolicy::usesNativeClearType(true, true) &&
        !TextRenderingPolicy::usesNativeClearType(false, false));
    passOrFail("glyph antialiasing keeps endpoints and strengthens intermediate coverage",
        TextRenderingPolicy::enhanceCoverage(0) == 0 &&
        TextRenderingPolicy::enhanceCoverage(64) > 64 &&
        TextRenderingPolicy::enhanceCoverage(128) > 128 &&
        TextRenderingPolicy::enhanceCoverage(255) == 255);
    const auto systemFont = SystemFont::findPreferredPath();
    passOrFail("Windows system font replaces the embedded fifteen megabyte font",
        SystemFont::PREFERRED_FILE_NAMES.front() == L"msyh.ttc" &&
        systemFont.has_value() &&
        std::filesystem::is_regular_file(*systemFont));
}

void expectImageInfoPresentation() {
    constexpr std::string_view rawInfo =
        "路径: C:\\Pictures\\这是一个非常长而且必须完整换行显示的图片文件名_sample.png\n"
        "大小: 103.0 KiB\n"
        "分辨率: 671x477\n"
        "原始日期时间: 2026-08-28 10:20:30\n"
        "型号: Sample Camera\n"
        "制造商: Sample Maker\n"
        "镜头型号: 24-70mm\n"
        "曝光时间: 1/125 s\n"
        "光圈值: F2.8\n"
        "ISO感光度: 200\n"
        "白平衡: 自动\n"
        "色彩空间: sRGB\n"
        "Xmp.xmp.CreatorTool: Microsoft Windows Photo Viewer with a deliberately long creator name\n"
        "Xmp.xmpMM.InstanceID: uuid:faf5bdd5-ba3d-11da-ad31-d33d75182f1b\n"
        "Exif.Photo.MakerNote: private binary payload\n";
    const auto model = ImageInfoPresentation::build(rawInfo, UiLanguage::SIMPLIFIED, "RGBA · 32bpp");

    passOrFail("full image information preserves the complete filename path and color mode",
        model.basic.size() == 6 &&
        model.basic[0].label == "文件名" &&
        model.basic[0].value == "这是一个非常长而且必须完整换行显示的图片文件名_sample.png" &&
        model.basic[1].label == "路径" && model.basic[1].value.starts_with("C:\\Pictures\\") &&
        model.basic[2].value == "PNG" && model.basic[3].value == "103.0 KiB" &&
        model.basic[4].value == "671 × 477 px" && model.basic[5].value == "RGBA · 32bpp");
    const auto compact = ImageInfoPresentation::compactRows(model, UiLanguage::SIMPLIFIED);
    passOrFail("compact image information follows the five-field reference order",
        compact.size() == 5 && compact[0].label == "格式" &&
        compact[1].label == "文件大小" && compact[2].label == "分辨率" &&
        compact[3].label == "色彩" && compact[4].label == "文件名");
    passOrFail("image information keeps all selected useful metadata and omits MakerNote noise",
        model.details.size() > 6 &&
        std::ranges::none_of(model.details, [](const ImageInfoPresentation::Row& row) {
            return row.label.contains("MakerNote") || row.value.contains("MakerNote");
            }));
    const std::string longValue = compact.back().value;
    const auto wrapped = ImageInfoPresentation::wrapUtf8(longValue, 18);
    std::string restored;
    for (const auto& line : wrapped)
        restored += line;
    passOrFail("long UTF-8 image information wraps without ellipsis truncation or hidden bytes",
        wrapped.size() > 1 && restored == longValue &&
        ImageInfoPresentation::joinWrappedLines(wrapped).find("...") == std::string::npos);
    passOrFail("image information uses twelve-pixel compact and full cards",
        ImageInfoPresentation::LOGICAL_FONT_SIZE == 12 &&
        ImageInfoPresentation::LOGICAL_COMPACT_PANEL_WIDTH == 288 &&
        ImageInfoPresentation::LOGICAL_FULL_PANEL_WIDTH == 340);
    passOrFail("image information scrolling clamps at both content boundaries",
        ImageInfoPresentation::clampScrollOffset(800, 300, -20) == 0 &&
        ImageInfoPresentation::clampScrollOffset(800, 300, 240) == 240 &&
        ImageInfoPresentation::clampScrollOffset(800, 300, 900) == 500);
}

void expectWindowTitlePresentation() {
    const auto title = WindowTitlePresentation::build({
        .current = 3,
        .total = 16,
        .zoomPercent = 125,
        .pixelWidth = 1514,
        .pixelHeight = 857,
        .fileSize = L"1.6 MiB",
        .fileName = L"gpu心智图.png",
        });
    passOrFail("window title reads position, name, pixels with size, then zoom",
        title == L"[03/16] gpu心智图.png 1514x857(1.6MB) 125%");

    // 序号补零对齐：标题栏是比例字体，补空格照样跳，只有补零宽度才固定。
    const auto padded = WindowTitlePresentation::build({
        .current = 8, .total = 22, .zoomPercent = 110,
        .pixelWidth = 671, .pixelHeight = 477,
        .fileSize = L"108.0 KiB",
        .fileName = L"PixPin_2026-07-14_17-12-54.png",
        });
    passOrFail("single-digit position is zero padded to the width of the total",
        padded == L"[08/22] PixPin_2026-07-14_17-12-54.png 671x477(108.0KB) 110%");

    // 状态和旋转排在最后：它们时有时无，夹在中间会把后面的字段整体推走。
    const auto decorated = WindowTitlePresentation::build({
        .state = L"暂停",
        .current = 3, .total = 4, .zoomPercent = 100,
        .pixelWidth = 64, .pixelHeight = 64,
        .fileSize = L"2.3 KiB",
        .fileName = L"a.gif",
        .rotation = L"右转 90°",
        });
    passOrFail("optional state and rotation trail the fixed fields",
        decorated == L"[3/4] a.gif 64x64(2.3KB) 100% 暂停 右转 90°");

    passOrFail("file size drops the space and the IEC i",
        WindowTitlePresentation::compactSize(L"1.6 MiB") == L"1.6MB" &&
        WindowTitlePresentation::compactSize(L"980 KiB") == L"980KB" &&
        WindowTitlePresentation::compactSize(L"12 Bytes") == L"12Bytes");
}

void expectFileTargetConfig() {
    using namespace FileTargetConfig;

    Model model;
    addTarget(model, LR"(D:\photos\精选)");
    addTarget(model, LR"(D:\photos\废片)");
    passOrFail("adding a target makes it the current one",
        model.targets.size() == 2 && model.active == 1 &&
        activeTarget(model) == LR"(D:\photos\废片)");

    // 同一个位置再加一次不该多出一条，只是把它切回当前
    addTarget(model, LR"(d:/photos/精选)");
    passOrFail("re-adding an existing target just switches to it",
        model.targets.size() == 2 && model.active == 0);

    for (int index = 0; index < 4; ++index)
        addTarget(model, L"D:\\photos\\第" + std::to_wstring(index));
    passOrFail("the target list is capped at five and drops the oldest",
        model.targets.size() == MAX_TARGETS &&
        model.targets.front() != LR"(D:\photos\精选)");

    // 删掉当前之前的一项，当前指向的那个位置要跟着往前挪，不能指到别人身上
    Model shifting;
    addTarget(shifting, L"A");
    addTarget(shifting, L"B");
    addTarget(shifting, L"C");
    setActive(shifting, 2);
    removeTarget(shifting, 0);
    passOrFail("removing an earlier target keeps the current one selected",
        shifting.targets.size() == 2 && activeTarget(shifting) == L"C");
    removeTarget(shifting, 1);
    passOrFail("removing the current target falls back to the last one",
        shifting.targets.size() == 1 && activeTarget(shifting) == L"B");
    removeTarget(shifting, 0);
    passOrFail("an empty list reports no target instead of an out-of-range index",
        !hasTarget(shifting) && activeTarget(shifting).empty());

    passOrFail("the menu shows the folder name, not the whole path",
        displayName(LR"(D:\photos\精选)") == L"精选" &&
        displayName(LR"(D:\photos\精选\)") == L"精选");

    // 目标里已有同名文件时让路，绝不覆盖
    const std::vector<std::wstring> existing{ L"a.png", L"a (2).png" };
    const auto exists = [&](const std::wstring& name) {
        return std::find(existing.begin(), existing.end(), name) != existing.end();
    };
    passOrFail("a colliding name steps aside instead of overwriting",
        uniqueFileName(L"a.png", exists) == L"a (3).png" &&
        uniqueFileName(L"b.png", exists) == L"b.png");
    passOrFail("a name without an extension still gets a numbered suffix",
        uniqueFileName(L"README", exists) == L"README" &&
        uniqueFileName(L"a", [](const std::wstring& name) { return name == L"a"; }) == L"a (2)");
    // 一万个候选名全被占（几乎不可能，但不能因此死循环）：放弃并退回原名，
    // 交给上层的复制调用去报错，而不是在这里转到天荒地老。
    passOrFail("an impossible collision gives up instead of looping forever",
        uniqueFileName(L"a.png", [](const std::wstring&) { return true; }) == L"a.png");
}

void expectFullscreenInfoBar() {
    // 信息条贴左上角，宽度跟着文字走，但最宽不超过画布一半——再长就横穿整个画面。
    const auto narrow = FullscreenInfoBar::place(1920, 1080, 96, 300);
    passOrFail("fullscreen info bar hugs the top-left corner",
        narrow.x == 12 && narrow.y == 12 && narrow.width == 320 && narrow.height == 26);
    const auto clamped = FullscreenInfoBar::place(1920, 1080, 96, 4000);
    passOrFail("a very long title is capped at half the canvas width",
        clamped.width == 960);
    passOrFail("the bar disappears in a window too small to hold it",
        FullscreenInfoBar::place(40, 40, 96, 300).empty());
    // 「实况」标记同在左上角，显示信息条时要整体让开一行，否则两者叠在一起。
    passOrFail("the live badge steps down only while the info bar shows",
        FullscreenInfoBar::badgeTopOffset(true, 96) == 32 &&
        FullscreenInfoBar::badgeTopOffset(false, 96) == 0 &&
        FullscreenInfoBar::badgeTopOffset(true, 192) == 64);
}

void expectWheelInput() {
    constexpr int panStep = 96;
    passOrFail("ordinary wheel pans the image vertically by default",
        WheelInput::resolveDefault(0, 120, panStep).intent == WheelInput::Intent::PanVertical &&
        WheelInput::resolveDefault(0, 120, panStep).verticalDelta == panStep &&
        WheelInput::resolveDefault(0, -120, panStep).verticalDelta == -panStep);
    passOrFail("Ctrl wheel zooms in or out by default",
        WheelInput::resolveDefault(WheelInput::CONTROL_FLAG, 120, panStep).intent == WheelInput::Intent::ZoomIn &&
        WheelInput::resolveDefault(WheelInput::CONTROL_FLAG, -120, panStep).intent == WheelInput::Intent::ZoomOut);
    passOrFail("Shift wheel pans the image horizontally by default",
        WheelInput::resolveDefault(WheelInput::SHIFT_FLAG, 120, panStep).intent == WheelInput::Intent::PanHorizontal &&
        WheelInput::resolveDefault(WheelInput::SHIFT_FLAG, 120, panStep).horizontalDelta == panStep &&
        WheelInput::resolveDefault(WheelInput::SHIFT_FLAG, -120, panStep).horizontalDelta == -panStep);
    passOrFail("Ctrl takes priority when Ctrl and Shift are both held",
        WheelInput::resolveDefault(WheelInput::CONTROL_FLAG | WheelInput::SHIFT_FLAG, 120, panStep).intent ==
            WheelInput::Intent::ZoomIn);
    passOrFail("wheel actions can be remapped without changing direction semantics",
        WheelInput::resolve(0, 120, panStep,
            ShortcutConfig::WheelAction::SwitchImage,
            ShortcutConfig::WheelAction::PanHorizontal,
            ShortcutConfig::WheelAction::Zoom).intent == WheelInput::Intent::PreviousImage &&
        WheelInput::resolve(WheelInput::CONTROL_FLAG, -120, panStep,
            ShortcutConfig::WheelAction::SwitchImage,
            ShortcutConfig::WheelAction::PanHorizontal,
            ShortcutConfig::WheelAction::Zoom).horizontalDelta == -panStep);
    passOrFail("zero wheel delta does not enqueue an action",
        WheelInput::resolveDefault(WheelInput::CONTROL_FLAG | WheelInput::SHIFT_FLAG, 0, panStep).intent ==
            WheelInput::Intent::Default);
}

void expectShortcutConfig() {
    std::array<uint32_t, 777> storage{};
    ShortcutConfig::initialize(storage.data(), storage.size());
    passOrFail("shortcut storage upgrades old settings to the requested defaults",
        storage[ShortcutConfig::MAGIC_INDEX] == ShortcutConfig::STORAGE_MAGIC &&
        ShortcutConfig::getWheelAction(storage.data(), 0) == ShortcutConfig::WheelAction::PanVertical &&
        ShortcutConfig::getWheelAction(storage.data(), 1) == ShortcutConfig::WheelAction::Zoom &&
        ShortcutConfig::getWheelAction(storage.data(), 2) == ShortcutConfig::WheelAction::PanHorizontal &&
        ShortcutConfig::getBinding(storage.data(), ShortcutConfig::Action::RenameImage) ==
            ShortcutConfig::binding(0x71));
    ShortcutConfig::setBinding(storage.data(), ShortcutConfig::Action::RenameImage,
        ShortcutConfig::binding('R', ShortcutConfig::MODIFIER_CONTROL));
    ShortcutConfig::setBinding(storage.data(), ShortcutConfig::Action::OpenFile,
        ShortcutConfig::binding('R', ShortcutConfig::MODIFIER_CONTROL));
    passOrFail("reassigning a shortcut removes the conflicting old assignment",
        ShortcutConfig::getBinding(storage.data(), ShortcutConfig::Action::RenameImage) == 0 &&
        ShortcutConfig::matches(ShortcutConfig::getBinding(storage.data(),
            ShortcutConfig::Action::OpenFile), 'R', ShortcutConfig::MODIFIER_CONTROL));
    ShortcutConfig::setWheelAction(storage.data(), 0, ShortcutConfig::WheelAction::SwitchImage);
    ShortcutConfig::initialize(storage.data(), storage.size());
    passOrFail("valid custom keyboard and wheel mappings survive settings validation",
        ShortcutConfig::getWheelAction(storage.data(), 0) == ShortcutConfig::WheelAction::SwitchImage &&
        ShortcutConfig::getBinding(storage.data(), ShortcutConfig::Action::OpenFile) ==
            ShortcutConfig::binding('R', ShortcutConfig::MODIFIER_CONTROL));
    ShortcutConfig::reset(storage.data(), storage.size());
    passOrFail("shortcut reset restores every keyboard and wheel default",
        ShortcutConfig::getWheelAction(storage.data(), 0) == ShortcutConfig::DEFAULT_WHEEL_ACTIONS[0] &&
        ShortcutConfig::getBinding(storage.data(), ShortcutConfig::Action::OpenFile) ==
            ShortcutConfig::DEFAULT_BINDINGS[ShortcutConfig::actionIndex(ShortcutConfig::Action::OpenFile)] &&
        ShortcutConfig::keyName(ShortcutConfig::binding(0x71), UiLanguage::SIMPLIFIED) == "F2");

    // 追加动作后升级旧配置：只补新动作，用户改过的绑定必须原样保留。整体 reset 会
    // 清空全部自定义快捷键，所以这条回归盯的就是“别把用户配置洗掉”。
    std::array<uint32_t, 777> upgraded{};
    ShortcutConfig::reset(upgraded.data(), upgraded.size());
    ShortcutConfig::setBinding(upgraded.data(), ShortcutConfig::Action::RotateLeft,
        ShortcutConfig::binding('Z'));
    upgraded[ShortcutConfig::VERSION_INDEX] = 1;
    upgraded[ShortcutConfig::BINDING_BASE_INDEX +
        ShortcutConfig::actionIndex(ShortcutConfig::Action::ZoomActual)] = 0;
    ShortcutConfig::initialize(upgraded.data(), upgraded.size());
    passOrFail("upgrading version one storage adds new actions and keeps custom bindings",
        upgraded[ShortcutConfig::VERSION_INDEX] == ShortcutConfig::STORAGE_VERSION &&
        ShortcutConfig::getBinding(upgraded.data(), ShortcutConfig::Action::ZoomActual) ==
            ShortcutConfig::binding('1') &&
        ShortcutConfig::getBinding(upgraded.data(), ShortcutConfig::Action::RotateLeft) ==
            ShortcutConfig::binding('Z') &&
        ShortcutConfig::getBinding(upgraded.data(), ShortcutConfig::Action::ZoomFit) ==
            ShortcutConfig::binding('5'));

    std::array<uint32_t, 777> contested{};
    ShortcutConfig::reset(contested.data(), contested.size());
    ShortcutConfig::setBinding(contested.data(), ShortcutConfig::Action::ToggleFullscreen,
        ShortcutConfig::binding('1'));
    contested[ShortcutConfig::VERSION_INDEX] = 1;
    ShortcutConfig::initialize(contested.data(), contested.size());
    passOrFail("upgrade leaves a new action unbound rather than stealing an assigned key",
        ShortcutConfig::getBinding(contested.data(), ShortcutConfig::Action::ZoomActual) == 0 &&
        ShortcutConfig::matches(ShortcutConfig::getBinding(contested.data(),
            ShortcutConfig::Action::ToggleFullscreen), '1', 0));

    std::array<uint32_t, 777> future{};
    ShortcutConfig::reset(future.data(), future.size());
    future[ShortcutConfig::VERSION_INDEX] = ShortcutConfig::STORAGE_VERSION + 1;
    ShortcutConfig::initialize(future.data(), future.size());
    passOrFail("storage written by a newer version falls back to defaults",
        future[ShortcutConfig::VERSION_INDEX] == ShortcutConfig::STORAGE_VERSION &&
        ShortcutConfig::getBinding(future.data(), ShortcutConfig::Action::ZoomActual) ==
            ShortcutConfig::binding('1'));

    // 「关闭图片」从「行为」里的开关变成了快捷键，默认就绑在 Esc 上。
    std::array<uint32_t, 777> closing{};
    ShortcutConfig::reset(closing.data(), closing.size());
    passOrFail("closing the image defaults to Escape",
        ShortcutConfig::matches(ShortcutConfig::getBinding(closing.data(),
            ShortcutConfig::Action::CloseImage), 0x1B /* VK_ESCAPE */, 0) &&
        ShortcutConfig::keyName(ShortcutConfig::binding(0x1B /* VK_ESCAPE */), UiLanguage::SIMPLIFIED) == "Esc");

    // 从版本 2 升级只能补第 32 个动作。按版本 1 的数量去补会把 31 号（实际大小）
    // 一起重写，用户改过的键位就没了——这条盯的就是这个回归。
    std::array<uint32_t, 777> fromVersion2{};
    ShortcutConfig::reset(fromVersion2.data(), fromVersion2.size());
    ShortcutConfig::setBinding(fromVersion2.data(), ShortcutConfig::Action::ZoomActual,
        ShortcutConfig::binding('8'));
    fromVersion2[ShortcutConfig::VERSION_INDEX] = 2;
    fromVersion2[ShortcutConfig::BINDING_BASE_INDEX +
        ShortcutConfig::actionIndex(ShortcutConfig::Action::CloseImage)] = 0;
    const uint32_t previous = ShortcutConfig::initialize(fromVersion2.data(), fromVersion2.size());
    passOrFail("upgrading version two storage adds Escape without touching custom keys",
        previous == 2 &&
        ShortcutConfig::matches(ShortcutConfig::getBinding(fromVersion2.data(),
            ShortcutConfig::Action::CloseImage), 0x1B /* VK_ESCAPE */, 0) &&
        ShortcutConfig::matches(ShortcutConfig::getBinding(fromVersion2.data(),
            ShortcutConfig::Action::ZoomActual), '8', 0));
    // 返回值是给调用方用的：只有读到旧版本才需要迁移「Esc 关闭图片」这类老开关
    std::array<uint32_t, 777> blank{};
    passOrFail("initialize reports the version it read so callers know when to migrate",
        ShortcutConfig::initialize(fromVersion2.data(), fromVersion2.size()) ==
            ShortcutConfig::STORAGE_VERSION &&
        ShortcutConfig::initialize(blank.data(), blank.size()) == 0);

    // 清空后必须留在「未设置」，不能被下一次校验悄悄填回默认键
    std::array<uint32_t, 777> cleared{};
    ShortcutConfig::reset(cleared.data(), cleared.size());
    ShortcutConfig::setBinding(cleared.data(), ShortcutConfig::Action::DeleteImage, 0);
    ShortcutConfig::initialize(cleared.data(), cleared.size());
    passOrFail("a cleared shortcut stays unassigned and never fires",
        ShortcutConfig::getBinding(cleared.data(), ShortcutConfig::Action::DeleteImage) == 0 &&
        !ShortcutConfig::matches(0, 0x2E /* VK_DELETE */, 0) &&
        ShortcutConfig::keyName(0, UiLanguage::SIMPLIFIED) == "未设置" &&
        ShortcutConfig::keyName(0, UiLanguage::ENGLISH) == "Unassigned");

    // 繁體是第三种语言，不是「中文」的别名。凡是当年写成 chinese ? 简体 : English 的
    // 地方，漏改一处就会让繁體界面掉回简体或英文，这里挑几条有代表性的钉住。
    passOrFail("traditional Chinese is a language of its own, not an alias of simplified",
        UiLanguage::pick<const char*>(UiLanguage::TRADITIONAL, "简", "en", "繁") == std::string("繁") &&
        UiLanguage::pick<const char*>(UiLanguage::SIMPLIFIED, "简", "en", "繁") == std::string("简") &&
        UiLanguage::pick<const char*>(UiLanguage::ENGLISH, "简", "en", "繁") == std::string("en") &&
        UiLanguage::isChinese(UiLanguage::TRADITIONAL) &&
        UiLanguage::isChinese(UiLanguage::SIMPLIFIED) &&
        !UiLanguage::isChinese(UiLanguage::ENGLISH) &&
        UiLanguage::clamp(99) == UiLanguage::SIMPLIFIED);

    passOrFail("shortcut key names and image info labels have their own traditional wording",
        ShortcutConfig::keyName(0, UiLanguage::TRADITIONAL) == "未設定" &&
        ShortcutConfig::keyName(ShortcutConfig::binding(0x20), UiLanguage::TRADITIONAL) == "空白鍵" &&
        ShortcutConfig::keyName(ShortcutConfig::binding(0x25), UiLanguage::TRADITIONAL) == "左方向鍵");

    // 双键、三键、四键：修饰键任意组合都要存得下、认得出、显示得对
    std::array<uint32_t, 777> combos{};
    ShortcutConfig::reset(combos.data(), combos.size());
    const uint32_t triple = ShortcutConfig::binding('S',
        ShortcutConfig::MODIFIER_CONTROL | ShortcutConfig::MODIFIER_SHIFT);
    const uint32_t quad = ShortcutConfig::binding('P',
        ShortcutConfig::MODIFIER_CONTROL | ShortcutConfig::MODIFIER_SHIFT |
        ShortcutConfig::MODIFIER_ALT);
    ShortcutConfig::setBinding(combos.data(), ShortcutConfig::Action::ExportFrames, triple);
    ShortcutConfig::setBinding(combos.data(), ShortcutConfig::Action::PrintImage, quad);
    ShortcutConfig::initialize(combos.data(), combos.size());
    passOrFail("two, three and four key combinations round-trip through storage",
        ShortcutConfig::getBinding(combos.data(), ShortcutConfig::Action::ExportFrames) == triple &&
        ShortcutConfig::getBinding(combos.data(), ShortcutConfig::Action::PrintImage) == quad &&
        ShortcutConfig::matches(quad, 'P', ShortcutConfig::MODIFIER_CONTROL |
            ShortcutConfig::MODIFIER_SHIFT | ShortcutConfig::MODIFIER_ALT) &&
        !ShortcutConfig::matches(quad, 'P', ShortcutConfig::MODIFIER_CONTROL) &&
        ShortcutConfig::keyName(triple, UiLanguage::SIMPLIFIED) == "Ctrl+Shift+S" &&
        ShortcutConfig::keyName(quad, UiLanguage::SIMPLIFIED) == "Ctrl+Shift+Alt+P");
}

void expectDrawioTextFallback(std::string_view path) {
    const auto source = readFile(path);
    const auto processed = SvgRenderer::preprocess(source);
    const auto renderer = SvgRenderer::create(source);

    const bool selectedFallback = !processed.empty() &&
        processed.find("<foreignObject") == std::string::npos &&
        processed.find("<switch") == std::string::npos &&
        processed.find("light-dark(") == std::string::npos &&
        processed.find("data-yeimageviewer=\"drawio-text\"") != std::string::npos &&
        std::count(processed.begin(), processed.end(), '<') > 74;
    if (!selectedFallback || !renderer) {
        std::cerr << "draw.io preprocess diagnostics: bytes=" << processed.size()
            << ", marker=" << (processed.find("data-yeimageviewer") != std::string::npos)
            << ", text=" << (processed.find("<text") != std::string::npos)
            << ", image=" << (processed.find("<image") != std::string::npos)
            << ", renderer=" << static_cast<bool>(renderer) << '\n';
    }
    passOrFail("draw.io foreignObject labels become native SVG text", selectedFallback);

    if (!renderer) {
        passOrFail("draw.io SVG renders fallback label pixels", false);
        return;
    }

    const auto bitmap = renderer->renderToBitmap(723, 1130);
    size_t darkTitlePixels = 0;
    if (!bitmap.empty()) {
        for (int y = 10; y < 36; ++y) {
            for (int x = 40; x < 645; ++x) {
                const size_t offset = (static_cast<size_t>(y) * bitmap.width + x) * 4;
                const int brightness = bitmap.bgra[offset] + bitmap.bgra[offset + 1] + bitmap.bgra[offset + 2];
                if (bitmap.bgra[offset + 3] > 32 && brightness < 480) {
                    ++darkTitlePixels;
                }
            }
        }
    }
    const bool lightBackground = !bitmap.empty() &&
        bitmap.bgra[0] > 240 && bitmap.bgra[1] > 240 && bitmap.bgra[2] > 240 && bitmap.bgra[3] > 240;
    passOrFail("draw.io SVG renders native label pixels", darkTitlePixels > 100);
    passOrFail("draw.io light-dark CSS uses its light fallback", lightBackground);

    const auto enlarged = renderer->renderToBitmap(1446, 2260);
    size_t nativeTextDifferences = 0;
    if (!bitmap.empty() && !enlarged.empty()) {
        for (int y = 0; y < enlarged.height; ++y) {
            for (int x = 0; x < enlarged.width; ++x) {
                const size_t largeOffset = (static_cast<size_t>(y) * enlarged.width + x) * 4;
                const size_t smallOffset = (static_cast<size_t>(y / 2) * bitmap.width + x / 2) * 4;
                for (int channel = 0; channel < 4; ++channel) {
                    nativeTextDifferences += enlarged.bgra[largeOffset + channel] != bitmap.bgra[smallOffset + channel];
                }
            }
        }
    }
    passOrFail("draw.io text is rerendered as vector content when enlarged",
        !enlarged.empty() && nativeTextDifferences > enlarged.bgra.size() / 100);
}

// ===== 图片缓存（LRU.h）=====
// 这是整个看图流程的中枢：当前图、预读、翻页、删除后失效都压在它身上。
// 它自带一个后台预读线程，线程跑的是派生类的 loader()，所以除了容量/淘汰
// 这些常规语义，还要盯住「生命周期」和「在途任务作废」两件事。

class CountingCache : public LRU<int, std::string> {
public:
    std::atomic<int> loadCount{ 0 };
    std::atomic<int> delayMs{ 0 };

    ~CountingCache() override { stopPreloadWorker(); }

    std::string loader(const int& key) override {
        const int wait = delayMs.load();
        if (wait > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        ++loadCount;
        {
            std::lock_guard<std::mutex> lock(orderMutex);
            loadOrder.push_back(key);
        }
        return "v" + std::to_string(key);
    }

    std::vector<int> takeLoadOrder() {
        std::lock_guard<std::mutex> lock(orderMutex);
        return loadOrder;
    }

private:
    std::mutex orderMutex;
    std::vector<int> loadOrder;
};

// 等到谓词成立，最多等 timeoutMs。用轮询而不是固定 sleep：机器忙的时候
// 固定等待会假失败，等够了又白等。
bool waitUntil(const std::function<bool()>& ready, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

// 析构顺序的探针：成员的析构发生在派生类析构函数体「之后」，所以这个成员
// 看到的必须已经是「预读线程停了」的状态。谁把 stopPreloadWorker() 从派生类的
// 析构里删掉，这一条立刻变红。
struct WorkerStoppedProbe {
    std::function<bool()> workerRunning;
    bool* violated = nullptr;

    ~WorkerStoppedProbe() {
        if (workerRunning && workerRunning() && violated != nullptr)
            *violated = true;
    }
};

class ShutdownOrderCache : public LRU<int, std::string> {
public:
    explicit ShutdownOrderCache(bool* violated) {
        probe.workerRunning = [this] { return preloadWorkerRunning(); };
        probe.violated = violated;
    }

    ~ShutdownOrderCache() override { stopPreloadWorker(); }

    std::string loader(const int& key) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return "v" + std::to_string(key);
    }

private:
    WorkerStoppedProbe probe;
};

void expectLruCache() {
    // 存进去就能原样取出来
    {
        CountingCache cache;
        cache.put(1, "one");
        const auto hit = cache.tryGetPtr(1);
        passOrFail("LRU stores and returns a value", hit && *hit == "one" && cache.size() == 1);
        passOrFail("LRU reports a miss without loading", cache.tryGetPtr(2) == nullptr && cache.loadCount == 0);
    }

    // 超出容量淘汰最久没用的那个
    {
        CountingCache cache;
        cache.setCapacity(3);
        for (int key = 1; key <= 4; ++key)
            cache.put(key, "v" + std::to_string(key));
        passOrFail("LRU evicts the least recently used entry",
            cache.size() == 3 && cache.tryGetPtr(1) == nullptr && cache.tryGetPtr(4) != nullptr);
    }

    // 容量下限 3、上限 4096，越界一律回落到 3。
    // 下限不是随手定的：翻页时同时要留住「上一张、当前、下一张」，少于 3 会自己打自己。
    {
        CountingCache cache;
        cache.setCapacity(2);
        for (int key = 1; key <= 5; ++key)
            cache.put(key, "x");
        passOrFail("LRU clamps a too-small capacity up to 3", cache.size() == 3);
    }
    {
        CountingCache cache;
        cache.setCapacity(99999);
        for (int key = 1; key <= 5; ++key)
            cache.put(key, "x");
        passOrFail("LRU clamps an absurd capacity back to 3", cache.size() == 3);
    }

    // 调小容量要立刻裁掉多出来的，不能等下一次写入才裁
    {
        CountingCache cache;
        cache.setCapacity(10);
        for (int key = 1; key <= 8; ++key)
            cache.put(key, "x");
        cache.setCapacity(3);
        passOrFail("LRU trims immediately when the capacity shrinks", cache.size() == 3);
    }

    // 命中要刷新「最近使用」，否则正在看的那张图会被预读挤掉
    {
        CountingCache cache;
        cache.setCapacity(3);
        cache.put(1, "a");
        cache.put(2, "b");
        cache.put(3, "c");
        const auto refreshed = cache.getDataPtr(1);   // 1 重新变成最新
        cache.put(4, "d");                            // 该淘汰的是 2
        passOrFail("a cache hit refreshes recency",
            refreshed != nullptr && cache.tryGetPtr(1) != nullptr && cache.tryGetPtr(2) == nullptr);
    }

    // 同一个 key 再写一次是替换，不是新增
    {
        CountingCache cache;
        cache.put(7, "old");
        cache.put(7, "new");
        const auto hit = cache.tryGetPtr(7);
        passOrFail("writing the same key replaces the value", cache.size() == 1 && hit && *hit == "new");
    }

    // 缺的 key 由 getSafePtr 触发解码并等到结果
    {
        CountingCache cache;
        const auto loaded = cache.getSafePtr(42);
        passOrFail("getSafePtr loads a missing key",
            loaded && *loaded == "v42" && cache.loadCount == 1 && cache.tryGetPtr(42) != nullptr);
    }

    // 命中不该再解一次
    {
        CountingCache cache;
        cache.put(5, "cached");
        const auto hit = cache.getSafePtr(5);
        passOrFail("getSafePtr does not reload a cached key", hit && *hit == "cached" && cache.loadCount == 0);
    }

    // tryGetPtr 绝不等待：首次打开大图时主线程靠它先把窗口显示出来
    {
        CountingCache cache;
        cache.delayMs = 400;
        cache.requestPreload(3);
        const auto start = std::chrono::steady_clock::now();
        const auto missing = cache.tryGetPtr(3);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        passOrFail("tryGetPtr never waits for a decode in flight", missing == nullptr && elapsedMs < 100);
        passOrFail("the in-flight decode still lands in the cache",
            waitUntil([&] { return cache.tryGetPtr(3) != nullptr; }));
    }

    // 同一个 key 排队期间重复请求只解一次
    {
        CountingCache cache;
        cache.delayMs = 150;
        for (int repeat = 0; repeat < 5; ++repeat)
            cache.requestPreload(9);
        passOrFail("a key queued twice is decoded once",
            waitUntil([&] { return cache.tryGetPtr(9) != nullptr; }) && cache.loadCount == 1);
    }

    // 当前要显示的那张插队首：快速翻页时它不能排在旧的预读任务后面
    {
        CountingCache cache;
        cache.setCapacity(10);
        cache.delayMs = 120;
        cache.requestPreloadBatch({ 1, 2, 3 });               // 工作线程开始解 1
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cache.requestPreload(99, true);                       // 队列变成 [99, 2, 3]
        const bool done = waitUntil([&] { return cache.tryGetPtr(3) != nullptr; }, 5000);
        const auto order = cache.takeLoadOrder();
        passOrFail("an urgent request jumps the preload queue",
            done && order.size() == 4 && order[0] == 1 && order[1] == 99);
    }

    // 批量预读把整批都解出来
    {
        CountingCache cache;
        cache.setCapacity(10);
        cache.requestPreloadBatch({ 11, 12, 13 });
        passOrFail("a preload batch decodes every key",
            waitUntil([&] { return cache.size() == 3; }) && cache.loadCount == 3);
    }

    // 翻页那一对：当前图要等到，下一张只预读
    {
        CountingCache cache;
        cache.setCapacity(10);
        const auto current = cache.getSafePtr(20, 21);
        passOrFail("getSafePtr returns the current image and preloads the next",
            current && *current == "v20" && waitUntil([&] { return cache.tryGetPtr(21) != nullptr; }));
    }

    // 删除/重命名之后必须失效，否则之后出现的同名文件会命中过期内容
    {
        CountingCache cache;
        cache.put(4, "stale");
        cache.erase(4);
        passOrFail("erase drops the cached entry", cache.tryGetPtr(4) == nullptr && cache.size() == 0);
    }

    // erase 还要作废在途的那次解码，否则它会把刚删掉的内容又写回来
    {
        CountingCache cache;
        cache.delayMs = 250;
        cache.requestPreload(6);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));  // 确保线程已经进了 loader
        cache.erase(6);
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 等那次解码跑完
        passOrFail("erase invalidates a decode already in flight",
            cache.loadCount == 1 && cache.tryGetPtr(6) == nullptr);
    }

    // clear 清空缓存并丢掉排队的任务（启动那一次要靠 keepWarmCache 绕开它）
    {
        CountingCache cache;
        cache.setCapacity(10);
        cache.delayMs = 200;
        cache.put(1, "a");
        cache.requestPreloadBatch({ 31, 32, 33 });
        cache.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        passOrFail("clear empties the cache and cancels queued preloads",
            cache.size() == 0 && cache.loadCount <= 1);
    }

    // 关键的生命周期约定：预读线程跑的是派生类的 loader()，用的是派生类的成员。
    // 停线程必须发生在派生部分销毁之前，否则在途那次解码就在访问已释放的内存
    // ——打开大图后立刻退出程序曾经就是这样崩的。
    {
        bool violated = false;
        {
            ShutdownOrderCache cache(&violated);
            cache.requestPreload(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        passOrFail("the preload worker stops before the derived members die", !violated);
    }

    // 重复停线程无害：基类析构里还会再调一次
    {
        CountingCache cache;
        cache.stopPreloadWorker();
        cache.stopPreloadWorker();
        cache.put(1, "a");
        passOrFail("stopping the preload worker twice is harmless",
            !cache.preloadWorkerRunning() && cache.tryGetPtr(1) != nullptr);
    }

    // 线程停了之后排队的请求不再解码，但缓存照样可读可写
    {
        CountingCache cache;
        cache.stopPreloadWorker();
        cache.requestPreload(8);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        passOrFail("no key is decoded after the worker stops",
            cache.loadCount == 0 && cache.tryGetPtr(8) == nullptr);
    }
}

// ===== 文件关联的注册表键名（FileAssociationNaming.h）=====
// 写错一个字就是「关联了但右键里没有」或者把别人的键覆盖掉，而且出错时
// 很难从界面上看出来，所以逐条钉死。

void expectFileAssociationNaming() {
    using namespace FileAssociationNaming;

    passOrFail("extension normalization strips the dot and lowercases",
        NormalizeExtension(L".PNG") == L"png" && NormalizeExtension(L"JpEg") == L"jpeg");
    passOrFail("extension normalization strips repeated dots",
        NormalizeExtension(L"...tif") == L"tif");
    passOrFail("extension normalization tolerates an empty input",
        NormalizeExtension(L"").empty() && NormalizeExtension(L".").empty());

    passOrFail("the ProgID is per extension",
        BuildProgId(L".PNG") == L"YeImageViewer.ImageFile.png" &&
        BuildProgId(L"webp") == L"YeImageViewer.ImageFile.webp");
    passOrFail("the type name carries the extension",
        BuildTypeName(L".Jxl") == L"YeImageViewer jxl 图像");
    passOrFail("the type name falls back when there is no extension",
        BuildTypeName(L"") == L"YeImageViewer 图像");

    // 默认程序写在扩展名键的默认值上，「打开方式」列表读的是它下面的
    // OpenWithProgids——两回事，键名写串了就会动到用户的默认程序。
    passOrFail("the extension key lives under Software Classes",
        BuildExtensionKey(L"PNG") == L"Software\\Classes\\.png");
    passOrFail("the ProgID key lives under Software Classes",
        BuildProgIdKey(L"png") == L"Software\\Classes\\YeImageViewer.ImageFile.png");
    passOrFail("the open-with list hangs off the extension key",
        BuildOpenWithProgidsKey(L".png") == L"Software\\Classes\\.png\\OpenWithProgids");

    // Windows 按 exe 的文件名在 Applications 下找程序，路径不参与
    passOrFail("the application key uses the executable file name only",
        BuildApplicationKey(L"D:\\tools\\YeImageViewer.exe") ==
            L"Software\\Classes\\Applications\\YeImageViewer.exe");
    passOrFail("the application key accepts forward slashes",
        BuildApplicationKey(L"D:/tools/YeImageViewer.exe") ==
            L"Software\\Classes\\Applications\\YeImageViewer.exe");
    passOrFail("a bare executable name needs no splitting",
        BuildApplicationKey(L"YeImageViewer.exe") ==
            L"Software\\Classes\\Applications\\YeImageViewer.exe");
    passOrFail("the executable file name survives spaces in the path",
        ExecutableFileName(L"C:\\Program Files\\Ye Image\\YeImageViewer.exe") == L"YeImageViewer.exe");
}
// ===== 打印路径的尺寸计算（PrintLayout.h）=====
// 打印是「做错了才知道」的功能：预览不出来没有提示，排版算歪了要等纸出来才发现。
// 这里把每种退化输入都钉住，尤其是极端长宽比——那是真出过事的一类。

void expectPrintLayout() {
    using namespace PrintLayout;

    // 普通图：按最长边贴合方形预览画布，另一边等比缩
    {
        const auto fitted = previewSize(1600, 1200, 800);
        passOrFail("preview fits the longest side to the box",
            fitted.valid && fitted.width == 800 && fitted.height == 600);
    }
    {
        const auto fitted = previewSize(1200, 1600, 800);
        passOrFail("a portrait image fits by its height",
            fitted.valid && fitted.width == 600 && fitted.height == 800);
    }
    {
        const auto fitted = previewSize(500, 500, 800);
        passOrFail("a small square is still scaled up to the box",
            fitted.valid && fitted.width == 800 && fitted.height == 800);
    }

    // 极端长宽比：短边等比算下来是 0，必须抬到 1，否则预览窗口压根建不起来，
    // 用户点「打印」什么都不发生（这正是修掉的那个缺陷）
    {
        const auto wide = previewSize(10000, 1, 800);
        passOrFail("a 10000x1 image still gets a visible preview height",
            wide.valid && wide.width == 800 && wide.height == 1);
    }
    {
        const auto tall = previewSize(1, 10000, 800);
        passOrFail("a 1x10000 image still gets a visible preview width",
            tall.valid && tall.width == 1 && tall.height == 800);
    }
    {
        const auto wide = previewSize(19200, 200, 800);
        passOrFail("a 19200x200 panorama keeps a height of at least one pixel",
            wide.valid && wide.width == 800 && wide.height >= 1 && wide.height <= 800);
    }

    // 永远不超出方形画布，否则 ROI 会越界
    for (const auto& pair : std::vector<std::pair<int, int>>{
            {10000, 1}, {1, 10000}, {19200, 200}, {200, 19200}, {8191, 8193}, {1, 1}, {3, 7} }) {
        const auto fitted = previewSize(pair.first, pair.second, 800);
        if (!fitted.valid || fitted.width < 1 || fitted.height < 1 ||
            fitted.width > 800 || fitted.height > 800) {
            passOrFail("every preview size stays inside the square canvas", false);
            break;
        }
        if (pair == std::pair<int, int>{3, 7}) {
            passOrFail("every preview size stays inside the square canvas", true);
        }
    }

    // 烂输入要明确说不行，而不是返回一个会让下游崩掉的尺寸
    passOrFail("preview rejects a degenerate image",
        !previewSize(0, 100, 800).valid && !previewSize(100, 0, 800).valid &&
        !previewSize(-5, 100, 800).valid && !previewSize(100, 100, 0).valid);

    // 页面排版：0.9 留 5% 白边，居中
    {
        const auto page = pagePlacement(1000, 1000, 4960, 7016, 0.9);
        const int expected = static_cast<int>(std::lround(1000 * 0.9 * 4960.0 / 1000));
        passOrFail("the page keeps a 5% margin on the limiting side",
            page.valid && page.width == expected && page.height == expected);
        passOrFail("the page centers the image",
            page.offsetX == (4960 - page.width + 1) / 2 &&
            page.offsetY == (7016 - page.height + 1) / 2);
    }

    // 排好的矩形必须完整落在页内——它会直接拿去建 ROI
    for (const auto& pair : std::vector<std::pair<int, int>>{
            {10000, 1}, {1, 10000}, {19200, 200}, {200, 19200},
            {16384, 16384}, {1, 1}, {4095, 4097} }) {
        const auto page = pagePlacement(pair.first, pair.second, 4960, 7016, 0.9);
        const bool inside = page.valid && page.width >= 1 && page.height >= 1 &&
            page.offsetX >= 0 && page.offsetY >= 0 &&
            page.offsetX + page.width <= 4960 && page.offsetY + page.height <= 7016;
        if (!inside) {
            passOrFail("every page placement fits inside the sheet", false);
            break;
        }
        if (pair == std::pair<int, int>{4095, 4097}) {
            passOrFail("every page placement fits inside the sheet", true);
        }
    }

    // 很小的纸张（标签打印机）也不能算出 0 边长
    {
        const auto page = pagePlacement(10000, 1, 200, 100, 0.9);
        passOrFail("a tiny sheet still gets a one-pixel-tall placement",
            page.valid && page.height == 1 && page.width >= 1 && page.width <= 200 &&
            page.offsetX + page.width <= 200 && page.offsetY + page.height <= 100);
    }

    // fillRatio 1.0 就是铺满，不留白边
    {
        const auto page = pagePlacement(100, 100, 400, 800, 1.0);
        passOrFail("a fill ratio of 1 uses the whole limiting side",
            page.valid && page.width == 400 && page.height == 400 && page.offsetX == 0);
    }

    passOrFail("page placement rejects degenerate input",
        !pagePlacement(0, 100, 400, 400, 0.9).valid &&
        !pagePlacement(100, 100, 0, 400, 0.9).valid &&
        !pagePlacement(100, 100, 400, 0, 0.9).valid &&
        !pagePlacement(100, 100, 400, 400, 0.0).valid &&
        !pagePlacement(100, 100, 400, 400, -1.0).valid);

    // 16K 上限：没超就一个像素都不动，省掉一次没必要的重采样
    {
        const auto capped = cappedSize(4000, 3000, 16384);
        passOrFail("an image under the cap is returned untouched",
            capped.valid && capped.width == 4000 && capped.height == 3000);
    }
    {
        const auto capped = cappedSize(32768, 16384, 16384);
        passOrFail("an oversized image is scaled down proportionally",
            capped.valid && capped.width == 16384 && capped.height == 8192);
    }
    {
        // 这是 limitSizeTo16K 以前会算出 0 的那种：极宽且超限
        const auto capped = cappedSize(200000, 1, 16384);
        passOrFail("capping a 200000x1 image keeps at least one row",
            capped.valid && capped.width == 16384 && capped.height == 1);
    }
    passOrFail("capping rejects degenerate input",
        !cappedSize(0, 10, 16384).valid && !cappedSize(10, 0, 16384).valid &&
        !cappedSize(10, 10, 0).valid);
}
// ===== SVG 预处理（SVGPreprocessor.h）=====
// lunaSVG 不认 <switch>，也不认 light-dark() 这类新 CSS 函数。预处理把它们
// 变成 lunaSVG 认得的形式。挑错了子元素就是「图显示成了另一种语言」或者
// 「整块内容不见了」，而这两种都不会报错，所以得逐条钉住。

void expectSvgPreprocessor() {
    SVGPreprocessor preprocessor;

    const auto process = [&preprocessor](std::string_view svg, const std::string& language) {
        return preprocessor.preprocessSVG(svg.data(), svg.size(), language);
    };

    // switch 选第一个条件匹配的子元素，没选上的要被丢掉
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch>)svg"
            R"svg(<text systemLanguage="zh">中文</text>)svg"
            R"svg(<text systemLanguage="en">English</text>)svg"
            R"svg(<text>fallback</text>)svg"
            R"svg(</switch></svg>)svg";

        const auto chinese = process(svg, "zh");
        const auto english = process(svg, "en");
        passOrFail("a switch keeps the branch matching the requested language",
            chinese.find("中文") != std::string::npos &&
            chinese.find("English") == std::string::npos &&
            english.find("English") != std::string::npos &&
            english.find("中文") == std::string::npos);
        passOrFail("a switch drops the untaken branches entirely",
            chinese.find("fallback") == std::string::npos &&
            chinese.find("<switch") == std::string::npos);
    }

    // 带地区的语言标签也要能对上：systemLanguage="en-GB" 对请求 "en" 算匹配
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch>)svg"
            R"svg(<text systemLanguage="en-GB">British</text>)svg"
            R"svg(<text>fallback</text>)svg"
            R"svg(</switch></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("a regional language tag matches the base language",
            result.find("British") != std::string::npos);
    }

    // 一个都对不上时退回没有条件属性的那一支
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch>)svg"
            R"svg(<text systemLanguage="ja">日本語</text>)svg"
            R"svg(<text>fallback</text>)svg"
            R"svg(</switch></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("an unconditional branch is the fallback",
            result.find("fallback") != std::string::npos &&
            result.find("日本語") == std::string::npos);
    }

    // requiredFeatures / requiredExtensions 一律不选：lunaSVG 没有这些能力，
    // 谎称支持的后果是那一支画不出来，而且不会有任何提示
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch>)svg"
            R"svg(<text requiredFeatures="http://example.invalid/Filter">filtered</text>)svg"
            R"svg(<text requiredExtensions="http://example.invalid/ext">extended</text>)svg"
            R"svg(<text>plain</text>)svg"
            R"svg(</switch></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("branches requiring unknown features or extensions are never taken",
            result.find("plain") != std::string::npos &&
            result.find("filtered") == std::string::npos &&
            result.find("extended") == std::string::npos);
    }

    // foreignObject 要跳过：lunaSVG 不实现它，draw.io 在它后面紧跟一份图片兜底
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch>)svg"
            R"svg(<foreignObject width="10" height="10"><div>html</div></foreignObject>)svg"
            R"svg(<text>svgfallback</text>)svg"
            R"svg(</switch></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("a foreignObject branch is skipped for the SVG fallback after it",
            result.find("svgfallback") != std::string::npos &&
            result.find("foreignObject") == std::string::npos);
    }

    // 嵌套的 switch 也要处理掉，外层选中的那支里面不能再留 switch
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><switch><g>)svg"
            R"svg(<switch><text systemLanguage="zh">内层中文</text><text>inner</text></switch>)svg"
            R"svg(</g></switch></svg>)svg";
        const auto result = process(svg, "zh");
        passOrFail("nested switches are resolved too",
            result.find("内层中文") != std::string::npos &&
            result.find("<switch") == std::string::npos &&
            result.find("inner") == std::string::npos);
    }

    // light-dark() 取浅色那个参数。lunaSVG 不认这个函数，整条声明会被丢掉，
    // 于是该有颜色的地方变成默认黑。
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg">)svg"
            R"svg(<rect style="fill: light-dark(#ff0000, #00ff00); stroke: blue"/></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("light-dark() collapses to its light value",
            result.find("#ff0000") != std::string::npos &&
            result.find("#00ff00") == std::string::npos &&
            result.find("light-dark") == std::string::npos &&
            result.find("stroke: blue") != std::string::npos);
    }

    // 参数里还有函数调用（嵌套括号）时也要截对
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg">)svg"
            R"svg(<rect style="fill: light-dark(rgb(255, 0, 0), rgb(0, 255, 0))"/></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("light-dark() handles nested parentheses in its arguments",
            result.find("rgb(255, 0, 0)") != std::string::npos &&
            result.find("rgb(0, 255, 0)") == std::string::npos &&
            result.find("light-dark") == std::string::npos);
    }

    // 同一个 style 里出现多次要全换掉
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg">)svg"
            R"svg(<rect style="fill: light-dark(#111, #222); stroke: light-dark(#333, #444)"/></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("every light-dark() in one style is replaced",
            result.find("#111") != std::string::npos &&
            result.find("#333") != std::string::npos &&
            result.find("light-dark") == std::string::npos);
    }

    // 嵌在子元素里的 style 也要处理，不能只看根节点
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><g><g>)svg"
            R"svg(<rect style="fill: light-dark(#abcdef, #fedcba)"/></g></g></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("light-dark() is replaced at any nesting depth",
            result.find("#abcdef") != std::string::npos &&
            result.find("light-dark") == std::string::npos);
    }

    // 没有 switch、没有 light-dark 的图要原样留着（结构不变）
    {
        constexpr std::string_view svg =
            R"svg(<svg xmlns="http://www.w3.org/2000/svg"><rect fill="red" width="4" height="4"/></svg>)svg";
        const auto result = process(svg, "en");
        passOrFail("an ordinary SVG passes through unchanged in substance",
            result.find("<rect") != std::string::npos &&
            result.find("red") != std::string::npos);
    }

    // 解析不了的内容要明确返回空，不能把半截 XML 交给 lunaSVG
    {
        constexpr std::string_view broken = "<svg><rect";
        passOrFail("unparseable SVG yields nothing rather than half a document",
            process(broken, "en").empty());
        passOrFail("empty input yields nothing", process("", "en").empty());
    }
}
// ===== 缩略图组件（YeThumbnailProvider.dll）=====
// 这是随程序交付的三个文件之一，之前一条自动化测试都没有：注册成功不等于
// 真能出图，而资源管理器只会默默显示一个通用图标，不报错也没日志。
//
// 这里直接 LoadLibrary 构建产物里的那份 DLL，走 DllGetClassObject →
// IInitializeWithStream → IThumbnailProvider::GetThumbnail，和资源管理器
// 调它的路子一样，但不依赖注册表、不受 shell 缩略图缓存干扰。

struct ThumbnailModule {
    HMODULE handle = nullptr;
    HRESULT(STDAPICALLTYPE* getClassObject)(REFCLSID, REFIID, void**) = nullptr;
    HRESULT(STDAPICALLTYPE* canUnloadNow)() = nullptr;

    ~ThumbnailModule() {
        if (handle)
            FreeLibrary(handle);
    }
};

bool loadThumbnailModule(const std::string& dllPath, ThumbnailModule& module) {
    module.handle = LoadLibraryA(dllPath.c_str());
    if (!module.handle)
        return false;

    module.getClassObject = reinterpret_cast<decltype(module.getClassObject)>(
        GetProcAddress(module.handle, "DllGetClassObject"));
    module.canUnloadNow = reinterpret_cast<decltype(module.canUnloadNow)>(
        GetProcAddress(module.handle, "DllCanUnloadNow"));
    return module.getClassObject != nullptr && module.canUnloadNow != nullptr;
}

// 每次都新建一个对象：Initialize 只允许调一次，复用会得到 ERROR_ALREADY_INITIALIZED
IInitializeWithStream* createThumbnailProvider(const ThumbnailModule& module) {
    IClassFactory* factory = nullptr;
    if (FAILED(module.getClassObject(YeThumbnailProviderGuids::CLSID_YeThumbnailProvider,
        IID_IClassFactory, reinterpret_cast<void**>(&factory))) || !factory) {
        return nullptr;
    }

    IInitializeWithStream* initializer = nullptr;
    const HRESULT created = factory->CreateInstance(nullptr, IID_IInitializeWithStream,
        reinterpret_cast<void**>(&initializer));
    factory->Release();
    return SUCCEEDED(created) ? initializer : nullptr;
}

struct ThumbnailResult {
    HRESULT hr = E_FAIL;
    int width = 0;
    int height = 0;
    WTS_ALPHATYPE alphaType = WTSAT_UNKNOWN;
    bool allZero = true;
    std::vector<uint8_t> pixels;   // BGRA，自上而下
};

ThumbnailResult renderThumbnail(const ThumbnailModule& module,
    const std::vector<uint8_t>& fileBytes, UINT requestedSize) {
    ThumbnailResult result;

    IInitializeWithStream* initializer = createThumbnailProvider(module);
    if (!initializer)
        return result;

    IStream* stream = SHCreateMemStream(fileBytes.data(), static_cast<UINT>(fileBytes.size()));
    if (!stream) {
        initializer->Release();
        return result;
    }

    const HRESULT initialized = initializer->Initialize(stream, STGM_READ);
    stream->Release();

    if (FAILED(initialized)) {
        result.hr = initialized;
        initializer->Release();
        return result;
    }

    IThumbnailProvider* provider = nullptr;
    if (FAILED(initializer->QueryInterface(IID_IThumbnailProvider,
        reinterpret_cast<void**>(&provider))) || !provider) {
        initializer->Release();
        return result;
    }

    HBITMAP bitmap = nullptr;
    result.hr = provider->GetThumbnail(requestedSize, &bitmap, &result.alphaType);

    if (SUCCEEDED(result.hr) && bitmap) {
        BITMAP info{};
        if (GetObjectW(bitmap, sizeof(info), &info) != 0) {
            result.width = info.bmWidth;
            result.height = info.bmHeight;

            // 全透明/全黑的位图也是「成功」，但用户看到的是一块空白，所以要查像素
            const size_t pixelBytes = static_cast<size_t>(info.bmWidthBytes) * info.bmHeight;
            result.pixels.resize(pixelBytes);
            if (pixelBytes > 0 &&
                GetBitmapBits(bitmap, static_cast<LONG>(pixelBytes), result.pixels.data()) != 0) {
                result.allZero = std::all_of(result.pixels.begin(), result.pixels.end(),
                    [](uint8_t value) { return value == 0; });
            }
            else {
                result.pixels.clear();
            }
        }
        DeleteObject(bitmap);
    }

    provider->Release();
    initializer->Release();
    return result;
}

// 已知解不出来的，连同原因。登记在这里而不是把素材从清单里删掉：
// 删掉等于以后谁都想不起这个缺口；登记着的话，哪天某个缺口被补上了，
// 测试会提醒把这一条拿掉。
struct ThumbnailKnownGap {
    std::string_view fileName;
    std::string_view reason;
};

constexpr std::array thumbnailKnownGaps{
    ThumbnailKnownGap{ "opencv-float.exr",
        "组件里没有 EXR 解码器：主程序靠 OpenCV 的 imgcodecs 解，而这个 DLL 不链 OpenCV。"
        "WIC 也不认 OpenEXR。" },
    ThumbnailKnownGap{ "exr_color.exr", "同上" },
    ThumbnailKnownGap{ "exr_alpha.exr", "同上" },
    ThumbnailKnownGap{ "common.jp2",
        "组件里没有 JPEG 2000 解码器：jasper.lib 虽然链进来了，但仓库里没有它的头文件；"
        "WIC 默认也不带 JP2 解码器。" },
};

bool isKnownThumbnailGap(const std::string& fileName, std::string_view& reason) {
    for (const auto& gap : thumbnailKnownGaps) {
        if (gap.fileName == fileName) {
            reason = gap.reason;
            return true;
        }
    }
    return false;
}

void expectThumbnailProvider(const std::string& dllPath, const std::vector<std::string>& fixtures) {
    ThumbnailModule module;
    if (!loadThumbnailModule(dllPath, module)) {
        ++failedTests;
        std::cerr << "FAIL thumbnail provider could not be loaded from " << dllPath << '\n';
        return;
    }
    passOrFail("the thumbnail provider DLL exports the COM entry points", true);

    for (const auto& fixture : fixtures) {
        const auto bytes = readFile(fixture);
        const auto name = std::filesystem::path(fixture).filename().string();
        if (bytes.empty()) {
            ++failedTests;
            std::cerr << "FAIL thumbnail fixture is missing or empty: " << fixture << '\n';
            continue;
        }

        const auto rendered = renderThumbnail(module, bytes, 256);
        // 不放大是对的：资源管理器要的是「最长边不超过 cx」，一张 8x8 的图
        // 拉成 256 只会糊。所以这里只要求不超限、不空、不是全零像素。
        const bool usable = SUCCEEDED(rendered.hr) &&
            rendered.width > 0 && rendered.height > 0 &&
            rendered.width <= 256 && rendered.height <= 256 &&
            !rendered.allZero;

        std::string_view reason;
        if (isKnownThumbnailGap(name, reason)) {
            if (usable) {
                ++failedTests;
                std::cerr << "FAIL " << name << " now renders a thumbnail; remove it from "
                    << "thumbnailKnownGaps so the gap list stays honest\n";
            }
            else {
                ++passedTests;
                std::cout << "PASS " << name << " has no thumbnail, as documented ("
                    << reason << ")\n";
            }
            continue;
        }

        if (!usable) {
            ++failedTests;
            std::cerr << "FAIL the thumbnail provider renders " << name
                << ": hr=0x" << std::hex << static_cast<unsigned>(rendered.hr) << std::dec
                << " size=" << rendered.width << 'x' << rendered.height
                << " blank=" << (rendered.allZero ? "yes" : "no") << '\n';
        }
        else {
            ++passedTests;
            std::cout << "PASS the thumbnail provider renders " << name
                << " at " << rendered.width << 'x' << rendered.height << '\n';
        }
    }

    // 「出图了」不等于「出对了」：通道顺序搞反、通道错位这类错照样「有图」。
    // 语料里几组素材是同一张源图导出的不同格式，拿其中最可信的一个当基准逐像素比。
    //
    // 比之前要先还原预乘。交出去的位图是预乘过的，而有些格式压根没有透明通道
    // （Sun raster），直接比会差出 127——第一次写这条测试就是这么误判成
    // 「解码错了」的，实际是在拿预乘值和不透明值做比较。
    {
        const auto findFixture = [&fixtures](std::string_view fileName) {
            return std::find_if(fixtures.begin(), fixtures.end(),
                [fileName](const std::string& path) {
                    return std::filesystem::path(path).filename() == fileName;
                });
        };

        const auto comparePixels = [&](std::string_view referenceName,
            const std::vector<std::string_view>& candidates, uint32_t renderSize) {
            const auto referencePath = findFixture(referenceName);
            if (referencePath == fixtures.end()) {
                ++failedTests;
                std::cerr << "FAIL " << referenceName << " was not provided as a pixel reference\n";
                return;
            }

            const auto reference = renderThumbnail(module, readFile(*referencePath), renderSize);
            for (const auto candidateName : candidates) {
                const auto candidatePath = findFixture(candidateName);
                if (candidatePath == fixtures.end()) {
                    ++failedTests;
                    std::cerr << "FAIL comparison fixture was not provided: "
                        << candidateName << '\n';
                    continue;
                }

                const auto candidate = renderThumbnail(module, readFile(*candidatePath), renderSize);
                if (reference.pixels.empty() ||
                    candidate.pixels.size() != reference.pixels.size()) {
                    ++failedTests;
                    std::cerr << "FAIL " << candidateName << " does not decode to the same size as "
                        << referenceName << '\n';
                    continue;
                }

                // 还原预乘后再比 B/G/R；全透明的像素没有颜色可比，跳过
                const auto straighten = [](int premultiplied, int alpha) {
                    return alpha <= 0 ? 0 : std::clamp(premultiplied * 255 / alpha, 0, 255);
                };

                uint64_t total = 0;
                int worst = 0;
                size_t compared = 0;
                for (size_t offset = 0; offset + 3 < reference.pixels.size(); offset += 4) {
                    const int referenceAlpha = reference.pixels[offset + 3];
                    const int candidateAlpha = candidate.pixels[offset + 3];
                    if (referenceAlpha <= 8 || candidateAlpha <= 8) {
                        continue;
                    }
                    for (int channel = 0; channel < 3; ++channel) {
                        const int difference = std::abs(
                            straighten(reference.pixels[offset + channel], referenceAlpha) -
                            straighten(candidate.pixels[offset + channel], candidateAlpha));
                        total += static_cast<uint64_t>(difference);
                        worst = std::max(worst, difference);
                        ++compared;
                    }
                }

                if (compared == 0) {
                    ++failedTests;
                    std::cerr << "FAIL " << candidateName << " had no comparable opaque pixels\n";
                    continue;
                }

                const double mean = static_cast<double>(total) / compared;
                if (mean > 2.0 || worst > 24) {
                    ++failedTests;
                    std::cerr << "FAIL " << candidateName << " does not match " << referenceName
                        << " (mean " << mean << ", worst " << worst << ")\n";
                }
                else {
                    ++passedTests;
                    std::cout << "PASS " << candidateName << " decodes to the same picture as "
                        << referenceName << " (mean " << mean << ", worst " << worst << ")\n";
                }
            }
        };

        // common.* 是同一张 160x80 参考图导出的各种格式。基准用 PNG，它走 WIC 最可信。
        comparePixels("common.png",
            { "common.tga", "common.ras", "common.sr", "common.pcx" }, 160);

        // 三张 PSD 同源（ImageMagick 比 psd_8bit 和 psd_16bit 的 RMSE 是 0），
        // 和 common.* 不是同一张图，所以单独一组。
        // 16 位那张是 RLE 压缩的，stb 解它会从第二个通道起全部错位——出图但颜色是错的，
        // 这一条就是为了盯住「出的图对不对」而不只是「有没有图」。
        comparePixels("psd_8bit.psd", { "psd_16bit.psd", "psd_alpha.psd" }, 160);
    }

    const auto& first = fixtures.front();

    // 请求一个很小的尺寸也要出图：资源管理器的「小图标」视图就是这么要的
    {
        const auto small = renderThumbnail(module, readFile(first), 32);
        passOrFail("a 32-pixel request is honoured",
            SUCCEEDED(small.hr) && small.width > 0 && small.height > 0 &&
            small.width <= 32 && small.height <= 32 && !small.allZero);
    }

    // cx 为 0 必须失败而不是除零
    passOrFail("a zero-size request is rejected", FAILED(renderThumbnail(module, readFile(first), 0).hr));

    // 没 Initialize 就 GetThumbnail 要失败
    {
        IInitializeWithStream* initializer = createThumbnailProvider(module);
        IThumbnailProvider* provider = nullptr;
        HRESULT hr = S_OK;
        if (initializer && SUCCEEDED(initializer->QueryInterface(IID_IThumbnailProvider,
            reinterpret_cast<void**>(&provider))) && provider) {
            HBITMAP bitmap = nullptr;
            WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
            hr = provider->GetThumbnail(256, &bitmap, &alpha);
            if (bitmap)
                DeleteObject(bitmap);
            provider->Release();
        }
        if (initializer)
            initializer->Release();
        passOrFail("GetThumbnail before Initialize fails instead of reading uninitialized data",
            FAILED(hr));
    }

    // 同一个对象 Initialize 两次要被拒绝（COM 契约）
    {
        const auto bytes = readFile(first);
        IInitializeWithStream* initializer = createThumbnailProvider(module);
        HRESULT second = S_OK;
        if (initializer) {
            IStream* once = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
            IStream* again = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
            if (once && again) {
                initializer->Initialize(once, STGM_READ);
                second = initializer->Initialize(again, STGM_READ);
            }
            if (once) once->Release();
            if (again) again->Release();
            initializer->Release();
        }
        passOrFail("initializing twice is refused", FAILED(second));
    }

    // 空流和坏数据都要干净地失败，不能崩、不能返回一张空图
    {
        IInitializeWithStream* initializer = createThumbnailProvider(module);
        HRESULT hr = S_OK;
        if (initializer) {
            IStream* stream = SHCreateMemStream(nullptr, 0);
            if (stream) {
                hr = initializer->Initialize(stream, STGM_READ);
                stream->Release();
            }
            initializer->Release();
        }
        passOrFail("an empty stream is refused", FAILED(hr));
    }
    {
        std::vector<uint8_t> garbage(4096);
        for (size_t index = 0; index < garbage.size(); ++index)
            garbage[index] = static_cast<uint8_t>(index * 31 + 7);
        passOrFail("random bytes fail cleanly instead of producing a blank thumbnail",
            FAILED(renderThumbnail(module, garbage, 256).hr));
    }

    // 截断的文件同样不能崩。资源管理器会对正在下载的文件调缩略图。
    {
        auto truncated = readFile(first);
        truncated.resize(truncated.size() / 3);
        const auto rendered = renderThumbnail(module, truncated, 256);
        passOrFail("a truncated file is handled without crashing",
            FAILED(rendered.hr) || (rendered.width > 0 && rendered.height > 0));
    }

    // 不认识的接口要返回 E_NOINTERFACE，而不是硬塞一个指针回去
    {
        IInitializeWithStream* initializer = createThumbnailProvider(module);
        void* unexpected = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEAD));
        HRESULT hr = S_OK;
        if (initializer) {
            hr = initializer->QueryInterface(IID_IPersistFile, &unexpected);
            initializer->Release();
        }
        passOrFail("an unsupported interface is refused and the out pointer is cleared",
            hr == E_NOINTERFACE && unexpected == nullptr);
    }

    // 所有对象都放掉之后 DllCanUnloadNow 要说「可以卸了」，否则资源管理器
    // 会一直占着这个 DLL，升级时文件被锁住
    passOrFail("the module reports it can be unloaded once every object is released",
        module.canUnloadNow() == S_OK);
}

// ===== 打印的逐像素处理（PrintAdjustments.h）=====
// 这类错只有纸出来才看得见：界面不报错，预览图又小，偏一点根本看不出来。
// 以前 100% 的对比度配 50% 的亮度会让暗部算出 NaN（pow(负数, 非整数)），
// 转成字节的结果是平台说了算的——这里把每条边界都钉住。

void expectPrintAdjustments() {
    using namespace PrintAdjustments;

    // 100 / 100 是「不增不减」，必须一个像素都不动
    {
        const double contrast = contrastFactor(100);
        const double exponent = brightnessExponent(100);
        bool identity = true;
        for (int sample = 0; sample < 256; ++sample) {
            if (adjustSample(static_cast<uint8_t>(sample), contrast, exponent) != sample) {
                identity = false;
                break;
            }
        }
        passOrFail("brightness 100 and contrast 100 leave every sample untouched", identity);
    }

    // 扫一遍全部 201x201 种滑块组合：系数要有限，曲线要单调不降。
    // 曲线不单调意味着某一段被算坏了——以前暗部会经过 pow(负数, 非整数) = NaN，
    // 虽然 NaN 转整数正好也落到 0、结果看起来没变，但那是靠运气，换平台就不一定。
    // 现在先夹到 0~255 再做伽马，整条曲线都有定义。
    {
        bool healthy = true;
        for (uint32_t brightness = 0; brightness <= 200 && healthy; ++brightness) {
            for (uint32_t contrast = 0; contrast <= 200 && healthy; ++contrast) {
                const double contrastValue = contrastFactor(contrast);
                const double exponent = brightnessExponent(brightness);
                if (!std::isfinite(contrastValue) || !std::isfinite(exponent)) {
                    healthy = false;
                    break;
                }
                uint8_t previous = adjustSample(0, contrastValue, exponent);
                for (int sample = 1; sample < 256; ++sample) {
                    const uint8_t current = adjustSample(static_cast<uint8_t>(sample),
                        contrastValue, exponent);
                    if (current < previous) {
                        healthy = false;
                        break;
                    }
                    previous = current;
                }
            }
        }
        passOrFail("every slider combination yields a finite, non-decreasing curve", healthy);
    }

    // 对比度拉满、亮度压低，这正是以前会算出 NaN 的那一格。
    // 纯黑要留在黑，纯白要留在白，中灰不能塌成 0。
    {
        const double contrast = contrastFactor(200);
        const double exponent = brightnessExponent(50);
        const uint8_t black = adjustSample(0, contrast, exponent);
        const uint8_t white = adjustSample(255, contrast, exponent);
        const uint8_t light = adjustSample(200, contrast, exponent);
        passOrFail("maximum contrast with low brightness keeps black black and white white",
            black == 0 && white == 255 && light > 0);
    }

    // 对比度为 0 时整张图塌成中灰（128 对应的那个值），这是定义行为
    {
        const double contrast = contrastFactor(0);
        const double exponent = brightnessExponent(100);
        const uint8_t dark = adjustSample(0, contrast, exponent);
        const uint8_t bright = adjustSample(255, contrast, exponent);
        passOrFail("zero contrast collapses every sample to the same value", dark == bright);
    }

    // 亮度在合法区间内单调：数值大的输入出来不会更暗
    {
        bool monotonic = true;
        for (uint32_t brightness : { 1u, 50u, 100u, 150u, 199u }) {
            const double contrast = contrastFactor(100);
            const double exponent = brightnessExponent(brightness);
            for (int sample = 1; sample < 256 && monotonic; ++sample) {
                if (adjustSample(static_cast<uint8_t>(sample), contrast, exponent) <
                    adjustSample(static_cast<uint8_t>(sample - 1), contrast, exponent)) {
                    monotonic = false;
                }
            }
        }
        passOrFail("the brightness curve never makes a lighter sample darker", monotonic);
    }

    // 亮度越高整体越亮
    {
        const double contrast = contrastFactor(100);
        const uint8_t dim = adjustSample(128, contrast, brightnessExponent(50));
        const uint8_t normal = adjustSample(128, contrast, brightnessExponent(100));
        const uint8_t bright = adjustSample(128, contrast, brightnessExponent(150));
        passOrFail("raising the brightness slider brightens mid gray", dim < normal && normal < bright);
    }

    // 越界参数按文档回落（亮度夹到 1~199，对比度夹到 0~200），不能除零也不能爆
    passOrFail("out-of-range sliders fall back to the clamped ends",
        brightnessExponent(0) == brightnessExponent(1) &&
        brightnessExponent(1000) == brightnessExponent(199) &&
        contrastFactor(1000) == contrastFactor(200));

    // 整块像素：alpha 不能被动
    {
        std::vector<uint8_t> pixels{
            10, 20, 30, 77,
            200, 210, 220, 88
        };
        applyBrightnessContrast(pixels.data(), 2, 1, 4, 8, 180, 160);
        passOrFail("brightness and contrast leave the alpha channel alone",
            pixels[3] == 77 && pixels[7] == 88);
    }

    // 带步长（Mat 的 ROI 或者有行填充）时不能越过行尾去改别人的数据
    {
        constexpr int width = 2;
        constexpr int height = 2;
        constexpr size_t stride = 10;      // 每行 6 字节有效 + 4 字节填充
        std::vector<uint8_t> pixels(stride * height, 0xAA);
        applyBrightnessContrast(pixels.data(), width, height, 3, stride, 1, 200);
        bool paddingIntact = true;
        for (int y = 0; y < height; ++y) {
            for (size_t offset = width * 3; offset < stride; ++offset) {
                if (pixels[y * stride + offset] != 0xAA) {
                    paddingIntact = false;
                }
            }
        }
        passOrFail("row padding is not touched", paddingIntact);
    }

    // 烂输入不能崩
    {
        std::vector<uint8_t> pixels(12, 128);
        applyBrightnessContrast(nullptr, 2, 2, 3, 6, 100, 100);
        applyBrightnessContrast(pixels.data(), 0, 2, 3, 6, 100, 100);
        applyBrightnessContrast(pixels.data(), 2, 0, 3, 6, 100, 100);
        applyBrightnessContrast(pixels.data(), 2, 2, 1, 2, 100, 100);   // 单通道不处理
        passOrFail("degenerate buffers are ignored instead of crashing", pixels[0] == 128);
    }

    // 反相
    {
        std::vector<uint8_t> pixels{ 0, 128, 255, 40 };
        invertColors(pixels.data(), 1, 1, 4, 4);
        passOrFail("inverting flips BGR and keeps alpha",
            pixels[0] == 255 && pixels[1] == 127 && pixels[2] == 0 && pixels[3] == 40);
    }
    {
        std::vector<uint8_t> pixels{ 1, 2, 3 };
        invertColors(pixels.data(), 1, 1, 3, 3);
        invertColors(pixels.data(), 1, 1, 3, 3);
        passOrFail("inverting twice returns the original",
            pixels[0] == 1 && pixels[1] == 2 && pixels[2] == 3);
    }

    // 抖动：输出只能是 0 或 255
    {
        constexpr int width = 32;
        constexpr int height = 32;
        std::vector<uint8_t> gray(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                gray[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(x * 255 / (width - 1));

        floydSteinbergDither(gray.data(), width, height, width);
        const bool binary = std::all_of(gray.begin(), gray.end(),
            [](uint8_t value) { return value == 0 || value == 255; });
        passOrFail("dithering leaves only pure black and pure white", binary);
    }

    // 抖动要保住整体明暗：一张 50% 灰应当出来大约一半黑一半白
    {
        constexpr int width = 64;
        constexpr int height = 64;
        std::vector<uint8_t> gray(static_cast<size_t>(width) * height, 128);
        floydSteinbergDither(gray.data(), width, height, width);
        const size_t white = static_cast<size_t>(std::count(gray.begin(), gray.end(), 255));
        const double ratio = static_cast<double>(white) / gray.size();
        passOrFail("dithering a 50% gray keeps about half the dots white",
            ratio > 0.35 && ratio < 0.65);
    }

    // 纯黑和纯白不该被抖出噪点
    {
        constexpr int width = 16;
        constexpr int height = 16;
        std::vector<uint8_t> black(static_cast<size_t>(width) * height, 0);
        std::vector<uint8_t> white(static_cast<size_t>(width) * height, 255);
        floydSteinbergDither(black.data(), width, height, width);
        floydSteinbergDither(white.data(), width, height, width);
        passOrFail("dithering a flat black or white image adds no speckles",
            std::all_of(black.begin(), black.end(), [](uint8_t v) { return v == 0; }) &&
            std::all_of(white.begin(), white.end(), [](uint8_t v) { return v == 255; }));
    }

    // 抖动也要尊重步长，不能踩到行填充
    {
        constexpr int width = 8;
        constexpr int height = 8;
        constexpr size_t stride = 12;
        std::vector<uint8_t> gray(stride * height, 0x5A);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                gray[y * stride + x] = 128;

        floydSteinbergDither(gray.data(), width, height, stride);
        bool paddingIntact = true;
        for (int y = 0; y < height; ++y)
            for (size_t offset = width; offset < stride; ++offset)
                if (gray[y * stride + offset] != 0x5A)
                    paddingIntact = false;
        passOrFail("dithering respects the row stride", paddingIntact);
    }

    // 单像素、单行、单列都不能越界
    {
        std::vector<uint8_t> one{ 130 };
        floydSteinbergDither(one.data(), 1, 1, 1);
        std::vector<uint8_t> row(5, 130);
        floydSteinbergDither(row.data(), 5, 1, 5);
        std::vector<uint8_t> column(5, 130);
        floydSteinbergDither(column.data(), 1, 5, 1);
        floydSteinbergDither(nullptr, 4, 4, 4);
        passOrFail("dithering handles single pixels, rows, and columns",
            one[0] == 255 &&
            std::all_of(row.begin(), row.end(), [](uint8_t v) { return v == 0 || v == 255; }) &&
            std::all_of(column.begin(), column.end(), [](uint8_t v) { return v == 0 || v == 255; }));
    }
}
}

int main(int argc, char* argv[]) {
    // 缩略图组件内部会用到 COM 对象，和资源管理器一样先把 COM 起起来
    const HRESULT comReady = CoInitializeEx(nullptr,
        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    expectVideoSize("no motion-photo metadata", "Exif.Image.Make: DJI", 0);
    expectVideoSize("legacy offset followed by metadata", "Xmp.GCamera.MicroVideoOffset: 12345\nExif.Image.Make: DJI", 12345);
    expectVideoSize("legacy offset at end", "Xmp.GCamera.MicroVideoOffset: 12345", 12345);
    expectVideoSize("container length followed by metadata",
        "Item:Semantic: MotionPhoto\nItem:Length: 23947349\nExif.Photo.UserComment: oplus_8388608", 23947349);
    expectVideoSize("container length at end", "Item:Semantic: MotionPhoto\nItem:Length: 23947349", 23947349);
    expectVideoSize("missing length value", "Item:Semantic: MotionPhoto\nItem:Length: ", 0);
    expectVideoSize("non-numeric length", "Item:Semantic: MotionPhoto\nItem:Length: unknown", 0);
    expectVideoSize("overflowing length", "Item:Semantic: MotionPhoto\nItem:Length: 999999999999999999999999999999", 0);
    expectHdrChannelOrder();
    expectBilinearEnlargement();
    expectBackgroundRendering();
    expectOverlayLayout();
    expectZoomCommands();
    expectZoomPolicy();
    expectZoomEditPolicy();
    expectSlideshowPolicy();
    expectJpegQuality();
    expectColorSpaceName();
    expectImageHistogram();
    expectHistogramFromPixels();
    expectInfoPanelFields();
    expectInfoPanelOpacity();
    expectLoadingBadge();
    expectDecodeEstimate();
    expectImageViewTransform();
    expectViewerOptions();
    expectInitialWindowLayout();
    expectPresentationLayout();
    expectMonitorPlacement();
    expectEscapeBehavior();
    expectSettingLayout();
    expectHomeScreenLayout();
    expectTextRendering();
    expectImageInfoPresentation();
    expectWindowTitlePresentation();
    expectFileTargetConfig();
    expectFullscreenInfoBar();
    expectWheelInput();
    expectShortcutConfig();
    expectLivePhotoOptions();
    expectMotionTiming();
    expectLivePhotoBadge();
    expectExternalEditorConfig();
    expectRotationPersistence();
    expectRenamePolicy();
    expectFileAssociationNaming();
    expectPrintLayout();
    expectPrintAdjustments();
    expectSvgPreprocessor();
    expectLruCache();
    if (argc >= 2) {
        expectRealHdrChannelOrder(argv[1]);
    }
    else {
        ++failedTests;
        std::cerr << "FAIL real HDR fixture path was not provided\n";
    }
    if (argc >= 4) {
        expectSvgRerendersAtDisplayResolution(argv[2]);
        expectDrawioTextFallback(argv[3]);
    }
    else {
        failedTests += 4;
        std::cerr << "FAIL SVG regression fixture paths were not provided\n";
    }
    if (argc >= 23) {
        expectToolbarIcons({ argv[4], argv[5], argv[6], argv[7], argv[8], argv[9],
            argv[10], argv[11], argv[12], argv[13], argv[14], argv[15], argv[16],
            argv[17], argv[18], argv[19], argv[20], argv[21], argv[22] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL toolbar icon paths were not provided\n";
    }
    if (argc >= 26) {
        expectApplicationIcons({ argv[23], argv[24], argv[25] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL application icon paths were not provided\n";
    }

    if (argc >= 28) {
        std::vector<std::string> thumbnailFixtures;
        for (int index = 27; index < argc; ++index)
            thumbnailFixtures.emplace_back(argv[index]);
        expectThumbnailProvider(argv[26], thumbnailFixtures);
    }
    else {
        ++failedTests;
        std::cerr << "FAIL thumbnail provider DLL and fixture paths were not provided\n";
    }

    if (SUCCEEDED(comReady))
        CoUninitialize();

    std::cout << passedTests << " passed, " << failedTests << " failed\n";
    return failedTests == 0 ? 0 : 1;
}
