#pragma once
#include <algorithm>
#include <cstdint>
#include <climits>
#include <cstring>
#include <span>
#include <vector>
#include <ppl.h>
#include "lcms2.h"

namespace CmykColor {
inline bool adobe(std::span<const uint8_t> bytes) {
    size_t offset = 2;
    while (offset + 1 < bytes.size()) {
        if (bytes[offset++] != 255) return false;
        while (offset < bytes.size() && bytes[offset] == 255) ++offset;
        if (offset == bytes.size()) return false;
        const auto marker = bytes[offset++];
        if (marker == 218 || marker == 217) return false;
        if (marker == 1 || (marker >= 208 && marker <= 215)) continue;
        if (bytes.size() - offset < 2) return false;
        const size_t length = bytes[offset] * 256 + bytes[offset + 1];
        if (length < 2 || length > bytes.size() - offset) return false;
        if (marker == 238 && length >= 14 && std::memcmp(bytes.data() + offset + 2, "Adobe", 5) == 0) return true;
        offset += length;
    }
    return false;
}
inline bool jpeg(std::span<const uint8_t> bytes) {
    if (bytes.size() < 4 || bytes[0] != 255 || bytes[1] != 216) return false;
    size_t offset = 2;
    while (offset + 1 < bytes.size()) {
        if (bytes[offset++] != 255) return false;
        while (offset < bytes.size() && bytes[offset] == 255) ++offset;
        if (offset == bytes.size()) return false;
        const uint8_t marker = bytes[offset++];
        if (marker == 218 || marker == 217) return false;
        if (marker == 1 || (marker >= 208 && marker <= 215)) continue;
        if (bytes.size() - offset < 2) return false;
        const size_t length = bytes[offset] * 256 + bytes[offset + 1];
        if (length < 2 || length > bytes.size() - offset) return false;
        if (marker >= 192 && marker <= 207 && marker != 196 && marker != 200 && marker != 204)
            return length >= 8 && bytes[offset + 7] == 4;
        offset += length;
    }
    return false;
}
// 输入是已由解码器统一为「0=无墨」的 CMYK；输出始终 sRGB/BGRA，不丢 K 通道。
inline std::vector<uint8_t> convert(std::span<const uint8_t> input, size_t width, size_t height,
    std::span<const uint8_t> profile, unsigned bits = 8) {
    if ((bits != 8 && bits != 16) || !width || !height || width > SIZE_MAX / height / 4 / (bits / 8) || input.size() != width * height * 4 * (bits / 8) ||
        width > UINT32_MAX || height > INT_MAX) return {};
    std::vector<uint8_t> output(width * height * 4, 255);
    cmsHPROFILE source = profile.empty() || profile.size() > 16 * 1024 * 1024 ? nullptr :
        cmsOpenProfileFromMem(profile.data(), static_cast<cmsUInt32Number>(profile.size()));
    cmsHPROFILE target = cmsCreate_sRGBProfile();
    cmsHTRANSFORM transform = source && target && cmsGetColorSpace(source) == cmsSigCmykData ?
        cmsCreateTransform(source, bits == 16 ? TYPE_CMYK_16 : TYPE_CMYK_8, target, TYPE_BGRA_8, INTENT_PERCEPTUAL, cmsFLAGS_NOCACHE) : nullptr;
    const auto row = [&](int y) {
        const auto* in = input.data() + static_cast<size_t>(y) * width * 4 * (bits / 8);
        auto* out = output.data() + static_cast<size_t>(y) * width * 4;
        if (transform) cmsDoTransform(transform, in, out, static_cast<cmsUInt32Number>(width));
        for (size_t x = 0; x < width; ++x) {
            if (!transform) {
                const auto sample = [&](size_t c) {
                    if (bits == 8) return int(in[x * 4 + c]);
                    uint16_t value = 0;
                    std::memcpy(&value, in + (x * 4 + c) * 2, 2);
                    return (int(value) + 128) / 257;
                };
                out[x * 4] = static_cast<uint8_t>(((255 - sample(2)) * (255 - sample(3)) + 127) / 255);
                out[x * 4 + 1] = static_cast<uint8_t>(((255 - sample(1)) * (255 - sample(3)) + 127) / 255);
                out[x * 4 + 2] = static_cast<uint8_t>(((255 - sample(0)) * (255 - sample(3)) + 127) / 255);
            }
            out[x * 4 + 3] = 255;
        }
    };
    if (width * height > 2 * 1024 * 1024) concurrency::parallel_for(0, static_cast<int>(height), row);
    else for (int y = 0; y < static_cast<int>(height); ++y) row(y);
    if (transform) cmsDeleteTransform(transform);
    if (source) cmsCloseProfile(source);
    if (target) cmsCloseProfile(target);
    return output;
}
}
