#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstring>
#include <format>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace blpDecoder{
    constexpr uint32_t kMagicBLP1 = 0x31504C42u; // "BLP1"
    constexpr uint32_t kMagicBLP2 = 0x32504C42u; // "BLP2"

    constexpr int kMaxMipmaps = 16;

    enum class BLP1Compression : uint32_t {
        JPEG = 0,
        Palette = 1,
    };

    // BLP1 pictureType (only relevant when compression==Palette)
    enum class BLP1PictureType : uint32_t {
        PalettedWithAlpha8 = 3, // palette indices + 8-bit alpha data
        PalettedNoAlpha = 4, // palette indices only
        PalettedWithAlpha1 = 5, // palette indices + 1-bit packed alpha
    };

    // BLP2 encoding byte values
    enum class BLP2Encoding : uint8_t {
        Uncompressed = 1, // palette-indexed
        DXT = 2, // DXTn block compression
        RawBGRA = 3, // plain 32-bit BGRA
    };

    // BLP2 alphaEncoding — only relevant when encoding == DXT
    enum class BLP2AlphaEncoding : uint8_t {
        DXT1 = 0, // BC1 (alphaDepth 0 = opaque, 1 = 1-bit)
        DXT3 = 1, // BC2 — explicit 4-bit alpha
        DXT5 = 7, // BC3 — interpolated alpha
    };

    // BLP1 header  (total: 156 bytes)
#pragma pack(push, 1)
    struct BLP1Header {
        uint32_t magic;                          //  0  "BLP1"
        uint32_t compression;                    //  4
        uint32_t flags;                          //  8  bit3 = has alpha
        uint32_t width;                          // 12
        uint32_t height;                         // 16
        uint32_t pictureType;                    // 20
        uint32_t pictureSubType;                 // 24
        uint32_t mipmapOffset[kMaxMipmaps];      // 28
        uint32_t mipmapSize[kMaxMipmaps];      // 92
        // At offset 156: uint32_t jpegHeaderSize  (JPEG mode)
        //                uint32_t palette[256]    (Palette mode)
    };
    static_assert(sizeof(BLP1Header) == 156, "BLP1Header size mismatch");

    // BLP2 header  (total: 148 bytes)
    struct BLP2Header {
        uint32_t magic;                          //   0  "BLP2"
        uint32_t type;                           //   4  0=JPEG (rare), 1=direct
        uint8_t  encoding;                       //   8
        uint8_t  alphaDepth;                     //   9  0,1,4,8
        uint8_t  alphaEncoding;                  //  10  0=DXT1, 1=DXT3, 7=DXT5
        uint8_t  hasMipmaps;                     //  11
        uint32_t width;                          //  12
        uint32_t height;                         //  16
        uint32_t mipmapOffset[kMaxMipmaps];      //  20
        uint32_t mipmapSize[kMaxMipmaps];      //  84
        // At offset 148: uint32_t palette[256]
    };
    static_assert(sizeof(BLP2Header) == 148, "BLP2Header size mismatch");
#pragma pack(pop)

    constexpr size_t kPaletteBytes = 256 * sizeof(uint32_t); // 1024 bytes
    constexpr size_t kBLP1PalOffset = sizeof(BLP1Header);     // 156
    constexpr size_t kBLP2PalOffset = sizeof(BLP2Header);     // 148

    // Bounds-checked buffer view
    // No exceptions: all accessors return nullptr / false on out-of-bounds.
    struct BufView {
        const uint8_t* data = nullptr;
        size_t         size = 0;

        // Returns true iff [offset, offset+len) is fully inside the buffer.
        [[nodiscard]] bool check(size_t offset, size_t len) const
        {
            return len <= size && offset <= size - len; // guards wrap-around too
        }

        // Returns typed pointer, or nullptr if out-of-bounds.
        template<typename T>
        [[nodiscard]] const T* as(size_t offset) const
        {
            if (!check(offset, sizeof(T))) return nullptr;
            return reinterpret_cast<const T*>(data + offset);
        }

        // Returns raw pointer, or nullptr if out-of-bounds.
        [[nodiscard]] const uint8_t* ptr(size_t offset, size_t len) const
        {
            if (!check(offset, len)) return nullptr;
            return data + offset;
        }

        // Returns a sub-view; sub.data == nullptr signals failure.
        [[nodiscard]] BufView sub(size_t offset, size_t len) const
        {
            if (!check(offset, len)) return { nullptr, 0 };
            return { data + offset, len };
        }
    };

    cv::Mat decodeBLP1(BufView buf);
    cv::Mat decodeBLP2(BufView buf);

    // Block decoders are always called with pre-validated source pointers.
    void    decompressBC1Block(const uint8_t* src, uint8_t dst[64], bool hasBinaryAlpha);
    void    decompressBC2Block(const uint8_t* src, uint8_t dst[64]);
    void    decompressBC3Block(const uint8_t* src, uint8_t dst[64]);
    cv::Mat decompressDXT(BufView data, uint32_t width, uint32_t height, int dxtType);

    cv::Mat decodePalette(
        const uint8_t* indices,
        size_t          indexCount,
        const uint8_t* alphaData,  // may be nullptr
        size_t          alphaSize,
        int             alphaDepth, // 0, 1, 4, or 8
        const uint32_t* palette,    // 256 BGRA entries
        uint32_t        width,
        uint32_t        height);

    cv::Mat decodeJPEG(
        const uint8_t* header, size_t headerLen,
        const uint8_t* body, size_t bodyLen);

}