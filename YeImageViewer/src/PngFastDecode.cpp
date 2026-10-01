#include "PngFastDecode.h"

#include <Windows.h>

#include <emmintrin.h>
#include <intrin.h>

#include <cstring>
#include <memory>
#include <vector>

#include "libdeflate.h"

#pragma comment(lib, "libdeflate.lib")

namespace {

constexpr std::uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

constexpr int COLOR_GRAY = 0;
constexpr int COLOR_RGB = 2;
constexpr int COLOR_PALETTE = 3;
constexpr int COLOR_GRAY_ALPHA = 4;
constexpr int COLOR_RGBA = 6;

std::uint32_t readBE32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

struct Header {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bitDepth = 0;
    int colorType = 0;
    int interlace = 0;
    std::size_t idatBytes = 0;
    std::size_t idatChunks = 0;
    bool hasTrns = false;
    bool sane = false;
};

// 只扫块头，不碰像素数据。坏文件在这里就该被挡回去。
Header readHeader(std::span<const std::uint8_t> file) {
    Header header;
    if (file.size() < 8 + 25 || std::memcmp(file.data(), PNG_SIGNATURE, 8) != 0)
        return header;

    bool sawIhdr = false;
    bool sawIend = false;
    std::size_t pos = 8;
    while (pos + 12 <= file.size()) {
        const std::uint32_t length = readBE32(file.data() + pos);
        // 块长度不能把我们带出文件，也不能溢出
        if (length > file.size() || pos + 12 + static_cast<std::size_t>(length) > file.size())
            return header;
        const std::uint8_t* type = file.data() + pos + 4;
        const std::uint8_t* body = file.data() + pos + 8;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (sawIhdr || length != 13)
                return header;
            sawIhdr = true;
            header.width = readBE32(body);
            header.height = readBE32(body + 4);
            header.bitDepth = body[8];
            header.colorType = body[9];
            header.interlace = body[12];
        }
        else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (!sawIhdr)
                return header;
            header.idatBytes += length;
            ++header.idatChunks;
        }
        else if (std::memcmp(type, "tRNS", 4) == 0) {
            header.hasTrns = true;
        }
        else if (std::memcmp(type, "IEND", 4) == 0) {
            sawIend = true;
            break;
        }
        else if (std::memcmp(type, "acTL", 4) == 0) {
            // APNG：第一帧之后还有动画帧，这条路只认静态图
            return header;
        }
        pos += 12 + static_cast<std::size_t>(length);
    }

    header.sane = sawIhdr && sawIend && header.idatBytes > 0 &&
        header.width > 0 && header.height > 0;
    return header;
}

int channelsOf(int colorType) {
    switch (colorType) {
    case COLOR_GRAY: return 1;
    case COLOR_RGB: return 3;
    case COLOR_RGBA: return 4;
    default: return 0;
    }
}

bool supported(const Header& header) {
    if (!header.sane)
        return false;
    if (header.interlace != 0)                       // Adam7 的行宽逐趟变化，不接
        return false;
    if (header.bitDepth != 8 && header.bitDepth != 16)
        return false;
    if (header.hasTrns)                              // tRNS 会让 OpenCV 多出一个 alpha 通道
        return false;
    if (header.colorType == COLOR_PALETTE || header.colorType == COLOR_GRAY_ALPHA)
        return false;                                // 调色板展开、灰+alpha 转 RGBA，交给 OpenCV
    if (channelsOf(header.colorType) == 0)
        return false;

    // 行字节数不能溢出，总像素数也要在合理范围
    const std::size_t bytesPerPixel =
        static_cast<std::size_t>(channelsOf(header.colorType)) * (header.bitDepth / 8);
    const std::size_t stride = static_cast<std::size_t>(header.width) * bytesPerPixel;
    if (stride / bytesPerPixel != header.width)
        return false;
    const std::size_t rawSize = static_cast<std::size_t>(header.height) * (stride + 1);
    if (header.height != 0 && rawSize / header.height != stride + 1)
        return false;
    return true;
}

// ── 去滤波 ────────────────────────────────────────────────────────────────
// 结构照搬 libpng 自带的 SSE2 实现：像素之间是串行的（每个像素要用左边那个
// 已经重建出来的像素），SIMD 只用在一个像素内部的 BPP 个字节上。
//
// 快路径用 8 字节加载，只在「剩余 ≥ 8 字节」时进入，最后一个像素走精确宽度的
// 慢路径，所以永远不会读到行尾之外。多读的那几个字节落在不写回的通道里，
// 下一轮会变成 a 的高位通道，同样只影响不写回的通道。

__m128i loadWide(const void* p) {
    return _mm_loadl_epi64(static_cast<const __m128i*>(p));
}

template <int BPP>
__m128i loadExact(const void* p) {
    std::uint8_t tmp[8] = {};
    std::memcpy(tmp, p, BPP);
    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(tmp));
}

template <int BPP>
void storeExact(void* p, __m128i v) {
    std::uint8_t tmp[8];
    _mm_storel_epi64(reinterpret_cast<__m128i*>(tmp), v);
    std::memcpy(p, tmp, BPP);
}

__m128i absI16(__m128i x) {
    const __m128i negative = _mm_cmplt_epi16(x, _mm_setzero_si128());
    x = _mm_xor_si128(x, negative);
    return _mm_sub_epi16(x, negative);
}

__m128i selectI16(__m128i cond, __m128i t, __m128i e) {
    return _mm_or_si128(_mm_and_si128(cond, t), _mm_andnot_si128(cond, e));
}

__m128i truncatedAverage(__m128i a, __m128i b) {
    // PNG 要的是向下取整的平均，_mm_avg_epu8 是四舍五入，
    // 所以在 a、b 奇偶性不同（会进位）的字节上减 1 修回来。
    const __m128i rounded = _mm_avg_epu8(a, b);
    return _mm_sub_epi8(rounded, _mm_and_si128(_mm_xor_si128(a, b), _mm_set1_epi8(1)));
}

template <int BPP>
void filterSub(std::uint8_t* row, const std::uint8_t*, std::size_t rowbytes) {
    __m128i a, d = _mm_setzero_si128();
    std::size_t remaining = rowbytes;
    while (remaining >= 8) {
        a = d; d = _mm_add_epi8(loadWide(row), a);
        storeExact<BPP>(row, d);
        row += BPP; remaining -= BPP;
    }
    while (remaining > 0) {
        a = d; d = _mm_add_epi8(loadExact<BPP>(row), a);
        storeExact<BPP>(row, d);
        row += BPP; remaining -= BPP;
    }
}

template <int BPP>
void filterUp(std::uint8_t* row, const std::uint8_t* prev, std::size_t rowbytes) {
    // 没有像素间依赖，整行一次 16 字节地加即可
    std::size_t i = 0;
    for (; i + 16 <= rowbytes; i += 16) {
        const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i));
        const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prev + i));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(row + i), _mm_add_epi8(d, b));
    }
    for (; i < rowbytes; ++i)
        row[i] = static_cast<std::uint8_t>(row[i] + prev[i]);
}

template <int BPP>
void filterAvg(std::uint8_t* row, const std::uint8_t* prev, std::size_t rowbytes) {
    __m128i a, b, d = _mm_setzero_si128();
    std::size_t remaining = rowbytes;
    while (remaining >= 8) {
        b = loadWide(prev); a = d; d = loadWide(row);
        d = _mm_add_epi8(d, truncatedAverage(a, b));
        storeExact<BPP>(row, d);
        row += BPP; prev += BPP; remaining -= BPP;
    }
    while (remaining > 0) {
        b = loadExact<BPP>(prev); a = d; d = loadExact<BPP>(row);
        d = _mm_add_epi8(d, truncatedAverage(a, b));
        storeExact<BPP>(row, d);
        row += BPP; prev += BPP; remaining -= BPP;
    }
}

template <int BPP>
void filterPaeth(std::uint8_t* row, const std::uint8_t* prev, std::size_t rowbytes) {
    const __m128i zero = _mm_setzero_si128();
    __m128i c, b = zero, a, d = zero;
    std::size_t remaining = rowbytes;

    const auto step = [&](__m128i loadedPrev, __m128i loadedRow) {
        c = b; b = _mm_unpacklo_epi8(loadedPrev, zero);
        a = d; d = _mm_unpacklo_epi8(loadedRow, zero);

        __m128i pa = _mm_sub_epi16(b, c);           // (p-a) == (b-c)
        __m128i pb = _mm_sub_epi16(a, c);           // (p-b) == (a-c)
        __m128i pc = _mm_add_epi16(pa, pb);         // (p-c) == (b-c)+(a-c)
        pa = absI16(pa); pb = absI16(pb); pc = absI16(pc);

        const __m128i smallest = _mm_min_epi16(pc, _mm_min_epi16(pa, pb));
        // 平局按 a、b、c 的顺序取，与规范一致
        const __m128i nearest = selectI16(_mm_cmpeq_epi16(smallest, pa), a,
                                selectI16(_mm_cmpeq_epi16(smallest, pb), b, c));
        d = _mm_add_epi8(d, nearest);               // 要按 256 取模，所以用 _epi8
    };

    while (remaining >= 8) {
        step(loadWide(prev), loadWide(row));
        storeExact<BPP>(row, _mm_packus_epi16(d, d));
        row += BPP; prev += BPP; remaining -= BPP;
    }
    while (remaining > 0) {
        step(loadExact<BPP>(prev), loadExact<BPP>(row));
        storeExact<BPP>(row, _mm_packus_epi16(d, d));
        row += BPP; prev += BPP; remaining -= BPP;
    }
}

using FilterFn = void (*)(std::uint8_t*, const std::uint8_t*, std::size_t);

template <int BPP>
bool applyFilter(int filter, std::uint8_t* row, const std::uint8_t* prev, std::size_t rowbytes) {
    switch (filter) {
    case 0: return true;                                        // None
    case 1: filterSub<BPP>(row, prev, rowbytes); return true;
    case 2: filterUp<BPP>(row, prev, rowbytes); return true;
    case 3: filterAvg<BPP>(row, prev, rowbytes); return true;
    case 4: filterPaeth<BPP>(row, prev, rowbytes); return true;
    default: return false;                                      // 坏文件
    }
}

// ── 去完滤波的行 → 目标 Mat 的一行 ───────────────────────────────────────
// 和 OpenCV 的 IMREAD_UNCHANGED 对齐：真彩转 BGR（含 alpha 时 BGRA），
// 16 位从 PNG 的大端转成小端。行此刻还在缓存里，这一趟几乎不要钱。

void convertRow8(const std::uint8_t* src, std::uint8_t* dst, std::uint32_t width, int channels) {
    if (channels == 1) {
        std::memcpy(dst, src, width);
    }
    else if (channels == 3) {
        for (std::uint32_t x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    else {
        for (std::uint32_t x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
}

void convertRow16(const std::uint8_t* src, std::uint16_t* dst, std::uint32_t width, int channels) {
    // PNG 的 16 位样本是大端，OpenCV 要小端（它走的是 png_set_swap）。
    // 按 uint16 读再 _byteswap_ushort，编译成一条 rol 指令；
    // 原先那样逐字节移位要多一倍的访存，整张图下来差好几百毫秒。
    // x86 不在乎非对齐读，所以直接 reinterpret 就行。
    const auto* in = reinterpret_cast<const std::uint16_t*>(src);
    if (channels == 1) {
        for (std::uint32_t x = 0; x < width; ++x)
            dst[x] = _byteswap_ushort(in[x]);
    }
    else if (channels == 3) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint16_t r = in[x * 3 + 0];
            const std::uint16_t g = in[x * 3 + 1];
            const std::uint16_t b = in[x * 3 + 2];
            dst[x * 3 + 0] = _byteswap_ushort(b);
            dst[x * 3 + 1] = _byteswap_ushort(g);
            dst[x * 3 + 2] = _byteswap_ushort(r);
        }
    }
    else {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint16_t r = in[x * 4 + 0];
            const std::uint16_t g = in[x * 4 + 1];
            const std::uint16_t b = in[x * 4 + 2];
            const std::uint16_t a = in[x * 4 + 3];
            dst[x * 4 + 0] = _byteswap_ushort(b);
            dst[x * 4 + 1] = _byteswap_ushort(g);
            dst[x * 4 + 2] = _byteswap_ushort(r);
            dst[x * 4 + 3] = _byteswap_ushort(a);
        }
    }
}

template <int BPP>
bool unfilterAndConvert(std::uint8_t* raw, const Header& header, cv::Mat& out) {
    const std::size_t stride = static_cast<std::size_t>(header.width) * BPP;
    const int channels = channelsOf(header.colorType);
    // 第一行的「上一行」按全零处理，这正是规范的要求
    const std::vector<std::uint8_t> zeroRow(stride, 0);
    const std::uint8_t* prev = zeroRow.data();

    for (std::uint32_t y = 0; y < header.height; ++y) {
        std::uint8_t* line = raw + static_cast<std::size_t>(y) * (stride + 1);
        const int filter = line[0];
        std::uint8_t* row = line + 1;

        if (!applyFilter<BPP>(filter, row, prev, stride))
            return false;

        if (header.bitDepth == 8)
            convertRow8(row, out.ptr<std::uint8_t>(static_cast<int>(y)), header.width, channels);
        else
            convertRow16(row, out.ptr<std::uint16_t>(static_cast<int>(y)), header.width, channels);

        prev = row;
    }
    return true;
}

// 解压缓冲 + 目标 Mat 两份加起来，不能超过当前可用物理内存的一半。
// 取「可用」而不是「总量」：别人正占着的内存不该算进我们的预算。
bool hasHeadroomFor(std::size_t rawBytes, std::size_t matBytes) {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return false;                       // 问不出来就不冒险
    const std::uint64_t needed = static_cast<std::uint64_t>(rawBytes) + matBytes;
    return needed <= status.ullAvailPhys / 2;
}

}  // namespace

namespace PngFastDecode {

bool isSupported(std::span<const std::uint8_t> file, bool ignoreSizeLimit) {
    if (!ignoreSizeLimit && file.size() < MIN_FILE_BYTES)
        return false;
    return supported(readHeader(file));
}

bool decode(std::span<const std::uint8_t> file, cv::Mat& out, bool ignoreSizeLimit) {
    if (!ignoreSizeLimit && file.size() < MIN_FILE_BYTES)
        return false;

    const Header header = readHeader(file);
    if (!supported(header))
        return false;

    const int channels = channelsOf(header.colorType);
    const int bytesPerPixel = channels * (header.bitDepth / 8);
    const std::size_t stride = static_cast<std::size_t>(header.width) * bytesPerPixel;
    const std::size_t rawSize = static_cast<std::size_t>(header.height) * (stride + 1);

    // 整块解压是拿内存换时间：解压缓冲和目标 Mat 要同时在手，各占一份全图大小，
    // 而 libpng 那条流式路径只需要目标 Mat。一张 9000×9000 的 16 位图，
    // 这边峰值约 1.3 GB，那边约 0.85 GB。
    // 所以内存不宽裕时主动让开，让 OpenCV 用它那条省内存的路——
    // 慢一点总好过把机器拖进换页，或者在 new 的时候抛出来再回退。
    if (!hasHeadroomFor(rawSize, static_cast<std::size_t>(header.height) * stride))
        return false;

    // IDAT 可能被切成很多块，zlib 流本身是连续的，先拼起来再一次解完。
    //
    // 这里刻意不用 std::vector：resize 会把整块内存填零，而这几百 MB 紧接着
    // 就被拷贝/解压整体覆盖，填零纯属白跑——一张 291 MB 的图上要多花上百毫秒。
    // new uint8_t[n]（不带括号）对 POD 是默认初始化，也就是不初始化。
    // 只有一个 IDAT 块时根本不用拼：直接指着文件里那一段解压。
    // 大图很常见——moon_81M 就是一整块 291 MB，省掉这份副本既省时间，
    // 又让峰值内存少扛一份压缩数据。
    std::unique_ptr<std::uint8_t[]> idatCopy;
    const std::uint8_t* compressed = nullptr;

    if (header.idatChunks == 1) {
        for (std::size_t pos = 8; pos + 12 <= file.size(); ) {
            const std::uint32_t length = readBE32(file.data() + pos);
            if (std::memcmp(file.data() + pos + 4, "IDAT", 4) == 0) {
                compressed = file.data() + pos + 8;
                break;
            }
            pos += 12 + static_cast<std::size_t>(length);
        }
        if (compressed == nullptr)
            return false;
    }
    else {
        try {
            idatCopy.reset(new std::uint8_t[header.idatBytes]);
        }
        catch (const std::bad_alloc&) {
            return false;
        }
        std::size_t used = 0;
        for (std::size_t pos = 8; pos + 12 <= file.size(); ) {
            const std::uint32_t length = readBE32(file.data() + pos);
            const std::uint8_t* type = file.data() + pos + 4;
            if (std::memcmp(type, "IDAT", 4) == 0) {
                if (used + length > header.idatBytes)
                    return false;
                std::memcpy(idatCopy.get() + used, file.data() + pos + 8, length);
                used += length;
            }
            else if (std::memcmp(type, "IEND", 4) == 0) {
                break;
            }
            pos += 12 + static_cast<std::size_t>(length);
        }
        if (used != header.idatBytes)
            return false;
        compressed = idatCopy.get();
    }

    std::unique_ptr<std::uint8_t[]> raw;
    try {
        raw.reset(new std::uint8_t[rawSize]);
    }
    catch (const std::bad_alloc&) {
        return false;
    }

    libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
    if (decompressor == nullptr)
        return false;
    std::size_t produced = 0;
    const libdeflate_result result = libdeflate_zlib_decompress(
        decompressor, compressed, header.idatBytes, raw.get(), rawSize, &produced);
    libdeflate_free_decompressor(decompressor);
    // 拼出来的那份这时候已经没用了，赶紧还回去：接下来还要再开一块和 raw 一样大的 Mat，
    // 不放的话峰值内存要多扛一份 IDAT。
    compressed = nullptr;
    idatCopy.reset();
    // 解出来的字节数必须刚好等于「每行一个滤波字节 + 行数据」，少一个都说明文件有问题
    if (result != LIBDEFLATE_SUCCESS || produced != rawSize)
        return false;

    const int matType = header.bitDepth == 8 ? CV_MAKETYPE(CV_8U, channels)
                                             : CV_MAKETYPE(CV_16U, channels);
    try {
        out.create(static_cast<int>(header.height), static_cast<int>(header.width), matType);
    }
    catch (const cv::Exception&) {
        return false;
    }

    switch (bytesPerPixel) {
    case 1: return unfilterAndConvert<1>(raw.get(), header, out);
    case 2: return unfilterAndConvert<2>(raw.get(), header, out);
    case 3: return unfilterAndConvert<3>(raw.get(), header, out);
    case 4: return unfilterAndConvert<4>(raw.get(), header, out);
    case 6: return unfilterAndConvert<6>(raw.get(), header, out);
    case 8: return unfilterAndConvert<8>(raw.get(), header, out);
    default: return false;
    }
}

}  // namespace PngFastDecode
