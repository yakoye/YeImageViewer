#pragma once
#include "PsdCompositeDecoder.h"
#include <string>

// 只读图层索引/单层栅格预览，与合成图路径独立。span 生命周期由调用方文件缓冲保证。
namespace PsdLayers
{
using PsdCompositeDecoder::Reader;
struct Channel
{
    int id;
    std::span<const uint8_t> data;
    uint64_t length;
};
struct Layer
{
    std::wstring name;
    int32_t top = 0, left = 0, bottom = 0, right = 0;
    uint8_t opacity = 255;
    bool visible = true, group = false, masked = false, adjustment = false;
    uint32_t sectionType = 0;
    bool clipped = false, effects = false, vectorMask = false, customBlendRanges = false;
    uint8_t fillOpacity = 255;
    std::span<const uint8_t> maskData;
    std::string blend;
    std::vector<Channel> channels;
};
struct Document
{
    uint32_t width = 0, height = 0;
    int depth = 0, mode = 0;
    bool psb = false, valid = false;
    std::span<const uint8_t> colorData, resources;
    std::vector<Layer> layers;
};
inline bool key(std::span<const uint8_t> value, const char *text)
{
    return value.size() == 4 && !std::memcmp(value.data(), text, 4);
}
inline bool longLength(std::span<const uint8_t> tag)
{
    for (auto name :
         {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"})
        if (key(tag, name))
            return true;
    return false;
}
inline int32_t signed32(Reader &r)
{
    return std::bit_cast<int32_t>(static_cast<uint32_t>(r.integer(4)));
}
inline Document read(std::span<const uint8_t> bytes) noexcept
{
    try
    {
        Document doc;
        if (!PsdCompositeDecoder::signature(bytes))
            return {};
        Reader r(bytes);
        r.take(4);
        auto version = r.integer(2);
        if (version != 1 && version != 2)
            return {};
        doc.psb = version == 2;
        for (auto b : r.take(6))
            if (b)
                return {};
        const auto channels = r.integer(2);
        doc.height = static_cast<uint32_t>(r.integer(4));
        doc.width = static_cast<uint32_t>(r.integer(4));
        doc.depth = static_cast<int>(r.integer(2));
        doc.mode = static_cast<int>(r.integer(2));
        if (!channels || channels > 56 || !doc.width || !doc.height || doc.width > (doc.psb ? 300000u : 30000u) ||
            doc.height > (doc.psb ? 300000u : 30000u))
            return {};
        if (doc.depth != 8 && doc.depth != 16 && doc.depth != 32)
            return {};
        doc.colorData = r.take(r.integer(4));
        doc.resources = r.take(r.integer(4));
        auto section = r.take(r.integer(doc.psb ? 8 : 4));
        doc.valid = true;
        if (section.empty())
            return doc;
        Reader mask(section);
        auto layerInfo = mask.take(mask.integer(doc.psb ? 8 : 4));
        if (layerInfo.empty() && mask.remaining() >= 4)
        {
            mask.take(mask.integer(4));
            while (mask.remaining() >= 12)
            {
                auto sig = mask.take(4);
                if (!key(sig, "8BIM") && !key(sig, "8B64"))
                    return {};
                auto tag = mask.take(4);
                const bool layers = key(tag, "Lr16") || key(tag, "Lr32");
                const auto length = mask.integer(doc.psb && longLength(tag) ? 8 : 4);
                auto data = mask.take(length);
                mask.take((4 - length % 4) % 4);
                if (layers)
                {
                    layerInfo = data;
                    break;
                }
            }
        }
        if (layerInfo.empty())
            return doc;
        Reader info(layerInfo);
        const int count = std::abs(static_cast<int>(static_cast<int16_t>(info.integer(2))));
        if (count > 10000 || uint64_t(count) * 34 > info.remaining())
            return {};
        size_t nameBudget = 0;
        for (int i = 0; i < count; ++i)
        {
            Layer layer;
            layer.top = signed32(info);
            layer.left = signed32(info);
            layer.bottom = signed32(info);
            layer.right = signed32(info);
            if (int64_t(layer.bottom) - layer.top < 0 || int64_t(layer.right) - layer.left < 0)
                return {};
            const auto channelCount = info.integer(2);
            if (channelCount > 56)
                return {};
            for (size_t c = 0; c < channelCount; ++c)
            {
                const int id = static_cast<int16_t>(info.integer(2));
                auto length = info.integer(doc.psb ? 8 : 4);
                if (length < 2)
                    return {};
                layer.channels.push_back({id, {}, length});
            }
            if (!key(info.take(4), "8BIM"))
                return {};
            auto blend = info.take(4);
            layer.blend.assign(reinterpret_cast<const char *>(blend.data()), 4);
            layer.opacity = static_cast<uint8_t>(info.integer(1));
            layer.clipped = info.integer(1) != 0;
            layer.visible = (info.integer(1) & 2) == 0;
            info.take(1);
            Reader extra(info.take(info.integer(4)));
            auto layerMask = extra.take(extra.integer(4));
            layer.masked = !layerMask.empty();
            layer.maskData = layerMask;
            const auto ranges = extra.take(extra.integer(4));
            for (size_t b = 0; b < ranges.size(); ++b)
                if (ranges[b] != (b % 4 < 2 ? 0 : 255)) layer.customBlendRanges = true;
            const auto nameSize = extra.integer(1);
            auto name = extra.take(nameSize);
            for (auto c : name)
                layer.name.push_back(c < 128 ? c : L'�');
            extra.take((4 - (nameSize + 1) % 4) % 4);
            while (extra.remaining() >= 12)
            {
                auto sig = extra.take(4);
                if (!key(sig, "8BIM") && !key(sig, "8B64"))
                    break;
                auto tag = extra.take(4);
                const auto length = extra.integer(doc.psb && longLength(tag) ? 8 : 4);
                auto data = extra.take(length);
                extra.take(length % 2);
                if (key(tag, "luni"))
                {
                    Reader unicode(data);
                    const auto units = unicode.integer(4);
                    if (units > 32768 || units * 2 > unicode.remaining())
                        return {};
                    layer.name.clear();
                    for (size_t c = 0; c < units; ++c)
                        layer.name.push_back(static_cast<wchar_t>(unicode.integer(2)));
                    if (!layer.name.empty() && layer.name.back() == 0)
                        layer.name.pop_back();
                }
                if ((key(tag, "lsct") || key(tag, "lsdk")) && data.size() >= 4)
                {
                    Reader type(data);
                    layer.sectionType = static_cast<uint32_t>(type.integer(4));
                    layer.group = layer.sectionType != 0;
                    if (type.remaining() >= 8 && key(type.take(4), "8BIM")) {
                        const auto groupBlend = type.take(4);
                        layer.blend.assign(reinterpret_cast<const char*>(groupBlend.data()), 4);
                    }
                }
                if (key(tag, "lfx2") || key(tag, "lmfx") || key(tag, "lrFX")) layer.effects = true;
                if (key(tag, "artb") || key(tag, "artd") || key(tag, "abdd") ||
                    (key(tag, "brst") && !data.empty()) ||
                    (key(tag, "knko") && !data.empty() && data[0]!=0) ||
                    (key(tag, "tsly") && !data.empty() && data[0]==0)) layer.effects = true;
                if (key(tag, "vmsk") || key(tag, "vsms")) layer.vectorMask = true;
                if (key(tag, "iOpa") && !data.empty()) layer.fillOpacity = data[0];
                for (auto adjustment :
                     {"SoCo", "GdFl", "PtFl", "brit", "levl", "curv", "expA", "vibA", "hue ", "hue2",
                      "blnc", "blwh", "phfl", "mixr", "clrL", "nvrt", "post", "thrs", "grdm", "selc"})
                    if (key(tag, adjustment))
                        layer.adjustment = true;
            }
            if (layer.name.empty())
                layer.name = L"#" + std::to_wstring(i + 1);
            nameBudget += layer.name.size();
            if (nameBudget > 1024 * 1024)
                return {};
            doc.layers.push_back(std::move(layer));
        }
        for (auto &layer : doc.layers)
            for (auto &channel : layer.channels)
                channel.data = info.take(channel.length);
        // 文件记录为自底向顶，先按物理顺序绑定通道，再规范化为界面自顶向底。
        // 分组头位于其子层之后，反转后才能用 sectionType 1/2 -> 3 构树。
        std::reverse(doc.layers.begin(),doc.layers.end());
        return doc;
    }
    catch (...)
    {
        return {};
    }
}
inline void integer(std::vector<uint8_t> &out, uint64_t value, int count)
{
    for (int i = count - 1; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
inline std::vector<uint8_t> channelPixels(const Channel &channel, uint32_t width, uint32_t height, int depth,
                                          bool psb, size_t limit)
{
    const uint64_t row = uint64_t(width) * (depth / 8), total = row * height;
    if (!total || total > limit)
        throw std::runtime_error("layer too large");
    Reader r(channel.data);
    const auto compression = r.integer(2);
    std::vector<uint8_t> output(static_cast<size_t>(total));
    if (compression == 0)
    {
        auto raw = r.take(total);
        std::copy(raw.begin(), raw.end(), output.begin());
    }
    else if (compression == 1)
    {
        std::vector<uint32_t> counts;
        counts.reserve(height);
        for (size_t y = 0; y < height; ++y)
            counts.push_back(static_cast<uint32_t>(r.integer(psb ? 4 : 2)));
        for (size_t y = 0; y < height; ++y)
            PsdCompositeDecoder::unpackBits(
                r.take(counts[y]),
                std::span(output).subspan(y * static_cast<size_t>(row), static_cast<size_t>(row)));
    }
    else if (compression == 2 || compression == 3)
    {
        auto encoded = r.take(r.remaining());
        if (encoded.size() > UINT_MAX || output.size() > UINT_MAX)
            throw std::runtime_error("layer zlib too large");
        z_stream z{};
        z.next_in = const_cast<uint8_t *>(encoded.data());
        z.avail_in = static_cast<uInt>(encoded.size());
        z.next_out = output.data();
        z.avail_out = static_cast<uInt>(output.size());
        if (inflateInit(&z) != Z_OK)
            throw std::runtime_error("layer zlib init");
        int status = inflate(&z, Z_FINISH);
        bool okay = status == Z_STREAM_END && z.total_in == encoded.size() && z.total_out == output.size();
        inflateEnd(&z);
        if (!okay)
            throw std::runtime_error("layer zlib invalid");
        if (compression == 3)
        {
            std::vector<uint8_t> shuffle(static_cast<size_t>(row));
            for (size_t y = 0; y < height; ++y)
            {
                auto *p = output.data() + y * row;
                if (depth == 16)
                {
                    uint16_t previous = 0;
                    for (size_t x = 0; x < width; ++x)
                    {
                        previous = static_cast<uint16_t>(previous + p[x * 2] * 256 + p[x * 2 + 1]);
                        p[x * 2] = static_cast<uint8_t>(previous >> 8);
                        p[x * 2 + 1] = static_cast<uint8_t>(previous);
                    }
                }
                else
                {
                    for (size_t x = 1; x < row; ++x)
                        p[x] = static_cast<uint8_t>(p[x] + p[x - 1]);
                    if (depth == 32)
                    {
                        std::copy_n(p, static_cast<size_t>(row), shuffle.data());
                        for (size_t x = 0; x < width; ++x)
                            for (size_t b = 0; b < 4; ++b)
                                p[x * 4 + b] = shuffle[b * width + x];
                    }
                }
            }
        }
    }
    else
        throw std::runtime_error("layer compression unknown");
    if (r.remaining() == 1)
    {
        if (r.take(1)[0] == 0)
            return output; // 合法的偶数补齐。
        throw std::runtime_error("layer channel invalid padding");
    }
    if (r.remaining())
        throw std::runtime_error("layer channel extra bytes");
    return output;
}
inline PsdCompositeDecoder::Image preview(const Document &doc, size_t index,
                                          size_t memoryLimit = 256ULL << 20) noexcept
{
    try
    {
        if (!doc.valid || index >= doc.layers.size())
            return {};
        const auto &layer = doc.layers[index];
        const uint64_t width = int64_t(layer.right) - layer.left, height = int64_t(layer.bottom) - layer.top;
        const size_t base = doc.mode == 3 || doc.mode == 9 ? 3 : doc.mode == 4 ? 4 : 1;
        if (layer.group || layer.adjustment || !width || !height || width > 300000 || height > 300000 ||
            (doc.mode != 1 && doc.mode != 3 && doc.mode != 4 && doc.mode != 9))
            return {};
        const uint64_t raw = width * height * (doc.depth / 8) * (base + 1);
        // 同时考虑源平面、组装流、合成输出与临时转换；超额拒绝，避免多层预览 OOM。
        if (raw * 4 + width * height * 8 + doc.resources.size() > memoryLimit)
            return {};
        const Channel *alpha = nullptr;
        std::vector<const Channel *> selected(base, nullptr);
        for (const auto &channel : layer.channels)
        {
            if (channel.id >= 0 && static_cast<size_t>(channel.id) < base)
                selected[channel.id] = &channel;
            if (channel.id == -1)
                alpha = &channel;
        }
        for (auto *channel : selected)
            if (!channel)
                return {};
        if (alpha)
            selected.push_back(alpha);
        std::vector<uint8_t> synthetic = {'8', 'B', 'P', 'S'};
        integer(synthetic, doc.psb ? 2 : 1, 2);
        integer(synthetic, 0, 6);
        integer(synthetic, selected.size(), 2);
        integer(synthetic, height, 4);
        integer(synthetic, width, 4);
        integer(synthetic, doc.depth, 2);
        integer(synthetic, doc.mode, 2);
        integer(synthetic, doc.colorData.size(), 4);
        synthetic.insert(synthetic.end(), doc.colorData.begin(), doc.colorData.end());
        // 只带 ICC，不带“无兼容合成图”标志等原文档资源。
        std::vector<uint8_t> resources;
        Reader rr(doc.resources);
        while (rr.remaining())
        {
            auto sig = rr.take(4);
            if (!key(sig, "8BIM"))
                return {};
            auto id = rr.integer(2);
            auto n = rr.integer(1);
            rr.take(n + ((n + 1) % 2));
            auto length = rr.integer(4);
            auto data = rr.take(length);
            rr.take(length % 2);
            if (id == 1039)
            {
                if (length > 16 * 1024 * 1024)
                    return {};
                resources.insert(resources.end(), {'8', 'B', 'I', 'M'});
                integer(resources, id, 2);
                integer(resources, 0, 2);
                integer(resources, length, 4);
                resources.insert(resources.end(), data.begin(), data.end());
                if (length % 2)
                    resources.push_back(0);
            }
        }
        integer(synthetic, resources.size(), 4);
        synthetic.insert(synthetic.end(), resources.begin(), resources.end());
        // 合成解码器以负 layer count 判断最后一个通道为透明度。
        integer(synthetic, alpha ? (doc.psb ? 10 : 6) : 0, doc.psb ? 8 : 4);
        if (alpha)
        {
            integer(synthetic, 2, doc.psb ? 8 : 4);
            integer(synthetic, 65535, 2);
        }
        integer(synthetic, 0, 2);
        for (auto *channel : selected)
        {
            auto bytes = channelPixels(*channel, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                       doc.depth, doc.psb, memoryLimit);
            synthetic.insert(synthetic.end(), bytes.begin(), bytes.end());
        }
        // 合成容器只是复用解码与色彩转换，单层RGB没有最终合成图的白色衬底。
        return PsdCompositeDecoder::decode(synthetic, memoryLimit, false);
    }
    catch (...)
    {
        return {};
    }
}
} // namespace PsdLayers
