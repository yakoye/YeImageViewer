#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>
#include "zlib.h"
#include "lcms2.h"

// 只读取 Photoshop 保存的最终合成图，不展开和重建图层。
// 按 Adobe 公开 PSD/PSB 规范实现；与主程序、缩略图共用同一边界与通道规则。
namespace PsdCompositeDecoder {
struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bgra, icc;
    bool hasAlpha = false;
};
class Reader {
    std::span<const uint8_t> bytes;
    size_t position = 0;
public:
    explicit Reader(std::span<const uint8_t> value) : bytes(value) {}
    size_t remaining() const { return bytes.size() - position; }
    std::span<const uint8_t> take(uint64_t count) {
        if (count > remaining()) throw std::runtime_error("PSD section truncated");
        auto result = bytes.subspan(position, static_cast<size_t>(count));
        position += static_cast<size_t>(count);
        return result;
    }
    uint64_t integer(size_t count) {
        uint64_t value = 0;
        for (uint8_t byte : take(count)) value = (value << 8) | byte;
        return value;
    }
};
inline bool signature(std::span<const uint8_t> bytes) {
    return bytes.size() >= 6 && std::memcmp(bytes.data(), "8BPS", 4) == 0;
}
inline std::vector<uint8_t> srgbProfile() {
    auto profile = cmsCreate_sRGBProfile();
    if (!profile) throw std::runtime_error("sRGB profile unavailable");
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    std::vector<uint8_t> result(size);
    const bool saved = cmsSaveProfileToMem(profile, result.data(), &size) != 0;
    cmsCloseProfile(profile);
    if (!saved) throw std::runtime_error("sRGB profile serialization failed");
    return result;
}
inline void unpackBits(std::span<const uint8_t> input, std::span<uint8_t> output) {
    size_t source = 0, target = 0;
    while (source < input.size()) {
        const int code = static_cast<int8_t>(input[source++]);
        if (code == -128) continue;
        const size_t count = code >= 0 ? code + 1 : 1 - code;
        if (count > output.size() - target) throw std::runtime_error("PSD RLE overrun");
        if (code >= 0) {
            if (count > input.size() - source) throw std::runtime_error("PSD RLE truncated literal");
            std::memcpy(output.data() + target, input.data() + source, count);
            source += count;
        } else {
            if (source == input.size()) throw std::runtime_error("PSD RLE truncated run");
            std::fill_n(output.data() + target, count, input[source++]);
        }
        target += count;
    }
    if (target != output.size()) throw std::runtime_error("PSD RLE incomplete row");
}
inline bool layerTransparency(std::span<const uint8_t> section, bool psb) {
    if (section.empty()) return false;
    Reader reader(section);
    auto info = reader.take(reader.integer(psb ? 8 : 4));
    if (info.size() >= 2) return (info[0] & 0x80) != 0;
    if (reader.remaining() < 4) return false;
    reader.take(reader.integer(4)); // 全局蒙版
    while (reader.remaining() >= 12) {
        auto sig = reader.take(4);
        if (std::memcmp(sig.data(), "8BIM", 4) && std::memcmp(sig.data(), "8B64", 4)) break;
        auto key = reader.take(4);
        const bool layers = std::memcmp(key.data(), "Lr16", 4) == 0 || std::memcmp(key.data(), "Lr32", 4) == 0;
        const auto length = reader.integer(psb && layers ? 8 : 4);
        auto data = reader.take(length);
        if (layers && data.size() >= 2) return (data[0] & 0x80) != 0;
        reader.take((4 - length % 4) % 4);
    }
    return false;
}
inline Image decode(std::span<const uint8_t> bytes, size_t memoryLimit = 1024ULL * 1024 * 1024,
                    bool compositeWhiteMatte = true) noexcept {
    try {
        if (!signature(bytes)) return {};
        Reader reader(bytes);
        reader.take(4);
        const auto version = reader.integer(2);
        if (version != 1 && version != 2) return {};
        const bool psb = version == 2;
        for (auto byte : reader.take(6)) if (byte) return {};
        const size_t channels = static_cast<size_t>(reader.integer(2));
        const auto height = reader.integer(4), width = reader.integer(4);
        const auto depth = reader.integer(2), mode = reader.integer(2);
        if (!width || !height || width > (psb ? 300000 : 30000) || height > (psb ? 300000 : 30000) ||
            channels == 0 || channels > 56 || (depth != 1 && depth != 8 && depth != 16 && depth != 32)) return {};
        const auto colorData = reader.take(reader.integer(4));
        Reader resources(reader.take(reader.integer(4)));
        std::vector<uint8_t> profile;
        int transparentIndex = -1;
        bool realMerged = true;
        while (resources.remaining()) {
            auto sig = resources.take(4);
            if (std::memcmp(sig.data(), "8BIM", 4)) return {};
            const auto id = resources.integer(2);
            const auto nameLength = resources.integer(1);
            resources.take(nameLength + ((nameLength + 1) % 2));
            const auto length = resources.integer(4);
            auto data = resources.take(length);
            resources.take(length % 2);
            if (id == 1039) {
                if (data.size() > 16 * 1024 * 1024) return {};
                profile.assign(data.begin(), data.end());
            }
            if (id == 1047 && data.size() == 2) transparentIndex = data[0] * 256 + data[1];
            if (id == 1057 && data.size() >= 5) realMerged = data[4] != 0;
        }
        auto layers = reader.take(reader.integer(psb ? 8 : 4));
        if (!realMerged) return {}; // 未保存兼容合成图不能把占位数据当完整成像。
        const size_t base = mode == 3 || mode == 9 ? 3 : mode == 4 ? 4 : 1;
        if ((mode != 0 && mode != 1 && mode != 2 && mode != 3 && mode != 4 && mode != 8 && mode != 9) ||
            channels < base || (depth == 1 && mode != 0) || (mode == 0 && depth != 1) ||
            (mode == 2 && (depth != 8 || colorData.size() != 768))) return {};
        const bool alpha = channels > base && layerTransparency(layers, psb);
        const size_t used = base + (alpha ? 1 : 0);
        const uint64_t pixels = width * height;
        const uint64_t rowBytes = (width * depth + 7) / 8, planeBytes = rowBytes * height;
        const uint64_t totalBytes = planeBytes * channels;
        if (pixels * 4 + totalBytes * 2 > memoryLimit) return {};
        const auto compression = reader.integer(2);
        std::vector<uint32_t> rowCounts;
        if (compression == 0) {
            if (totalBytes > reader.remaining()) return {};
        } else if (compression == 1) {
            if (height * channels * (psb ? 4 : 2) > reader.remaining()) return {};
            uint64_t encodedBytes = 0;
            for (size_t index = 0; index < height * channels; ++index) {
                const auto count = reader.integer(psb ? 4 : 2);
                if (count < ((rowBytes + 127) / 128) * 2) return {};
                encodedBytes += count;
                rowCounts.push_back(static_cast<uint32_t>(count));
            }
            if (encodedBytes > reader.remaining()) return {};
        } else if (compression != 2 && compression != 3) return {};
        if (compression >= 2 && totalBytes > uint64_t(reader.remaining()) * 1100) return {};
        std::vector<uint8_t> planes(static_cast<size_t>(planeBytes * used));
        if (compression == 0) {
            auto raw = reader.take(planes.size());
            std::memcpy(planes.data(), raw.data(), planes.size());
        } else if (compression == 1) {
            for (size_t index = 0; index < height * channels; ++index) {
                auto encoded = reader.take(rowCounts[index]);
                if (index < height * used)
                    unpackBits(encoded, std::span(planes).subspan(index * static_cast<size_t>(rowBytes), static_cast<size_t>(rowBytes)));
            }
        } else {
            // zlib 的输出长度和流终止都必须严格匹配；不接受截断流或靠补零成功。
            std::vector<uint8_t> unpacked(static_cast<size_t>(totalBytes));
            auto encoded = reader.take(reader.remaining());
            if (encoded.size() > UINT_MAX || unpacked.size() > UINT_MAX) return {};
            // 常见写入器既有整图一个 zlib 流，也有每平面一个连续 zlib 流。
            size_t consumed = 0, produced = 0;
            while (produced < unpacked.size()) {
                z_stream stream{};
                stream.next_in = const_cast<Bytef*>(encoded.data()) + consumed;
                stream.avail_in = static_cast<uInt>(encoded.size() - consumed);
                stream.next_out = unpacked.data() + produced;
                stream.avail_out = static_cast<uInt>(unpacked.size() - produced);
                if (inflateInit(&stream) != Z_OK) return {};
                const int status = inflate(&stream, Z_FINISH);
                const auto read = stream.total_in, written = stream.total_out;
                inflateEnd(&stream);
                if (status != Z_STREAM_END || read == 0 || written == 0) return {};
                consumed += read; produced += written;
            }
            if (consumed != encoded.size()) return {};
            std::copy_n(unpacked.data(), planes.size(), planes.data());
            if (compression == 3) {
                if (depth == 1) return {};
                std::vector<uint8_t> shuffled(static_cast<size_t>(rowBytes));
                for (size_t row = 0; row < height * used; ++row) {
                    uint8_t* data = planes.data() + row * static_cast<size_t>(rowBytes);
                    if (depth == 16) {
                        uint16_t previous = 0;
                        for (size_t x = 0; x < width; ++x) {
                            previous = static_cast<uint16_t>(previous + data[x * 2] * 256 + data[x * 2 + 1]);
                            data[x * 2] = static_cast<uint8_t>(previous >> 8); data[x * 2 + 1] = static_cast<uint8_t>(previous);
                        }
                    } else {
                        for (size_t x = 1; x < rowBytes; ++x) data[x] = static_cast<uint8_t>(data[x] + data[x - 1]);
                        if (depth == 32) {
                            std::copy_n(data, static_cast<size_t>(rowBytes), shuffled.data());
                            for (size_t x = 0; x < width; ++x)
                                for (size_t b = 0; b < 4; ++b) data[x * 4 + b] = shuffled[b * width + x];
                        }
                    }
                }
            }
        }
        Image result;
        result.width = static_cast<uint32_t>(width); result.height = static_cast<uint32_t>(height);
        result.hasAlpha = alpha || transparentIndex >= 0;
        result.bgra.resize(static_cast<size_t>(pixels * 4), 255);
        const auto sample = [&](size_t channel, size_t index) -> double {
            const uint8_t* p = planes.data() + channel * static_cast<size_t>(planeBytes);
            if (depth == 1) return (p[(index / width) * rowBytes + (index % width) / 8] & (128 >> (index % width % 8))) ? 0.0 : 1.0;
            p += index * static_cast<size_t>(depth / 8);
            if (depth == 8) return p[0] / 255.0;
            if (depth == 16) return (p[0] * 256 + p[1]) / 65535.0;
            const uint32_t value = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
            const float value32 = std::bit_cast<float>(value);
            return std::isfinite(value32) ? value32 : 0.0;
        };
        const auto byte = [](double value) {return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));};
        // 部分第三方写入器存直通RGB而非白色衬底。白衬底必须满足每个半透明
        // 色值 >= 1-alpha（允许两次编码量化的1 LSB）；全图存在反例就不能撤衬底。
        // 这检测的是编码恒等式，不用文件名、尺寸或某个特定位置的颜色猜来源。
        bool undoWhiteMatte=compositeWhiteMatte&&alpha&&mode==3;
        if(undoWhiteMatte) {
            const double tolerance=depth==8?1.0/255:depth==16?1.0/65535:1e-6;
            for(size_t index=0;index<pixels&&undoWhiteMatte;++index) {
                const double a=sample(base,index);
                if(a<=0||a>=1)continue;
                for(size_t c=0;c<3;++c)if(sample(c,index)+a<1-tolerance){undoWhiteMatte=false;break;}
            }
        }
        std::vector<uint8_t> device(static_cast<size_t>(width) * (mode == 4 ? 4 : 1));
        std::vector<double> lab(mode == 9 ? static_cast<size_t>(width) * 3 : 0);
        std::vector<uint8_t> converted(static_cast<size_t>(width) * 3);
        cmsHPROFILE source = profile.empty() ? nullptr : cmsOpenProfileFromMem(profile.data(), static_cast<cmsUInt32Number>(profile.size()));
        const bool deviceConvert = mode == 4 || mode == 9 || ((mode == 1 || mode == 8) && source);
        if (mode == 9 && !source) source = cmsCreateLab4Profile(nullptr);
        cmsHPROFILE target = deviceConvert ? cmsCreate_sRGBProfile() : nullptr;
        const cmsColorSpaceSignature requiredSpace = mode == 4 ? cmsSigCmykData : mode == 9 ? cmsSigLabData : cmsSigGrayData;
        cmsHTRANSFORM transform = deviceConvert && source && target && cmsGetColorSpace(source) == requiredSpace ?
            cmsCreateTransform(source, mode == 4 ? TYPE_CMYK_8 : mode == 9 ? TYPE_Lab_DBL : TYPE_GRAY_8,
                target, TYPE_BGR_8, INTENT_PERCEPTUAL, 0) : nullptr;
        // 对整个扫描线做色彩转换，而不是每个像素建立一次变换。
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                const size_t index = y * static_cast<size_t>(width) + x, dst = index * 4;
                if (mode == 4) {
                    for (size_t c = 0; c < 4; ++c) device[x * 4 + c] = byte(1.0 - sample(c, index));
                    result.bgra[dst] = byte(sample(2, index) * sample(3, index));
                    result.bgra[dst + 1] = byte(sample(1, index) * sample(3, index));
                    result.bgra[dst + 2] = byte(sample(0, index) * sample(3, index));
                } else if (mode == 9) {
                    lab[x * 3] = sample(0, index) * 100;
                    lab[x * 3 + 1] = sample(1, index) * 255 - 128;
                    lab[x * 3 + 2] = sample(2, index) * 255 - 128;
                } else if (mode == 3) {
                    // Photoshop最终RGB合成图带白色衬底，图层本身的栅格不带。
                    // 在原位深浮点精度下撤掉衬底，不能先量化为8位再反算。
                    const double a=alpha?std::clamp(sample(base,index),0.0,1.0):1.0;
                    const auto color=[&](size_t c) {
                        const double value=sample(c,index);
                        if(!undoWhiteMatte)return byte(value);
                        if(a<=0)return uint8_t(0);
                        if(depth==8||depth==16) {
                            const int maximum=depth==8?255:65535;
                            const int ac=static_cast<int>(std::lround(a*maximum));
                            const int cc=static_cast<int>(std::lround(value*maximum));
                            // 整数域先相减，避免 .5 因归一化减法误差变成 .499999…。
                            return byte(double(cc+ac-maximum)/ac);
                        }
                        return byte((value+a-1)/a);
                    };
                    result.bgra[dst] = color(2); result.bgra[dst + 1] = color(1); result.bgra[dst + 2] = color(0);
                } else if (mode == 2) {
                    const size_t palette = byte(sample(0, index));
                    result.bgra[dst] = colorData[512 + palette]; result.bgra[dst + 1] = colorData[256 + palette]; result.bgra[dst + 2] = colorData[palette];
                    if (static_cast<int>(palette) == transparentIndex) result.bgra[dst + 3] = 0;
                } else {
                    device[x] = byte(sample(0, index));
                    result.bgra[dst] = result.bgra[dst + 1] = result.bgra[dst + 2] = device[x];
                }
                if (alpha) result.bgra[dst + 3] = byte(sample(base, index));
            }
            if (transform) {
                cmsDoTransform(transform, mode == 9 ? static_cast<const void*>(lab.data()) : device.data(), converted.data(), static_cast<cmsUInt32Number>(width));
                for (size_t x = 0; x < width; ++x) std::copy_n(converted.data() + x * 3, 3, result.bgra.data() + (y * static_cast<size_t>(width) + x) * 4);
            }
        }
        if (transform) cmsDeleteTransform(transform);
        if (source) cmsCloseProfile(source);
        if (target) cmsCloseProfile(target);
        if (mode == 9 && !transform) return {};
        result.icc = deviceConvert ? srgbProfile() : std::move(profile);
        return result;
    } catch (...) { return {}; }
}
} // namespace PsdCompositeDecoder
