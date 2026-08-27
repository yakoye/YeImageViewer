#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ImageInfoPresentation {

struct Row {
    std::string label;
    std::string value;
};

struct Model {
    std::vector<Row> basic;
    std::vector<Row> details;
};

inline constexpr int LOGICAL_PANEL_WIDTH = 360;
inline constexpr int LOGICAL_FONT_SIZE = 16;
inline constexpr int LOGICAL_HEADER_HEIGHT = 48;
inline constexpr int LOGICAL_SECTION_HEIGHT = 28;
inline constexpr int LOGICAL_ROW_HEIGHT = 30;
inline constexpr int LOGICAL_FOOTER_HEIGHT = 36;
inline constexpr std::size_t MAX_DETAIL_ROWS = 6;
inline constexpr uint32_t PANEL_BACKGROUND = 0x99000000u;
inline constexpr uint32_t PANEL_BORDER = 0xFF334155u;
inline constexpr uint32_t PANEL_ACCENT = 0xFF3B82F6u;
inline constexpr uint32_t TEXT_PRIMARY = 0xFFF1F5F9u;
inline constexpr uint32_t TEXT_SECONDARY = 0xFFCBD5E1u;
inline constexpr uint32_t TEXT_MUTED = 0xFF8491A5u;

inline std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

inline std::vector<Row> parseRows(std::string_view raw) {
    std::vector<Row> rows;
    std::size_t offset = 0;
    while (offset <= raw.size()) {
        const std::size_t end = raw.find('\n', offset);
        const std::string line = trim(raw.substr(offset,
            end == std::string_view::npos ? raw.size() - offset : end - offset));
        const std::size_t separator = line.find(':');
        if (separator != std::string::npos) {
            const std::string key = trim(std::string_view(line).substr(0, separator));
            const std::string value = trim(std::string_view(line).substr(separator + 1));
            if (!key.empty() && !value.empty() && value != "-")
                rows.push_back({ key, value });
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1;
    }
    return rows;
}

inline std::string findValue(const std::vector<Row>& rows,
    std::initializer_list<std::string_view> aliases) {
    for (const auto alias : aliases) {
        for (const auto& row : rows) {
            if (row.label == alias)
                return row.value;
        }
    }
    return {};
}

inline std::string fileName(std::string_view path) {
    const std::size_t separator = path.find_last_of("/\\");
    return std::string(path.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

inline std::string formatName(std::string_view path) {
    const std::string name = fileName(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
        return {};
    std::string result = name.substr(dot + 1);
    std::ranges::transform(result, result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
        });
    return result;
}

inline std::string compactResolution(std::string value) {
    const std::size_t separator = value.find('x');
    if (separator != std::string::npos)
        value.replace(separator, 1, " × ");
    if (!value.ends_with(" px"))
        value += " px";
    return value;
}

inline std::string truncateUtf8(std::string value, std::size_t maxBytes = 84) {
    if (value.size() <= maxBytes)
        return value;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0u) == 0x80u)
        --cut;
    value.resize(cut);
    value += "...";
    return value;
}

inline Model build(std::string_view raw, bool chinese) {
    const auto rows = parseRows(raw);
    Model model;

    const std::string path = findValue(rows, { "路径", "Path" });
    const std::string size = findValue(rows, { "大小", "FileSize" });
    const std::string resolution = findValue(rows, { "分辨率", "Resolution" });
    if (!path.empty()) {
        model.basic.push_back({ chinese ? "文件名" : "Name", truncateUtf8(fileName(path), 54) });
        const std::string format = formatName(path);
        if (!format.empty())
            model.basic.push_back({ chinese ? "格式" : "Format", format });
    }
    if (!size.empty())
        model.basic.push_back({ chinese ? "文件大小" : "Size", size });
    if (!resolution.empty())
        model.basic.push_back({ chinese ? "分辨率" : "Dimensions", compactResolution(resolution) });

    struct PreferredField {
        const char* zhLabel;
        const char* enLabel;
        std::initializer_list<std::string_view> aliases;
    };
    const std::array preferred{
        PreferredField{ "拍摄时间", "Captured", { "原始日期时间", "Exif.Photo.DateTimeOriginal", "日期时间", "Exif.Image.DateTime" } },
        PreferredField{ "相机", "Camera", { "型号", "Exif.Image.Model" } },
        PreferredField{ "制造商", "Maker", { "制造商", "Exif.Image.Make" } },
        PreferredField{ "镜头", "Lens", { "镜头型号", "Exif.Photo.LensModel" } },
        PreferredField{ "曝光时间", "Exposure", { "曝光时间", "Exif.Photo.ExposureTime" } },
        PreferredField{ "光圈", "Aperture", { "光圈值", "Exif.Photo.FNumber" } },
        PreferredField{ "ISO", "ISO", { "ISO感光度", "Exif.Photo.ISOSpeedRatings", "Exif.Photo.PhotographicSensitivity" } },
        PreferredField{ "焦距", "Focal length", { "焦距", "Exif.Photo.FocalLength" } },
        PreferredField{ "曝光补偿", "Exposure bias", { "曝光补偿值", "Exif.Photo.ExposureBiasValue" } },
        PreferredField{ "白平衡", "White balance", { "白平衡", "Exif.Photo.WhiteBalance" } },
        PreferredField{ "色彩空间", "Color space", { "色彩空间", "Exif.Photo.ColorSpace" } },
    };

    for (const auto& field : preferred) {
        const std::string value = findValue(rows, field.aliases);
        if (value.empty())
            continue;
        model.details.push_back({ chinese ? field.zhLabel : field.enLabel, truncateUtf8(value) });
        if (model.details.size() == MAX_DETAIL_ROWS)
            break;
    }
    return model;
}

constexpr uint32_t blendBgra(uint32_t destination, uint32_t overlay) {
    const uint32_t alpha = overlay >> 24;
    const uint32_t inverseAlpha = 255 - alpha;
    const auto blend = [alpha, inverseAlpha](uint32_t foreground, uint32_t behind) constexpr {
        return (foreground * alpha + behind * inverseAlpha + 127) / 255;
        };
    const uint32_t blue = blend(overlay & 0xFFu, destination & 0xFFu);
    const uint32_t green = blend((overlay >> 8) & 0xFFu, (destination >> 8) & 0xFFu);
    const uint32_t red = blend((overlay >> 16) & 0xFFu, (destination >> 16) & 0xFFu);
    return 0xFF000000u | red << 16 | green << 8 | blue;
}

inline int logicalPanelHeight(const Model& model) {
    int height = LOGICAL_HEADER_HEIGHT + LOGICAL_SECTION_HEIGHT +
        static_cast<int>(model.basic.size()) * LOGICAL_ROW_HEIGHT;
    if (!model.details.empty())
        height += 1 + LOGICAL_SECTION_HEIGHT + static_cast<int>(model.details.size()) * LOGICAL_ROW_HEIGHT;
    return height + LOGICAL_FOOTER_HEIGHT + 16;
}

}
