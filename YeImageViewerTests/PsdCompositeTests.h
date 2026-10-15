#pragma once
#include "PsdCompositeDecoder.h"
#include "RawDecodePolicy.h"
#include "CmykColor.h"
#include <filesystem>
#include <fstream>

#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "lcms2.lib")

inline void expectPsdComposites() {
    passOrFail("RAW 成功码不能掩盖数据错误", RawDecodePolicy::valid(0, 0) && !RawDecodePolicy::valid(0, 18486281) && !RawDecodePolicy::valid(-2, 0));
    auto inks = CmykColor::convert(std::vector<uint8_t>{0,255,255,0, 255,0,255,0, 255,255,0,0, 0,0,0,255},4,1,{});
    passOrFail("CMYK 转换保留 K 和 RGB 通道顺序", inks == std::vector<uint8_t>({0,0,255,255,0,255,0,255,255,0,0,255,0,0,0,255}));
    passOrFail("CMYK 非法缓冲拒绝", CmykColor::convert(std::vector<uint8_t>{0},4,1,{}).empty());
    const std::filesystem::path root = L"test/compatibility";
    int cases = 0;
    for (const auto& file : std::filesystem::directory_iterator(root)) {
        if (file.path().extension() != L".psd" && file.path().extension() != L".psb") continue;
        std::ifstream stream(file.path(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
        const auto image = PsdCompositeDecoder::decode(bytes);
        passOrFail("PSD/PSB 色彩模式、位深、压缩合成图 " + file.path().filename().string(),
            image.width == 4 && image.height == 2 && image.bgra.size() == 32);
        // 各模型的编码量化不同；红、绿、蓝与黑白的布局必须保留。
        if (image.bgra.size() == 32) {
            const bool gray = file.path().filename().string().find("Gray") != std::string::npos;
            const auto& p = image.bgra;
            passOrFail("PSD/PSB 通道、方向和黑白值 " + file.path().filename().string(),
                p[12] > 245 && p[13] > 245 && p[14] > 245 && p[16] < 10 && p[17] < 10 && p[18] < 10 &&
                (gray || (p[2] > p[0] + 80 && p[5] > p[4] + 80 && p[8] > p[10] + 80)));
        }
        for (size_t length : {size_t(0), size_t(25), bytes.size() / 2, bytes.size() - 1}) {
            passOrFail("PSD/PSB 截断不能补零成功", PsdCompositeDecoder::decode(std::span(bytes).first(length)).bgra.empty());
        }
        passOrFail("PSD/PSB 内存预算阻止解压炸弹", PsdCompositeDecoder::decode(bytes, 16).bgra.empty());
        ++cases;
    }
    passOrFail("PSD/PSB 夹具矩阵未遗漏", cases == 48);
    std::vector<uint8_t> row(4);
    bool caught = false;
    try { PsdCompositeDecoder::unpackBits(std::vector<uint8_t>{252, 1}, row); } catch (...) { caught = true; }
    passOrFail("PSD PackBits 越界拒绝", caught);
    caught = false;
    try { PsdCompositeDecoder::unpackBits(std::vector<uint8_t>{3, 1}, row); } catch (...) { caught = true; }
    passOrFail("PSD PackBits 缺失字节拒绝", caught);

    // 1057 资源的 hasRealMergedData 为 false 时，存在像素也只是占位合成图。
    // 正计数图层外的附加通道可能是专色，不能擅自当成 alpha。
    for (bool realMerged : {false, true}) {
        std::vector<uint8_t> data{'8','B','P','S'};
        auto be = [&](uint64_t value, size_t length) {for (size_t i = length; i > 0; --i) data.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));};
        be(1,2); be(0,6); be(4,2); be(1,4); be(1,4); be(8,2); be(3,2); be(0,4);
        be(18,4); // 资源头 12 字节，5 字节数据加偶数补齐。
        data.insert(data.end(), {'8','B','I','M'}); be(1057,2); be(0,2); be(5,4);
        be(1,4); be(realMerged ? 1 : 0,1); be(0,1);
        be(6,4); be(2,4); be(1,2); // 图层计数为正，不含合成透明。
        be(0,2); data.insert(data.end(), {255,0,0,0});
        const auto image = PsdCompositeDecoder::decode(data);
        passOrFail(realMerged ? "PSD 附加专色不误作透明" : "PSD 未保存真实合成图必须失败",
            realMerged ? image.bgra == std::vector<uint8_t>({0,0,255,255}) && !image.hasAlpha : image.bgra.empty());
    }

    // 不依赖外部写入器的 32 位浮点、ZIP 预测和真正合成透明通道夹具。
    for (int depth : {8, 16, 32}) {
        std::vector<uint8_t> data{'8','B','P','S'};
        auto be = [&](uint64_t value, size_t length) {for (size_t i = length; i > 0; --i) data.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));};
        be(1,2); be(0,6); be(4,2); be(1,4); be(3,4); be(depth,2); be(3,2);
        be(0,4); be(0,4); be(6,4); be(2,4); be(65535,2); be(3,2);
        std::vector<uint8_t> raw;
        const std::vector<float> values{1,0.5f,0, 0,1,0.5f, 0.5f,0,1, 0,0.5f,1};
        for(float value : values) {
            const uint32_t code = depth == 32 ? std::bit_cast<uint32_t>(value) : static_cast<uint32_t>(std::lround(value * (depth == 16 ? 65535 : 255)));
            for(int b = depth / 8 - 1; b >= 0; --b) raw.push_back(static_cast<uint8_t>(code >> (b * 8)));
        }
        const size_t rowLength = 3 * depth / 8;
        for(size_t row = 0; row < 4; ++row) {
            auto* bytes = raw.data() + row * rowLength;
            if(depth == 16) {
                for(int x = 2; x > 0; --x) {
                    const auto difference = static_cast<uint16_t>((bytes[x*2]*256+bytes[x*2+1])-(bytes[x*2-2]*256+bytes[x*2-1]));
                    bytes[x*2] = difference >> 8; bytes[x*2+1] = difference & 255;
                }
            } else {
                if(depth == 32) {
                    std::vector<uint8_t> shuffled(rowLength);
                    for(size_t b = 0; b < 4; ++b) for(size_t x = 0; x < 3; ++x) shuffled[b*3+x] = bytes[x*4+b];
                    std::copy(shuffled.begin(),shuffled.end(),bytes);
                }
                for(size_t x = rowLength - 1; x > 0; --x) bytes[x] -= bytes[x-1];
            }
        }
        uLongf length = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> zipped(length);
        compress2(zipped.data(),&length,raw.data(),static_cast<uLong>(raw.size()),9); zipped.resize(length);
        data.insert(data.end(),zipped.begin(),zipped.end());
        auto image = PsdCompositeDecoder::decode(data);
        passOrFail("PSD 8/16/32 位 ZIP 预测及合成透明", image.bgra == std::vector<uint8_t>({128,0,255,0, 0,255,128,128, 255,128,0,255}));
        data.back() ^= 1;
        passOrFail("PSD ZIP 校验损坏必须失败", PsdCompositeDecoder::decode(data).bgra.empty());
    }
}
