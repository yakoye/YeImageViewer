#include "MotionPhotoUtils.h"
#include "ImageInterpolation.h"
#include "StbImageDecoder.h"
#include "SvgRenderer.h"

#include <algorithm>
#include <cmath>
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

    std::cout << passedTests << " passed, " << failedTests << " failed\n";
    return failedTests == 0 ? 0 : 1;
}
