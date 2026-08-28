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

enum class Mode {
    Compact,
    Full,
};

inline constexpr int LOGICAL_COMPACT_PANEL_WIDTH = 288;
inline constexpr int LOGICAL_FULL_PANEL_WIDTH = 340;
inline constexpr int LOGICAL_FONT_SIZE = 12;
inline constexpr int LOGICAL_LINE_HEIGHT = 18;
inline constexpr int LOGICAL_ROW_PADDING = 5;
inline constexpr int LOGICAL_COMPACT_HEADER_HEIGHT = 36;
inline constexpr int LOGICAL_FULL_HEADER_HEIGHT = 40;
inline constexpr int LOGICAL_SECTION_HEIGHT = 24;
inline constexpr int LOGICAL_FOOTER_HEIGHT = 36;
inline constexpr int LOGICAL_COMPACT_MAX_HEIGHT = 360;
inline constexpr int LOGICAL_FULL_MAX_HEIGHT = 400;
inline constexpr int LOGICAL_COMPACT_LABEL_WIDTH = 48;
inline constexpr int LOGICAL_FULL_LABEL_WIDTH = 64;
inline constexpr int LOGICAL_COMPACT_PADDING = 12;
inline constexpr int LOGICAL_FULL_PADDING = 16;

inline constexpr uint32_t DARK_PANEL_BACKGROUND = 0xD10A0E1Au;
inline constexpr uint32_t DARK_PANEL_BORDER = 0xFF2D3544u;
inline constexpr uint32_t DARK_TEXT_PRIMARY = 0xFFE0E8F8u;
inline constexpr uint32_t DARK_TEXT_SECONDARY = 0xFFE0E8F8u;
inline constexpr uint32_t DARK_TEXT_MUTED = 0xFF8294BDu;
inline constexpr uint32_t DARK_ACCENT = 0xFF5685DCu;
inline constexpr uint32_t LIGHT_PANEL_BACKGROUND = 0xDBFFFFFFu;
inline constexpr uint32_t LIGHT_PANEL_BORDER = 0xFFD8DDE7u;
inline constexpr uint32_t LIGHT_TEXT_PRIMARY = 0xFF142844u;
inline constexpr uint32_t LIGHT_TEXT_SECONDARY = 0xFF142844u;
inline constexpr uint32_t LIGHT_TEXT_MUTED = 0xFF506EA0u;
inline constexpr uint32_t LIGHT_ACCENT = 0xFF3C64C8u;

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

inline std::size_t utf8SequenceLength(unsigned char first) {
    if ((first & 0x80u) == 0)
        return 1;
    if ((first & 0xE0u) == 0xC0u)
        return 2;
    if ((first & 0xF0u) == 0xE0u)
        return 3;
    if ((first & 0xF8u) == 0xF0u)
        return 4;
    return 1;
}

inline int utf8DisplayUnits(std::string_view codePoint) {
    if (codePoint.empty())
        return 0;
    return static_cast<unsigned char>(codePoint.front()) < 0x80u ? 1 : 2;
}

inline bool isPreferredBreak(std::string_view codePoint) {
    return codePoint == " " || codePoint == "\\" || codePoint == "/" ||
        codePoint == "_" || codePoint == "-" || codePoint == "." ||
        codePoint == ":" || codePoint == ";" || codePoint == ",";
}

// Inserts line breaks only. Every original UTF-8 byte remains present and in order.
inline std::vector<std::string> wrapUtf8(std::string_view value, int maxUnits) {
    maxUnits = std::max(1, maxUnits);
    std::vector<std::string> codePoints;
    for (std::size_t offset = 0; offset < value.size();) {
        if (value[offset] == '\n') {
            codePoints.emplace_back("\n");
            ++offset;
            continue;
        }
        std::size_t length = utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        length = std::min(length, value.size() - offset);
        while (length > 1 && (static_cast<unsigned char>(value[offset + length - 1]) & 0xC0u) != 0x80u)
            --length;
        codePoints.emplace_back(value.substr(offset, length));
        offset += length;
    }

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < codePoints.size()) {
        if (codePoints[start] == "\n") {
            lines.emplace_back();
            ++start;
            continue;
        }

        int units = 0;
        std::size_t end = start;
        std::size_t preferred = std::string::npos;
        while (end < codePoints.size() && codePoints[end] != "\n") {
            const int nextUnits = utf8DisplayUnits(codePoints[end]);
            if (end > start && units + nextUnits > maxUnits)
                break;
            units += nextUnits;
            if (isPreferredBreak(codePoints[end]))
                preferred = end + 1;
            ++end;
            if (units >= maxUnits)
                break;
        }

        std::size_t chosen = end;
        if (end < codePoints.size() && codePoints[end] != "\n" &&
            preferred != std::string::npos && preferred > start) {
            chosen = preferred;
        }
        if (chosen == start)
            chosen = std::min(start + 1, codePoints.size());

        std::string line;
        for (std::size_t index = start; index < chosen; ++index)
            line += codePoints[index];
        lines.push_back(std::move(line));
        start = chosen;
        if (start < codePoints.size() && codePoints[start] == "\n")
            ++start;
    }
    if (lines.empty())
        lines.emplace_back();
    return lines;
}

inline std::string joinWrappedLines(const std::vector<std::string>& lines) {
    std::string result;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index)
            result.push_back('\n');
        result += lines[index];
    }
    return result;
}

inline int valueWidthUnits(Mode mode) {
    const int panelWidth = mode == Mode::Compact ? LOGICAL_COMPACT_PANEL_WIDTH : LOGICAL_FULL_PANEL_WIDTH;
    const int padding = mode == Mode::Compact ? LOGICAL_COMPACT_PADDING : LOGICAL_FULL_PADDING;
    const int labelWidth = mode == Mode::Compact ? LOGICAL_COMPACT_LABEL_WIDTH : LOGICAL_FULL_LABEL_WIDTH;
    // Segoe UI's CJK glyphs occupy close to one em and Latin glyphs average
    // slightly above half an em. Keep a safety margin so native DrawText never
    // introduces an extra unaccounted line that could hide the final text.
    return std::max(1, (panelWidth - padding * 2 - labelWidth - 12) * 17 /
        (LOGICAL_FONT_SIZE * 10));
}

inline int logicalRowHeight(std::string_view value, Mode mode) {
    return static_cast<int>(wrapUtf8(value, valueWidthUnits(mode)).size()) *
        LOGICAL_LINE_HEIGHT + LOGICAL_ROW_PADDING * 2;
}

inline int clampScrollOffset(int contentHeight, int viewportHeight, int offset) {
    return std::clamp(offset, 0, std::max(0, contentHeight - viewportHeight));
}

inline bool useLightPalette(uint8_t blue, uint8_t green, uint8_t red) {
    const int luminance = (static_cast<int>(red) * 299 + static_cast<int>(green) * 587 +
        static_cast<int>(blue) * 114) / 1000;
    return luminance > 128;
}

inline Model build(std::string_view raw, bool chinese, std::string_view colorMode = {}) {
    const auto rows = parseRows(raw);
    Model model;

    const std::string path = findValue(rows, { "路径", "Path" });
    const std::string size = findValue(rows, { "大小", "FileSize" });
    const std::string resolution = findValue(rows, { "分辨率", "Resolution" });
    if (!path.empty()) {
        model.basic.push_back({ chinese ? "文件名" : "Name", fileName(path) });
        model.basic.push_back({ chinese ? "路径" : "Path", path });
        const std::string format = formatName(path);
        if (!format.empty())
            model.basic.push_back({ chinese ? "格式" : "Format", format });
    }
    if (!size.empty())
        model.basic.push_back({ chinese ? "文件大小" : "Size", size });
    if (!resolution.empty())
        model.basic.push_back({ chinese ? "分辨率" : "Dimensions", compactResolution(resolution) });
    if (!colorMode.empty())
        model.basic.push_back({ chinese ? "色彩" : "Color", std::string(colorMode) });

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
        PreferredField{ "创建工具", "Creator tool", { "Xmp.xmp.CreatorTool", "Creator Tool" } },
        PreferredField{ "方向", "Orientation", { "方向", "Exif.Image.Orientation", "Xmp.tiff.Orientation" } },
        PreferredField{ "实例 ID", "Instance ID", { "Xmp.xmpMM.InstanceID", "Instance ID" } },
    };

    for (const auto& field : preferred) {
        const std::string value = findValue(rows, field.aliases);
        if (value.empty())
            continue;
        model.details.push_back({ chinese ? field.zhLabel : field.enLabel, value });
    }
    return model;
}

inline std::vector<Row> compactRows(const Model& model, bool chinese) {
    const std::array<std::string_view, 5> labels = chinese ?
        std::array<std::string_view, 5>{ "格式", "文件大小", "分辨率", "色彩", "文件名" } :
        std::array<std::string_view, 5>{ "Format", "Size", "Dimensions", "Color", "Name" };
    std::vector<Row> result;
    for (const auto label : labels) {
        const auto found = std::ranges::find_if(model.basic, [label](const Row& row) {
            return row.label == label;
            });
        if (found != model.basic.end())
            result.push_back(*found);
    }
    return result;
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

inline int logicalContentHeight(const Model& model, Mode mode, bool chinese) {
    int height = 0;
    const auto rows = mode == Mode::Compact ? compactRows(model, chinese) : model.basic;
    if (mode == Mode::Full)
        height += LOGICAL_SECTION_HEIGHT;
    for (const auto& row : rows)
        height += logicalRowHeight(row.value, mode);
    if (mode == Mode::Full && !model.details.empty()) {
        height += 1 + LOGICAL_SECTION_HEIGHT;
        for (const auto& row : model.details)
            height += logicalRowHeight(row.value, mode);
    }
    return height;
}

}
