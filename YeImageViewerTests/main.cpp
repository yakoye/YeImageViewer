#include "MotionPhotoUtils.h"
#include "MonitorPlacement.h"
#include "BackgroundRenderer.h"
#include "BackgroundPolicy.h"
#include "EscapeBehavior.h"
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
#include "JpegQuality.h"
#include "ColorSpaceName.h"
#include "ImageHistogram.h"
#include "DecodeEstimate.h"
#include "ImageViewTransform.h"
#include "RotationStore.h"
#include "RenamePolicy.h"
#include "SettingCommand.h"
#include "SettingLayout.h"
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

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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
    passOrFail("previous, slideshow, and next form the centered primary toolbar group",
        toolbarPlayPause.x + toolbarPlayPause.width / 2 == width / 2 &&
        toolbarPrevious.x < toolbarPlayPause.x && toolbarPlayPause.x < toolbarNext.x);

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
    const auto model = ImageInfoPresentation::build(raw, true, "RGB · 24bpp",
        "Adobe RGB (1998)", 85);
    const auto hasBasic = [&model](std::string_view label, std::string_view value) {
        return std::ranges::any_of(model.basic, [&](const ImageInfoPresentation::Row& row) {
            return row.label == label && row.value == value;
            });
        };
    passOrFail("info panel shows the resolved color space and quality factor",
        hasBasic("色彩空间", "Adobe RGB (1998)") && hasBasic("质量因子", "约 85"));

    // EXIF 原始值 65535 不能再作为「色彩空间」重复列一遍——
    // 同一个标签出现两次、其中一个还是看不懂的数字，只会让人犯疑
    passOrFail("info panel drops the raw EXIF color space once it is resolved",
        std::ranges::none_of(model.details, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间";
            }));

    // 两项都很短，紧凑面板也要能看到
    const auto compact = ImageInfoPresentation::compactRows(model, true);
    passOrFail("info panel surfaces both fields in the compact layout",
        std::ranges::any_of(compact, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间"; }) &&
        std::ranges::any_of(compact, [](const ImageInfoPresentation::Row& row) {
            return row.label == "质量因子"; }));

    // 估不出质量因子（0）时不能显示「约 0」；色彩空间为空时同理
    const auto bare = ImageInfoPresentation::build(raw, true, "RGB · 24bpp", {}, 0);
    passOrFail("info panel omits the fields when nothing identifies them",
        std::ranges::none_of(bare.basic, [](const ImageInfoPresentation::Row& row) {
            return row.label == "质量因子" || row.label == "色彩空间"; }) &&
        // 这时 EXIF 原始值该照旧出现在照片信息里，不能一起丢掉
        std::ranges::any_of(bare.details, [](const ImageInfoPresentation::Row& row) {
            return row.label == "色彩空间"; }));

    // 英文界面用英文标签
    const auto english = ImageInfoPresentation::build(raw, false, "RGB · 24bpp", "sRGB", 92);
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
    bool allValid = paths.size() == 18;
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

    {
        std::ofstream corrupt(databasePath, std::ios::binary | std::ios::trunc);
        corrupt << "not a rotation database";
    }
    RotationStore corrupted(databasePath);
    passOrFail("a malformed rotation database fails safely without returning stale data",
        !corrupted.load() && corrupted.get(originalPath) == 0);

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

void expectEscapeBehavior() {
    passOrFail("Escape close preference closes directly from presentation",
        EscapeBehavior::resolve(true, false, false, true) == EscapeBehavior::Action::CloseImage);
    passOrFail("Escape close preference closes directly from fullscreen",
        EscapeBehavior::resolve(false, true, false, true) == EscapeBehavior::Action::CloseImage);
    passOrFail("Escape leaves presentation when close preference is disabled",
        EscapeBehavior::resolve(true, false, false, false) == EscapeBehavior::Action::ExitPresentation);
    passOrFail("Escape exits fullscreen when close preference is disabled",
        EscapeBehavior::resolve(false, true, true, false) == EscapeBehavior::Action::ExitFullScreen);
    passOrFail("Escape restores a maximized window when close preference is disabled",
        EscapeBehavior::resolve(false, false, true, false) == EscapeBehavior::Action::RestoreWindow);
    passOrFail("Escape does not close a normal window by default",
        EscapeBehavior::resolve(false, false, false, false) == EscapeBehavior::Action::Ignore);
    passOrFail("Escape closes a normal window only when explicitly enabled",
        EscapeBehavior::resolve(false, false, false, true) == EscapeBehavior::Action::CloseImage);
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
    // 必须与 SettingCommand::resolve 里的 optionCounts 逐项一致，否则命中判定会用错
    // 分段宽度。数量与 GENERAL_RADIOS 对齐由下面的静态断言兜住。
    constexpr std::array<int, 10> radioOptions{ 3, 3, 2, 2, 3, 3, 2, 2, 2, 4 };
    static_assert(radioOptions.size() == SettingLayout::GENERAL_RADIOS.size());
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
        ExternalEditorConfig::menuLabel(editors.front(), true) == L"在 画图" &&
        ExternalEditorConfig::menuLabel(editors.front(), false) == L"Open in 画图" &&
        ExternalEditorConfig::menuLabel(editors.front(), true).find(L'（') ==
            std::wstring::npos &&
        ExternalEditorConfig::menuLabel(editors.front(), true).find(L'(') ==
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
    const auto configFile = tempDirectory / ExternalEditorConfig::FILE_NAME;
    std::error_code ignored;
    std::filesystem::create_directories(tempDirectory, ignored);
    const bool saved = ExternalEditorConfig::save(configFile.wstring(), editors);
    const auto loaded = ExternalEditorConfig::load(configFile.wstring());
    passOrFail("external editor names and Unicode executable paths persist in a dedicated INI file",
        saved && loaded == editors && std::filesystem::file_size(configFile, ignored) > 2);
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
    const auto model = ImageInfoPresentation::build(rawInfo, true, "RGBA · 32bpp");

    passOrFail("full image information preserves the complete filename path and color mode",
        model.basic.size() == 6 &&
        model.basic[0].label == "文件名" &&
        model.basic[0].value == "这是一个非常长而且必须完整换行显示的图片文件名_sample.png" &&
        model.basic[1].label == "路径" && model.basic[1].value.starts_with("C:\\Pictures\\") &&
        model.basic[2].value == "PNG" && model.basic[3].value == "103.0 KiB" &&
        model.basic[4].value == "671 × 477 px" && model.basic[5].value == "RGBA · 32bpp");
    const auto compact = ImageInfoPresentation::compactRows(model, true);
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
        .rotation = L"右转 90°",
        });
    passOrFail("window title leads with the filename before position zoom dimensions size and rotation",
        title == L"gpu心智图.png | [3/16] | 125% | 1514 × 857 px | 1.6 MiB | 右转 90°");
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
        ShortcutConfig::keyName(ShortcutConfig::binding(0x71), true) == "F2");

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

}

int main(int argc, char* argv[]) {
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
    expectWheelInput();
    expectShortcutConfig();
    expectExternalEditorConfig();
    expectRotationPersistence();
    expectRenamePolicy();
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
    if (argc >= 22) {
        expectToolbarIcons({ argv[4], argv[5], argv[6], argv[7], argv[8], argv[9],
            argv[10], argv[11], argv[12], argv[13], argv[14], argv[15], argv[16],
            argv[17], argv[18], argv[19], argv[20], argv[21] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL toolbar icon paths were not provided\n";
    }
    if (argc >= 25) {
        expectApplicationIcons({ argv[22], argv[23], argv[24] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL application icon paths were not provided\n";
    }

    std::cout << passedTests << " passed, " << failedTests << " failed\n";
    return failedTests == 0 ? 0 : 1;
}
