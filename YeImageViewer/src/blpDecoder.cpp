#include "jarkUtils.h"
#include "blpDecoder.h"

/// Expand 5-bit component → 8-bit  (replicates top bits into bottom)
inline static uint8_t expand5(uint8_t v) { return (v << 3) | (v >> 2); }
/// Expand 6-bit component → 8-bit
inline static uint8_t expand6(uint8_t v) { return (v << 2) | (v >> 4); }

/// Unpack RGB565 word → R, G, B  (each 0-255)
inline static void unpack565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = expand5(static_cast<uint8_t>((c >> 11) & 0x1F));
    g = expand6(static_cast<uint8_t>((c >> 5) & 0x3F));
    b = expand5(static_cast<uint8_t>(c & 0x1F));
}

/// Read little-endian uint16
inline static uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/// Read little-endian uint32
inline static uint32_t readU32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}


void blpDecoder::decompressBC1Block(const uint8_t* src, uint8_t dst[64], bool hasBinaryAlpha) {
    const uint16_t c0 = readU16(src + 0);
    const uint16_t c1 = readU16(src + 2);
    const uint32_t bits = readU32(src + 4);

    uint8_t r[4], g[4], b[4], a[4];
    unpack565(c0, r[0], g[0], b[0]);
    unpack565(c1, r[1], g[1], b[1]);
    a[0] = a[1] = a[2] = a[3] = 0xFF;

    if ((c0 > c1) || !hasBinaryAlpha) {
        // 4-colour mode — two interpolated values, fully opaque
        r[2] = static_cast<uint8_t>((2 * r[0] + r[1] + 1) / 3);
        g[2] = static_cast<uint8_t>((2 * g[0] + g[1] + 1) / 3);
        b[2] = static_cast<uint8_t>((2 * b[0] + b[1] + 1) / 3);

        r[3] = static_cast<uint8_t>((r[0] + 2 * r[1] + 1) / 3);
        g[3] = static_cast<uint8_t>((g[0] + 2 * g[1] + 1) / 3);
        b[3] = static_cast<uint8_t>((b[0] + 2 * b[1] + 1) / 3);
    }
    else {
        // 3-colour mode — midpoint + transparent black
        r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2);
        g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2);
        b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2);

        r[3] = g[3] = b[3] = 0;
        a[3] = 0x00;
    }

    for (int i = 0; i < 16; ++i) {
        const int    idx = (bits >> (i * 2)) & 0x3;
        uint8_t* px = dst + i * 4;
        px[0] = b[idx];
        px[1] = g[idx];
        px[2] = r[idx];
        px[3] = a[idx];
    }
}

void blpDecoder::decompressBC2Block(const uint8_t* src, uint8_t dst[64]) {
    decompressBC1Block(src + 8, dst, false);

    for (int i = 0; i < 8; ++i) {
        dst[(i * 2 + 0) * 4 + 3] = static_cast<uint8_t>((src[i] & 0x0F) * 17);
        dst[(i * 2 + 1) * 4 + 3] = static_cast<uint8_t>((src[i] >> 4) * 17);
    }
}

void blpDecoder::decompressBC3Block(const uint8_t* src, uint8_t dst[64]) {
    const uint8_t a0 = src[0];
    const uint8_t a1 = src[1];

    uint8_t alphaTable[8];
    alphaTable[0] = a0;
    alphaTable[1] = a1;

    if (a0 > a1) {
        // 8-alpha mode: 6 interpolated values
        alphaTable[2] = static_cast<uint8_t>((6 * a0 + 1 * a1 + 3) / 7);
        alphaTable[3] = static_cast<uint8_t>((5 * a0 + 2 * a1 + 3) / 7);
        alphaTable[4] = static_cast<uint8_t>((4 * a0 + 3 * a1 + 3) / 7);
        alphaTable[5] = static_cast<uint8_t>((3 * a0 + 4 * a1 + 3) / 7);
        alphaTable[6] = static_cast<uint8_t>((2 * a0 + 5 * a1 + 3) / 7);
        alphaTable[7] = static_cast<uint8_t>((1 * a0 + 6 * a1 + 3) / 7);
    }
    else {
        // 6-alpha mode: 4 interpolated + sentinels 0x00 / 0xFF
        alphaTable[2] = static_cast<uint8_t>((4 * a0 + 1 * a1 + 2) / 5);
        alphaTable[3] = static_cast<uint8_t>((3 * a0 + 2 * a1 + 2) / 5);
        alphaTable[4] = static_cast<uint8_t>((2 * a0 + 3 * a1 + 2) / 5);
        alphaTable[5] = static_cast<uint8_t>((1 * a0 + 4 * a1 + 2) / 5);
        alphaTable[6] = 0x00;
        alphaTable[7] = 0xFF;
    }

    // 48-bit index field — 6 bytes, little-endian
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k)
        bits |= static_cast<uint64_t>(src[2 + k]) << (k * 8);

    decompressBC1Block(src + 8, dst, false);

    for (int i = 0; i < 16; ++i)
        dst[i * 4 + 3] = alphaTable[(bits >> (i * 3)) & 0x7];
}


cv::Mat blpDecoder::decompressDXT(BufView  data, uint32_t width, uint32_t height, int dxtType) {
    if (width == 0 || height == 0) {
        JARK_LOG("[ERROR] BLP DXT{}: zero image dimension ({}x{})", dxtType, width, height);
        return {};
    }

    const int    blockBytes = (dxtType == 1) ? 8 : 16;
    const int    blocksX = (static_cast<int>(width) + 3) / 4;
    const int    blocksY = (static_cast<int>(height) + 3) / 4;
    const size_t needed = static_cast<size_t>(blocksX) * blocksY * blockBytes;

    if (data.size < needed) {
        JARK_LOG("[ERROR] BLP DXT{}: data too small ({} < {} bytes needed)",
            dxtType, data.size, needed);
        return {};
    }

    cv::Mat result(static_cast<int>(height), static_cast<int>(width), CV_8UC4);

    const uint8_t* src = data.data;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            uint8_t block[64];
            switch (dxtType) {
            case 1: decompressBC1Block(src, block, true); break;
            case 3: decompressBC2Block(src, block);       break;
            default: decompressBC3Block(src, block);      break; // 5
            }
            src += blockBytes;

            const int baseY = by * 4;
            const int baseX = bx * 4;
            for (int py = 0; py < 4; ++py) {
                const int imgY = baseY + py;
                if (imgY >= static_cast<int>(height)) continue;

                uint8_t* row = result.ptr<uint8_t>(imgY);
                for (int px = 0; px < 4; ++px) {
                    const int imgX = baseX + px;
                    if (imgX >= static_cast<int>(width)) continue;

                    const uint8_t* blkPx = block + (py * 4 + px) * 4;
                    uint8_t* outPx = row + imgX * 4;
                    outPx[0] = blkPx[0]; // B
                    outPx[1] = blkPx[1]; // G
                    outPx[2] = blkPx[2]; // R
                    outPx[3] = blkPx[3]; // A
                }
            }
        }
    }
    return result;
}

cv::Mat blpDecoder::decodePalette(const uint8_t* indices,
    size_t          indexCount,
    const uint8_t* alphaData,
    size_t          alphaSize,
    int             alphaDepth,
    const uint32_t* palette,
    uint32_t        width,
    uint32_t        height) {

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (indexCount < pixelCount) {
        JARK_LOG("[ERROR] BLP palette: index buffer too small ({} < {} pixels)",
            indexCount, pixelCount);
        return {};
    }

    cv::Mat result(static_cast<int>(height), static_cast<int>(width), CV_8UC4);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint32_t entry = palette[indices[i]];

        const uint8_t bOut = entry & 0xFF;
        const uint8_t gOut = (entry >> 8) & 0xFF;
        const uint8_t rOut = (entry >> 16) & 0xFF;
        uint8_t       aOut = 0xFF;

        if (alphaDepth == 1 && alphaData && alphaSize > 0) {
            const size_t byteIdx = i / 8;
            if (byteIdx < alphaSize)
                aOut = ((alphaData[byteIdx] >> (i % 8)) & 1) ? 0xFF : 0x00;
        }
        else if (alphaDepth == 4 && alphaData && alphaSize > 0) {
            const size_t byteIdx = i / 2;
            if (byteIdx < alphaSize) {
                const uint8_t nibble = (i % 2 == 0)
                    ? (alphaData[byteIdx] & 0x0F)
                    : ((alphaData[byteIdx] >> 4) & 0x0F);
                aOut = static_cast<uint8_t>(nibble * 17);
            }
        }
        else if (alphaDepth == 8 && alphaData && alphaSize > 0) {
            if (i < alphaSize) aOut = alphaData[i];
        }
        // alphaDepth == 0 → aOut stays 0xFF

        uint8_t* px = result.ptr<uint8_t>(static_cast<int>(i / width))
            + (i % width) * 4;
        px[0] = bOut;
        px[1] = gOut;
        px[2] = rOut;
        px[3] = aOut;
    }
    return result;
}

cv::Mat blpDecoder::decodeJPEG(const uint8_t* header, size_t headerLen, const uint8_t* body, size_t bodyLen) {
    std::vector<uint8_t> jpegBuf;
    jpegBuf.reserve(headerLen + bodyLen);
    jpegBuf.insert(jpegBuf.end(), header, header + headerLen);
    jpegBuf.insert(jpegBuf.end(), body, body + bodyLen);

    cv::Mat decoded = cv::imdecode(jpegBuf, cv::IMREAD_UNCHANGED);
    if (decoded.empty()) {
        JARK_LOG("[ERROR] BLP1 JPEG: cv::imdecode failed");
        return {};
    }

    cv::Mat bgra;
    switch (decoded.channels()) {
    case 1: cv::cvtColor(decoded, bgra, cv::COLOR_GRAY2BGRA); break;
    case 3: cv::cvtColor(decoded, bgra, cv::COLOR_BGR2BGRA);  break;
    case 4: bgra = decoded.clone();                           break;
    default:
        JARK_LOG("[ERROR] BLP1 JPEG: unexpected channel count {}", decoded.channels());
        return {};
    }

    // RGBA -> BGRA
    {
        std::vector<cv::Mat> ch(4);
        cv::split(bgra, ch);
        std::swap(ch[0], ch[2]); // swap B and R
        cv::merge(ch, bgra);
    }

    return bgra;
}


cv::Mat blpDecoder::decodeBLP1(BufView buf) {
    const auto* hdr = buf.as<BLP1Header>(0);
    if (!hdr) {
        JARK_LOG("[ERROR] BLP1: file too small for header ({} bytes)", buf.size);
        return {};
    }

    const uint32_t width = hdr->width;
    const uint32_t height = hdr->height;
    if (width == 0 || height == 0 || width > 65536 || height > 65536) {
        JARK_LOG("[ERROR] BLP1: invalid image dimensions ({}x{})", width, height);
        return {};
    }

    // Locate first (largest) mipmap
    uint32_t mipOff = 0;
    uint32_t mipSize = 0;
    for (int m = 0; m < kMaxMipmaps; ++m) {
        if (hdr->mipmapOffset[m] != 0 && hdr->mipmapSize[m] != 0) {
            mipOff = hdr->mipmapOffset[m];
            mipSize = hdr->mipmapSize[m];
            break;
        }
    }

    if (mipOff == 0 || mipSize == 0) {
        JARK_LOG("[ERROR] BLP1: no valid mipmap entry found");
        return {};
    }

    const uint8_t* mipData = buf.ptr(mipOff, mipSize);
    if (!mipData) {
        JARK_LOG("[ERROR] BLP1: mipmap data out of bounds (offset={}, size={})",
            mipOff, mipSize);
        return {};
    }

    // JPEG mode
    if (hdr->compression == static_cast<uint32_t>(BLP1Compression::JPEG)) {
        // At offset 156: uint32_t jpegHeaderSize, then jpegHeaderSize bytes of data
        constexpr size_t kSizeFieldOffset = sizeof(BLP1Header);           // 156
        constexpr size_t kDataOffset = kSizeFieldOffset + sizeof(uint32_t); // 160

        const uint8_t* sizeField = buf.ptr(kSizeFieldOffset, sizeof(uint32_t));
        if (!sizeField) {
            JARK_LOG("[ERROR] BLP1: JPEG size field out of bounds");
            return {};
        }
        const uint32_t jpegHeaderSize = readU32(sizeField);

        const uint8_t* jpegHeader = buf.ptr(kDataOffset, jpegHeaderSize);
        if (!jpegHeader) {
            JARK_LOG("[ERROR] BLP1: JPEG shared header out of bounds (size={})",
                jpegHeaderSize);
            return {};
        }

        return decodeJPEG(jpegHeader, jpegHeaderSize, mipData, mipSize);
    }

    // Palette mode
    if (hdr->compression == static_cast<uint32_t>(BLP1Compression::Palette)) {
        const uint8_t* palRaw = buf.ptr(kBLP1PalOffset, kPaletteBytes);
        if (!palRaw) {
            JARK_LOG("[ERROR] BLP1: palette data out of bounds");
            return {};
        }
        const auto* palette = reinterpret_cast<const uint32_t*>(palRaw);

        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (mipSize < pixelCount) {
            JARK_LOG("[ERROR] BLP1: mipmap too small for pixel indices ({} < {})",
                mipSize, pixelCount);
            return {};
        }

        const uint8_t* indices = mipData;
        const uint8_t* alphaData = nullptr;
        size_t         alphaSize = 0;
        int            alphaDepth = 0;

        auto setAlpha = [&](int depth) {
            alphaDepth = depth;
            if (mipSize > pixelCount) {
                alphaData = mipData + pixelCount;
                alphaSize = mipSize - pixelCount;
            }
            };

        switch (static_cast<BLP1PictureType>(hdr->pictureType)) {
        case BLP1PictureType::PalettedWithAlpha8: setAlpha(8); break;
        case BLP1PictureType::PalettedNoAlpha:    /* depth=0 */ break;
        case BLP1PictureType::PalettedWithAlpha1: setAlpha(1); break;
        default:
            // Unknown pictureType — fall back to flags bit 3
            if (hdr->flags & 0x8u) setAlpha(8);
            break;
        }

        return decodePalette(indices, pixelCount,
            alphaData, alphaSize, alphaDepth,
            palette, width, height);
    }

    JARK_LOG("[ERROR] BLP1: unknown compression type {}", hdr->compression);
    return {};
}

cv::Mat blpDecoder::decodeBLP2(BufView buf) {
    const auto* hdr = buf.as<BLP2Header>(0);
    if (!hdr) {
        JARK_LOG("[ERROR] BLP2: file too small for header ({} bytes)", buf.size);
        return {};
    }

    const uint32_t width = hdr->width;
    const uint32_t height = hdr->height;
    if (width == 0 || height == 0 || width > 65536 || height > 65536) {
        JARK_LOG("[ERROR] BLP2: invalid image dimensions ({}x{})", width, height);
        return {};
    }

    // Locate first valid mipmap
    uint32_t mipOff = 0;
    uint32_t mipSize = 0;
    for (int m = 0; m < kMaxMipmaps; ++m) {
        if (hdr->mipmapOffset[m] != 0 && hdr->mipmapSize[m] != 0) {
            mipOff = hdr->mipmapOffset[m];
            mipSize = hdr->mipmapSize[m];
            break;
        }
    }
    if (mipOff == 0 || mipSize == 0) {
        JARK_LOG("[ERROR] BLP2: no valid mipmap entry found");
        return {};
    }

    const BufView mipView = buf.sub(mipOff, mipSize);
    if (!mipView.data) {
        JARK_LOG("[ERROR] BLP2: mipmap data out of bounds (offset={}, size={})",
            mipOff, mipSize);
        return {};
    }

    // Dispatch by encoding
    switch (static_cast<BLP2Encoding>(hdr->encoding)) {
    case BLP2Encoding::Uncompressed: {  //  Encoding 1: palette-indexed
        const uint8_t* palRaw = buf.ptr(kBLP2PalOffset, kPaletteBytes);
        if (!palRaw) {
            JARK_LOG("[ERROR] BLP2: palette data out of bounds");
            return {};
        }
        const auto* palette = reinterpret_cast<const uint32_t*>(palRaw);

        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (mipSize < pixelCount) {
            JARK_LOG("[ERROR] BLP2: mipmap too small for palette indices ({} < {})",
                mipSize, pixelCount);
            return {};
        }

        const uint8_t* alphaData = nullptr;
        size_t         alphaSize = 0;
        if (hdr->alphaDepth > 0 && mipSize > pixelCount) {
            alphaData = mipView.data + pixelCount;
            alphaSize = mipSize - pixelCount;
        }

        return decodePalette(mipView.data, pixelCount,
            alphaData, alphaSize, hdr->alphaDepth,
            palette, width, height);
    }

    case BLP2Encoding::DXT: { // Encoding 2: DXT block compression 
        int dxtType = 0;
        switch (static_cast<BLP2AlphaEncoding>(hdr->alphaEncoding)) {
        case BLP2AlphaEncoding::DXT1: dxtType = 1; break;
        case BLP2AlphaEncoding::DXT3: dxtType = 3; break;
        case BLP2AlphaEncoding::DXT5: dxtType = 5; break;
        default:
            JARK_LOG("[ERROR] BLP2: unknown alphaEncoding {}",
                static_cast<int>(hdr->alphaEncoding));
            return {};
        }

        cv::Mat result = decompressDXT(mipView, width, height, dxtType);
        if (result.empty()) return {};

        // alphaDepth == 0 with DXT1 → force fully opaque
        if (dxtType == 1 && hdr->alphaDepth == 0) {
            std::vector<cv::Mat> ch(4);
            cv::split(result, ch);
            ch[3].setTo(0xFF);
            cv::merge(ch, result);
        }

        return result;
    }

    case BLP2Encoding::RawBGRA: {  // Encoding 3: raw 32-bit BGRA 
        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (mipSize < pixelCount * 4) {
            JARK_LOG("[ERROR] BLP2: raw BGRA mipmap too small ({} < {} bytes needed)",
                mipSize, pixelCount * 4);
            return {};
        }

        cv::Mat result(static_cast<int>(height), static_cast<int>(width), CV_8UC4);
        std::memcpy(result.data, mipView.data, pixelCount * 4);
        return result;
    }

    default:
        JARK_LOG("[ERROR] BLP2: unknown encoding {}", hdr->encoding);
        return {};
    }
}

