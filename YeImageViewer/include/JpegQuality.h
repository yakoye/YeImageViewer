#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// 从 JPEG 的量化表反推质量因子。
//
// JPEG 文件里不存"质量 85"这个数，只存量化表。libjpeg 写表时按
//     scale = quality < 50 ? 5000 / quality : 200 - quality * 2
//     entry = clamp((standard * scale + 50) / 100, 1, 255)
// 这里反着解出 scale 再换回 quality。
//
// 只对按 libjpeg 那套缩放写表的文件准确。Photoshop、mozjpeg 等用自定义表的
// 编码器算出来只是近似值，所以显示时要标明是估算，不能当成原始设置读出来了。
namespace JpegQuality {

    // JPEG 标准 Annex K 的亮度量化表
    inline constexpr std::array<int, 64> STANDARD_LUMINANCE{
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68, 109, 103, 77,
        24, 35, 55, 64, 81, 104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103, 99,
    };

    // Annex K 的色度量化表。只有色度表时用它兜底（少见但存在）
    inline constexpr std::array<int, 64> STANDARD_CHROMINANCE{
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
    };

    struct Table {
        int id = 0;
        bool sixteenBit = false;
        std::array<int, 64> values{};
    };

    // 扫描 DQT 段（0xFFDB）取出量化表。
    // 只走标记结构，不解码像素，代价是读几百字节。
    inline std::vector<Table> parseTables(std::span<const uint8_t> data) {
        std::vector<Table> tables;
        if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8)
            return tables;   // 不是 JPEG

        std::size_t offset = 2;
        while (offset + 3 < data.size()) {
            if (data[offset] != 0xFF) {
                ++offset;    // 填充字节，跳过
                continue;
            }
            const uint8_t marker = data[offset + 1];
            if (marker == 0xFF) {
                ++offset;
                continue;
            }
            // SOS 之后是熵编码数据，量化表一定在它前面，到此为止
            if (marker == 0xDA || marker == 0xD9)
                break;
            // 这些标记没有长度字段
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                offset += 2;
                continue;
            }
            if (offset + 4 > data.size())
                break;
            const std::size_t length = (static_cast<std::size_t>(data[offset + 2]) << 8) |
                data[offset + 3];
            if (length < 2 || offset + 2 + length > data.size())
                break;

            if (marker == 0xDB) {
                // 一个 DQT 段里可以放多张表，逐张读到段尾
                std::size_t cursor = offset + 4;
                const std::size_t segmentEnd = offset + 2 + length;
                while (cursor < segmentEnd) {
                    const uint8_t spec = data[cursor++];
                    Table table;
                    table.id = spec & 0x0F;
                    table.sixteenBit = (spec >> 4) != 0;
                    const std::size_t entryBytes = table.sixteenBit ? 2 : 1;
                    if (cursor + 64 * entryBytes > segmentEnd)
                        break;
                    for (int i = 0; i < 64; ++i) {
                        if (table.sixteenBit) {
                            table.values[i] = (static_cast<int>(data[cursor]) << 8) | data[cursor + 1];
                            cursor += 2;
                        }
                        else {
                            table.values[i] = data[cursor];
                            ++cursor;
                        }
                    }
                    tables.push_back(table);
                }
            }
            offset += 2 + length;
        }
        return tables;
    }

    // 由一张量化表反推 libjpeg 的 scale。
    // 饱和到上限的项没有信息量（多个 scale 都会被夹成同一个值），要剔除；
    // 取中位数而非平均，免得个别异常项把结果拉偏。
    // 质量 100 时 libjpeg 的 scale 是 0，整张表被夹成全 1。
    // 这种情况能精确判定，不必走反推——反推会因取整落在 99。
    inline bool isAllOnes(const Table& table) {
        return std::ranges::all_of(table.values, [](int v) { return v == 1; });
    }

    inline std::optional<double> estimateScale(const Table& table,
        const std::array<int, 64>& standard) {
        const int saturation = table.sixteenBit ? 32767 : 255;
        std::vector<double> scales;
        scales.reserve(64);
        for (int i = 0; i < 64; ++i) {
            const int actual = table.values[i];
            const int base = standard[i];
            if (base <= 0 || actual <= 0 || actual >= saturation)
                continue;
            // actual = floor((base * scale + 50) / 100)
            // → scale 落在 [(actual*100 - 50)/base, (actual*100 + 49)/base]，取区间中点
            const double low = (actual * 100.0 - 50.0) / base;
            const double high = (actual * 100.0 + 49.0) / base;
            scales.push_back((low + high) / 2.0);
        }
        if (scales.size() < 8)
            return std::nullopt;   // 有效项太少，估不出来就别硬给

        std::ranges::sort(scales);
        return scales[scales.size() / 2];
    }

    // scale → quality，libjpeg 缩放公式的反函数
    inline int scaleToQuality(double scale) {
        if (scale <= 0.0)
            return 100;
        const double quality = scale > 100.0 ? 5000.0 / scale : (200.0 - scale) / 2.0;
        return std::clamp(static_cast<int>(quality + 0.5), 1, 100);
    }

    // 返回估算的质量因子（1~100）。不是 JPEG、没有量化表、或表里有效项太少时返回空。
    inline std::optional<int> estimate(std::span<const uint8_t> data) {
        const auto tables = parseTables(data);
        if (tables.empty())
            return std::nullopt;

        // 优先用亮度表（id 0）：它的标准表数值分布最广，反推最稳
        for (const auto& table : tables) {
            if (table.id != 0)
                continue;
            if (isAllOnes(table))
                return 100;
            if (const auto scale = estimateScale(table, STANDARD_LUMINANCE))
                return scaleToQuality(*scale);
        }
        for (const auto& table : tables) {
            if (const auto scale = estimateScale(table,
                    table.id == 0 ? STANDARD_LUMINANCE : STANDARD_CHROMINANCE))
                return scaleToQuality(*scale);
        }
        return std::nullopt;
    }

}
