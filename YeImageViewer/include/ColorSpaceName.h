#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// 识别图片的色彩空间名，优先从内嵌 ICC 配置文件读，其次退回 EXIF 标签。
//
// 为什么不能只看 EXIF 的 ColorSpace：它只有三种取值——1 表示 sRGB、2 表示 Adobe RGB
// （非标准但常见）、65535 表示"未校准"。相机导出 Adobe RGB 时普遍写 65535 再靠 ICC
// 说明，只读 EXIF 会把 Adobe RGB 显示成"未校准"，等于没说。
//
// ICC 里的 'desc' 标签存的就是人类可读的配置文件名（"sRGB IEC61966-2.1"、
// "Adobe RGB (1998)"、"Display P3" 等），是最可靠的来源。
namespace ColorSpaceName {

    inline uint32_t readBE32(std::span<const uint8_t> data, std::size_t offset) {
        if (offset + 4 > data.size())
            return 0;
        return (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
    }

    inline std::string sanitize(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (const char ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            if (byte == 0)
                break;
            // 控制字符会把单行显示撑乱，统一换成空格
            result.push_back(byte < 0x20 ? ' ' : ch);
        }
        const auto first = result.find_first_not_of(' ');
        if (first == std::string::npos)
            return {};
        const auto last = result.find_last_not_of(' ');
        return result.substr(first, last - first + 1);
    }

    // 从 ICC 配置文件里取 'desc' 标签。
    // 结构：128 字节头 → 标签数(BE32) → 每条 12 字节(签名/偏移/长度) → 各标签数据。
    // v2 的 desc 是 textDescriptionType（ASCII），v4 是 mluc（UTF-16BE 多语言）。
    inline std::string fromIccProfile(std::span<const uint8_t> icc) {
        if (icc.size() < 132)
            return {};
        const uint32_t declaredSize = readBE32(icc, 0);
        // 声明长度和实际长度差太多说明这块数据不完整，不要按它的偏移乱跳
        if (declaredSize != 0 && declaredSize > icc.size())
            return {};

        const uint32_t tagCount = readBE32(icc, 128);
        if (tagCount == 0 || tagCount > 1024)
            return {};

        for (uint32_t i = 0; i < tagCount; ++i) {
            const std::size_t entry = 132 + static_cast<std::size_t>(i) * 12;
            if (entry + 12 > icc.size())
                break;
            const uint32_t signature = readBE32(icc, entry);
            if (signature != 0x64657363u)   // 'desc'
                continue;

            const uint32_t offset = readBE32(icc, entry + 4);
            const uint32_t size = readBE32(icc, entry + 8);
            if (offset + 12 > icc.size() || size < 12 ||
                static_cast<std::size_t>(offset) + size > icc.size())
                return {};

            const uint32_t type = readBE32(icc, offset);
            if (type == 0x64657363u) {
                // v2 textDescriptionType：sig(4) reserved(4) asciiCount(4) ascii...
                const uint32_t asciiCount = readBE32(icc, offset + 8);
                if (asciiCount == 0 || asciiCount > size)
                    return {};
                const std::size_t start = offset + 12;
                const std::size_t length = std::min<std::size_t>(asciiCount, icc.size() - start);
                return sanitize(std::string_view(
                    reinterpret_cast<const char*>(icc.data() + start), length));
            }
            if (type == 0x6D6C7563u) {
                // v4 mluc：sig(4) reserved(4) recordCount(4) recordSize(4) 然后是记录
                const uint32_t recordCount = readBE32(icc, offset + 8);
                if (recordCount == 0)
                    return {};
                const std::size_t record = offset + 16;
                if (record + 12 > icc.size())
                    return {};
                const uint32_t textLength = readBE32(icc, record + 4);
                const uint32_t textOffset = readBE32(icc, record + 8);
                const std::size_t start = static_cast<std::size_t>(offset) + textOffset;
                if (textLength == 0 || start + textLength > icc.size())
                    return {};
                // UTF-16BE。配置文件名都是 ASCII 范围，取低字节即可；
                // 真出现非 ASCII 就跳过那个字符，不要吐出乱码。
                std::string ascii;
                ascii.reserve(textLength / 2);
                for (std::size_t k = 0; k + 1 < textLength; k += 2) {
                    const uint32_t code = (static_cast<uint32_t>(icc[start + k]) << 8) |
                        icc[start + k + 1];
                    if (code == 0)
                        break;
                    if (code < 0x80)
                        ascii.push_back(static_cast<char>(code));
                }
                return sanitize(ascii);
            }
            return {};
        }
        return {};
    }

    // EXIF 的 ColorSpace 取值。1 = sRGB，2 = Adobe RGB（非标准但常见），
    // 65535 = 未校准（多数相机导出 Adobe RGB 时就写这个，靠 ICC 说明真实空间）。
    inline std::string fromExifColorSpace(std::string_view value) {
        const auto trimmed = sanitize(value);
        if (trimmed == "1" || trimmed == "sRGB")
            return "sRGB";
        if (trimmed == "2")
            return "Adobe RGB";
        if (trimmed == "65535" || trimmed == "-1")
            return {};   // 「未校准」等于没说，留给 ICC 或后面的兜底
        return trimmed;
    }

    // 综合判定。icc 为空且 EXIF 也说不清时返回空字符串——不猜。
    inline std::string resolve(std::span<const uint8_t> icc,
        std::string_view exifColorSpace, std::string_view exifInteropIndex = {}) {
        if (auto name = fromIccProfile(icc); !name.empty())
            return name;
        if (auto name = fromExifColorSpace(exifColorSpace); !name.empty())
            return name;
        // InteropIndex 是 Adobe RGB 的另一处线索：R98 = sRGB，R03 = Adobe RGB
        const auto interop = sanitize(exifInteropIndex);
        if (interop.starts_with("R03"))
            return "Adobe RGB";
        if (interop.starts_with("R98"))
            return "sRGB";
        return {};
    }

}
