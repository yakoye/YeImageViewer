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
#include "ImageViewTransform.h"
#include "RotationStore.h"
#include "RenamePolicy.h"
#include "SettingCommand.h"
#include "SettingLayout.h"
#include "ShortcutConfig.h"
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
    passOrFail("immersive canvas is sixty percent black while framed canvas is opaque middle gray",
        BackgroundPolicy::windowCanvasPixel(true, true, theme) == BackgroundPolicy::PRESENTATION_TINT &&
        BackgroundPolicy::PRESENTATION_TINT == 0x99000000u &&
        BackgroundPolicy::windowCanvasPixel(false, true, theme) == BackgroundPolicy::FRAMED_CANVAS &&
        BackgroundPolicy::FRAMED_CANVAS == 0xFF7F7F7Fu);
    passOrFail("immersive canvas falls back to the opaque theme without alpha composition",
        BackgroundPolicy::windowCanvasPixel(true, false, theme) == theme);
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

void expectInitialWindowLayout() {
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
    constexpr std::array<int, 4> radioOptions{ 3, 3, 2, 2 };
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
    passOrFail("image information uses twelve-pixel adaptive compact and full cards",
        ImageInfoPresentation::LOGICAL_FONT_SIZE == 12 &&
        ImageInfoPresentation::LOGICAL_COMPACT_PANEL_WIDTH == 288 &&
        ImageInfoPresentation::LOGICAL_FULL_PANEL_WIDTH == 340 &&
        ImageInfoPresentation::useLightPalette(255, 255, 255) &&
        !ImageInfoPresentation::useLightPalette(32, 24, 16));
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
    passOrFail("window title separates position zoom dimensions size filename and rotation",
        title == L"[3/16] | 125% | 1514 × 857 px | 1.6 MiB | gpu心智图.png | 右转 90°");
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
    expectImageViewTransform();
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
