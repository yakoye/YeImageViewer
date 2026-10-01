#include "ThumbPsd.h"

// psd_sdk 自己要定义这几个宏，而工程的命令行上已经有了，不先取消会报重定义警告
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX
#include "psdsdk.h"

#include <algorithm>
#include <cstring>
#include <limits>

#pragma comment(lib, "Psd_MT.lib")

namespace YeThumbnail {
namespace {

// 和 ThumbDecoders.cpp 里那份同样的上限：缩略图不需要无限大的图
constexpr uint32_t kMaxDimension = 65535;

bool validDimensions(uint32_t width, uint32_t height) noexcept {
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension)
        return false;

    const uint64_t bytes = static_cast<uint64_t>(width) * height * 4ULL;
    return bytes <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
}

// psd_sdk 的 File 是从磁盘读的，而缩略图组件只有一个内存里的缓冲，
// 所以实现一个内存版。只需要读，写一律拒绝。
class PsdMemoryFile final : public psd::File {
public:
    PsdMemoryFile(psd::Allocator* allocator, const uint8_t* data, size_t size) noexcept
        : psd::File(allocator), m_data(data), m_size(size) {}

private:
    bool DoOpenRead(const wchar_t*) override { return m_data != nullptr; }
    bool DoOpenWrite(const wchar_t*) override { return false; }
    bool DoClose(void) override { return true; }

    ReadOperation DoRead(void* buffer, uint32_t count, uint64_t position) override {
        if (!buffer || position > m_size)
            return nullptr;

        const size_t available = m_size - static_cast<size_t>(position);
        const size_t copied = count < available ? count : available;
        std::memcpy(buffer, m_data + position, copied);
        if (copied < count) {
            // 读过界的部分填零：截断的文件不至于读到未初始化内存
            std::memset(static_cast<uint8_t*>(buffer) + copied, 0, count - copied);
        }
        // 同步读已经完成，返回一个非空句柄表示「这次操作有效」
        return reinterpret_cast<ReadOperation>(static_cast<uintptr_t>(1));
    }

    bool DoWaitForRead(ReadOperation& operation) override {
        const bool ok = operation != nullptr;
        operation = nullptr;
        return ok;
    }

    WriteOperation DoWrite(const void*, uint32_t, uint64_t) override { return nullptr; }
    bool DoWaitForWrite(WriteOperation&) override { return false; }
    uint64_t DoGetSize(void) const override { return m_size; }

    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
};

// psd_sdk 给的是分离的平面通道，这里交织成 BGRA 并归一到 8 位
template <typename Sample>
bool interleaveChannels(const void* red, const void* green, const void* blue, const void* alpha,
    uint32_t width, uint32_t height, double toByte, ThumbBitmap& out) {
    if (!red || !green || !blue)
        return false;

    const auto* r = static_cast<const Sample*>(red);
    const auto* g = static_cast<const Sample*>(green);
    const auto* b = static_cast<const Sample*>(blue);
    const auto* a = static_cast<const Sample*>(alpha);

    out.width = width;
    out.height = height;
    out.hasAlpha = alpha != nullptr;
    out.bgra.assign(static_cast<size_t>(width) * height * 4ULL, 255);

    const auto toSample = [toByte](Sample value) {
        const double scaled = static_cast<double>(value) * toByte;
        return static_cast<uint8_t>(std::clamp(scaled, 0.0, 255.0));
    };

    const size_t pixelCount = static_cast<size_t>(width) * height;
    bool observedAlpha = false;
    for (size_t index = 0; index < pixelCount; ++index) {
        const size_t dst = index * 4ULL;
        out.bgra[dst + 0] = toSample(b[index]);
        out.bgra[dst + 1] = toSample(g[index]);
        out.bgra[dst + 2] = toSample(r[index]);
        if (a) {
            const uint8_t sample = toSample(a[index]);
            out.bgra[dst + 3] = sample;
            observedAlpha = observedAlpha || sample != 255;
        }
    }

    out.hasAlpha = a != nullptr && observedAlpha;
    return true;
}

} // namespace

// PSD 走 psd_sdk，不走 stb。stb 虽然也认 PSD，但「16 位 + RLE 压缩」那一种它解不对：
// 它那条 RLE 分支按每通道 pixelCount 个字节解，而 16 位每通道是 pixelCount*2 个字节，
// 从第二个通道起全部错位，alpha 读成整层零——而且它返回「成功」，所以兜底根本轮不到。
// 主程序踩过这个坑，经过记在 test/corpus/README.md。
bool decodePsd(std::span<const uint8_t> data, ThumbBitmap& out) noexcept {
    out = {};
    if (data.empty())
        return false;

    try {
        psd::MallocAllocator allocator;
        PsdMemoryFile file(&allocator, data.data(), data.size());
        if (!file.OpenRead(L""))
            return false;

        psd::Document* document = CreateDocument(&file, &allocator);
        if (!document) {
            file.Close();
            return false;
        }

        bool decoded = false;
        if (document->colorMode == psd::colorMode::RGB &&
            validDimensions(document->width, document->height) &&
            document->imageDataSection.length != 0) {
            psd::ImageDataSection* imageData = ParseImageDataSection(document, &file, &allocator);
            if (imageData && imageData->imageCount >= 3) {
                // 正好 3 个通道就没有透明通道；4 个及以上取第 4 个当 alpha。
                // 再多出来的是附加 alpha 通道，缩略图用不上。
                const void* alpha = imageData->imageCount >= 4 ? imageData->images[3].data : nullptr;

                switch (document->bitsPerChannel) {
                case 8:
                    decoded = interleaveChannels<uint8_t>(
                        imageData->images[0].data, imageData->images[1].data,
                        imageData->images[2].data, alpha,
                        document->width, document->height, 1.0, out);
                    break;
                case 16:
                    decoded = interleaveChannels<uint16_t>(
                        imageData->images[0].data, imageData->images[1].data,
                        imageData->images[2].data, alpha,
                        document->width, document->height, 255.0 / 65535.0, out);
                    break;
                case 32:
                    decoded = interleaveChannels<float>(
                        imageData->images[0].data, imageData->images[1].data,
                        imageData->images[2].data, alpha,
                        document->width, document->height, 255.0, out);
                    break;
                default:
                    break;
                }
            }

            if (imageData)
                DestroyImageDataSection(imageData, &allocator);
        }

        DestroyDocument(document, &allocator);
        file.Close();

        if (!decoded || out.empty()) {
            out = {};
            return false;
        }
        return true;
    }
    catch (...) {
        out = {};
        return false;
    }
}

} // namespace YeThumbnail
