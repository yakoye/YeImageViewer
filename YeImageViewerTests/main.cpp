#include "MotionPhotoUtils.h"
#include "MonitorPlacement.h"
#include "BackgroundRenderer.h"
#include "BackgroundPolicy.h"
#include "EscapeBehavior.h"
#include "ImageInterpolation.h"
#include "InitialWindowLayout.h"
#include "ImageInfoPresentation.h"
#include "PresentationLayout.h"
#include "OverlayLayout.h"
#include "ImageViewTransform.h"
#include "RotationStore.h"
#include "SettingLayout.h"
#include "TextRenderingPolicy.h"
#include "WheelInput.h"
#include "StbImageDecoder.h"
#include "SvgRenderer.h"

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
    constexpr auto toolbarNext = OverlayLayout::toolbarNextRect(width, height);
    constexpr auto toolbar = OverlayLayout::toolbarRect(width, height);
    constexpr auto previous = OverlayLayout::previousImageIconRect(width, height);
    constexpr auto next = OverlayLayout::nextImageIconRect(width, height);
    constexpr auto close = OverlayLayout::presentationCloseRect(width, height);

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
    passOrFail("side navigation buttons match the reference size and margins",
        previous.width == 40 && previous.height == 40 && previous.x == 16 &&
        next.width == 40 && next.height == 40 && next.x + next.width == width - 16 &&
        previous.y == 280 && next.y == 280);
    passOrFail("toolbar hit testing maps every reference action",
        hitCenter(OverlayLayout::toolbarPreviousRect(width, height)) == OverlayLayout::Hit::ToolbarPreviousImage &&
        hitCenter(OverlayLayout::toolbarNextRect(width, height)) == OverlayLayout::Hit::ToolbarNextImage &&
        hitCenter(OverlayLayout::rotateLeftRect(width, height)) == OverlayLayout::Hit::RotateLeft &&
        hitCenter(OverlayLayout::rotateRightRect(width, height)) == OverlayLayout::Hit::RotateRight &&
        hitCenter(OverlayLayout::flipHorizontalRect(width, height)) == OverlayLayout::Hit::FlipHorizontal &&
        hitCenter(OverlayLayout::flipVerticalRect(width, height)) == OverlayLayout::Hit::FlipVertical &&
        hitCenter(OverlayLayout::zoomFitRect(width, height)) == OverlayLayout::Hit::ZoomFit &&
        hitCenter(OverlayLayout::zoomActualRect(width, height)) == OverlayLayout::Hit::ZoomActual &&
        hitCenter(OverlayLayout::fullscreenRect(width, height)) == OverlayLayout::Hit::Fullscreen &&
        hitCenter(OverlayLayout::favoriteRect(width, height)) == OverlayLayout::Hit::Favorite &&
        hitCenter(OverlayLayout::copyImageRect(width, height)) == OverlayLayout::Hit::CopyImage &&
        hitCenter(OverlayLayout::deleteImageRect(width, height)) == OverlayLayout::Hit::DeleteImage &&
        hitCenter(OverlayLayout::settingsRect(width, height)) == OverlayLayout::Hit::Settings &&
        hitCenter(OverlayLayout::zoomOutRect(width, height)) == OverlayLayout::Hit::ZoomOut &&
        hitCenter(OverlayLayout::zoomInRect(width, height)) == OverlayLayout::Hit::ZoomIn);
    passOrFail("overlay reveal regions stay limited to visible controls",
        OverlayLayout::toolbarRevealRect(width, height).x == toolbar.x &&
        OverlayLayout::toolbarRevealRect(width, height).width == toolbar.width &&
        OverlayLayout::hitTest(width, height, 5, 590) == OverlayLayout::Hit::None &&
        OverlayLayout::hitTest(width, height, 40, 250) == OverlayLayout::Hit::None &&
        hitCenter(previous) == OverlayLayout::Hit::EdgePreviousImage &&
        hitCenter(next) == OverlayLayout::Hit::EdgeNextImage);
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
        !wide.portrait && !wide.extendsBelowViewport);

    const auto tall = PresentationLayout::calculate(723, 1130, 1920, 1020, 120);
    passOrFail("portrait images preserve readable logical 100 percent and may overflow vertically",
        std::abs(tall.scale - 1.25) < 0.000001 &&
        tall.renderedWidth == 904 && tall.renderedHeight == 1413 &&
        tall.imageLeft == 508 && tall.imageTop == -216 &&
        tall.portrait && tall.extendsBelowViewport);

    const auto veryWidePortrait = PresentationLayout::calculate(2000, 3000, 1920, 1020, 120);
    passOrFail("portrait images shrink only when their width exceeds the preview limit",
        std::abs(veryWidePortrait.scale - 1728.0 / 2000.0) < 0.000001 &&
        veryWidePortrait.renderedWidth == 1728 &&
        veryWidePortrait.renderedHeight == 2592);

    const auto smallWindow = PresentationLayout::calculateWindowed(small, 1920, 1020);
    const auto wideWindow = PresentationLayout::calculateWindowed(wide, 1920, 1020);
    const auto tallWindow = PresentationLayout::calculateWindowed(tall, 1920, 1020);
    passOrFail("leaving presentation mode wraps the window client around the displayed image",
        smallWindow.clientWidth == 839 && smallWindow.clientHeight == 596 &&
        wideWindow.clientWidth == 1486 && wideWindow.clientHeight == 841);
    passOrFail("an overflowing portrait keeps its zoom and uses a centered scrollable viewport",
        tallWindow.clientWidth == 904 && tallWindow.clientHeight == 918 &&
        tallWindow.initialSlideY == 1);
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
    bool allValid = paths.size() == 16;
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

void expectEscapeBehavior() {
    passOrFail("Escape exits fullscreen before considering close preference",
        EscapeBehavior::resolve(true, true, true) == EscapeBehavior::Action::ExitFullScreen);
    passOrFail("Escape restores a maximized window before considering close preference",
        EscapeBehavior::resolve(false, true, true) == EscapeBehavior::Action::RestoreWindow);
    passOrFail("Escape does not close a normal window by default",
        EscapeBehavior::resolve(false, false, false) == EscapeBehavior::Action::Ignore);
    passOrFail("Escape closes a normal window only when explicitly enabled",
        EscapeBehavior::resolve(false, false, true) == EscapeBehavior::Action::CloseImage);
}

void expectSettingLayout() {
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
    passOrFail("help shortcuts use twelve separated table rows",
        SettingLayout::HELP_ITEMS.size() == 12 &&
        SettingLayout::HELP_ITEMS.front().y < SettingLayout::HELP_ITEMS.back().y &&
        SettingLayout::helpItemsAreSeparated());
    passOrFail("only overflowing settings pages enable a compact scrollbar",
        SettingLayout::maxScrollOffset(SettingLayout::GENERAL_CONTENT_HEIGHT) == 0 &&
        SettingLayout::maxScrollOffset(SettingLayout::ABOUT_CONTENT_HEIGHT) == 0 &&
        SettingLayout::maxScrollOffset(SettingLayout::HELP_CONTENT_HEIGHT) > 0 &&
        SettingLayout::scrollbarThumbHeight(SettingLayout::HELP_CONTENT_HEIGHT) >= 32 &&
        SettingLayout::scrollbarThumbHeight(SettingLayout::HELP_CONTENT_HEIGHT) <
            SettingLayout::CONTENT_VIEW_HEIGHT);
    passOrFail("settings scroll offsets are clamped to the content bounds",
        SettingLayout::clampScrollOffset(SettingLayout::HELP_CONTENT_HEIGHT, -10) == 0 &&
        SettingLayout::clampScrollOffset(SettingLayout::HELP_CONTENT_HEIGHT, 10000) ==
            SettingLayout::maxScrollOffset(SettingLayout::HELP_CONTENT_HEIGHT));
}

void expectTextRendering() {
    passOrFail("logical text size scales continuously with monitor DPI",
        TextRenderingPolicy::scaledPixelSize(18, 96) == 18 &&
        TextRenderingPolicy::scaledPixelSize(18, 120) == 23 &&
        TextRenderingPolicy::scaledPixelSize(18, 144) == 27 &&
        TextRenderingPolicy::scaledPixelSize(18, 192) == 36);
    passOrFail("immersive EXIF keeps its legacy size and rendering strategy",
        TextRenderingPolicy::legacyImmersiveExifPixelSize(96) == 16 &&
        TextRenderingPolicy::legacyImmersiveExifPixelSize(144) == 24 &&
        TextRenderingPolicy::legacyImmersiveExifPixelSize(192) == 32);
    passOrFail("non-adaptive interface text uses native Windows ClearType",
        TextRenderingPolicy::usesNativeClearType(false, true) &&
        !TextRenderingPolicy::usesNativeClearType(true, true) &&
        !TextRenderingPolicy::usesNativeClearType(false, false));
    passOrFail("glyph antialiasing keeps endpoints and strengthens intermediate coverage",
        TextRenderingPolicy::enhanceCoverage(0) == 0 &&
        TextRenderingPolicy::enhanceCoverage(64) > 64 &&
        TextRenderingPolicy::enhanceCoverage(128) > 128 &&
        TextRenderingPolicy::enhanceCoverage(255) == 255);
}

void expectImageInfoPresentation() {
    constexpr std::string_view rawInfo =
        "路径: C:\\Pictures\\sample.png\n"
        "大小: 103.0 KiB\n"
        "分辨率: 671x477\n"
        "原始日期时间: 2026-08-28 10:20:30\n"
        "型号: Sample Camera\n"
        "制造商: Sample Maker\n"
        "镜头型号: 24-70mm\n"
        "曝光时间: 1/125 s\n"
        "光圈值: F2.8\n"
        "ISO感光度: 200\n"
        "Xmp.xmp.CreatorTool: noisy raw metadata\n"
        "Exif.Photo.MakerNote: private binary payload\n";
    const auto model = ImageInfoPresentation::build(rawInfo, true);

    passOrFail("image information keeps four immediately useful basic fields",
        model.basic.size() == 4 &&
        model.basic[0].label == "文件名" && model.basic[0].value == "sample.png" &&
        model.basic[1].value == "PNG" && model.basic[2].value == "103.0 KiB" &&
        model.basic[3].value == "671 × 477 px");
    passOrFail("image information caps details and omits raw XMP and MakerNote noise",
        model.details.size() == ImageInfoPresentation::MAX_DETAIL_ROWS &&
        std::ranges::none_of(model.details, [](const ImageInfoPresentation::Row& row) {
            return row.label.contains("Xmp") || row.value.contains("Xmp") ||
                row.label.contains("MakerNote") || row.value.contains("MakerNote") ||
                row.value.contains("CreatorTool");
            }));
    passOrFail("image information uses a compact sixty-percent black card",
        ImageInfoPresentation::blendBgra(0xFFFFFFFFu,
            ImageInfoPresentation::PANEL_BACKGROUND) == 0xFF666666u &&
        ImageInfoPresentation::logicalPanelHeight(model) <= 460);
}

void expectWheelInput() {
    constexpr int panStep = 96;
    passOrFail("ordinary wheel keeps the existing context-sensitive behavior",
        WheelInput::resolve(0, 120, panStep).intent == WheelInput::Intent::Default &&
        WheelInput::resolve(0, -120, panStep).intent == WheelInput::Intent::Default);
    passOrFail("Shift wheel pans a tall image vertically in both directions",
        WheelInput::resolve(WheelInput::SHIFT_FLAG, 120, panStep).intent == WheelInput::Intent::PanVertical &&
        WheelInput::resolve(WheelInput::SHIFT_FLAG, 120, panStep).verticalDelta == panStep &&
        WheelInput::resolve(WheelInput::SHIFT_FLAG, -120, panStep).verticalDelta == -panStep);
    passOrFail("Ctrl wheel switches to previous or next image",
        WheelInput::resolve(WheelInput::CONTROL_FLAG, 120, panStep).intent == WheelInput::Intent::PreviousImage &&
        WheelInput::resolve(WheelInput::CONTROL_FLAG, -120, panStep).intent == WheelInput::Intent::NextImage);
    passOrFail("Ctrl takes priority when Ctrl and Shift are both held",
        WheelInput::resolve(WheelInput::CONTROL_FLAG | WheelInput::SHIFT_FLAG, 120, panStep).intent ==
            WheelInput::Intent::PreviousImage);
    passOrFail("zero wheel delta does not enqueue an action",
        WheelInput::resolve(WheelInput::CONTROL_FLAG | WheelInput::SHIFT_FLAG, 0, panStep).intent ==
            WheelInput::Intent::Default);
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
    expectImageViewTransform();
    expectInitialWindowLayout();
    expectPresentationLayout();
    expectMonitorPlacement();
    expectEscapeBehavior();
    expectSettingLayout();
    expectTextRendering();
    expectImageInfoPresentation();
    expectWheelInput();
    expectRotationPersistence();
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
    if (argc >= 20) {
        expectToolbarIcons({ argv[4], argv[5], argv[6], argv[7], argv[8], argv[9],
            argv[10], argv[11], argv[12], argv[13], argv[14], argv[15], argv[16],
            argv[17], argv[18], argv[19] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL toolbar icon paths were not provided\n";
    }
    if (argc >= 23) {
        expectApplicationIcons({ argv[20], argv[21], argv[22] });
    }
    else {
        ++failedTests;
        std::cerr << "FAIL application icon paths were not provided\n";
    }

    std::cout << passedTests << " passed, " << failedTests << " failed\n";
    return failedTests == 0 ? 0 : 1;
}
