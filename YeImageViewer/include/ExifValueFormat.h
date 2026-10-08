#pragma once

#include "UiLanguage.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// EXIF 里摄影参数的原始写法和人看的写法差得很远：尼康把 1/60 秒存成 10/600，
// 光圈 f/8 存成 80/10，焦距 89.9mm 存成 899/10，白平衡「自动」就是一个 0。
// 通用的「折成小数再把原始值括起来」对这几项特别难看——信息面板里一整列
// 「曝光时间 0.02 (10/600)」「光圈 8 (80/10)」「白平衡 0」「方向 1」，
// 既不是摄影人熟悉的写法，也不是普通人看得懂的话。
//
// 这里按 EXIF 规范（CIPA DC-008）把常见字段翻成惯用写法。拿不准的一律返回
// std::nullopt，让上层退回原来的通用显示——猜错比不猜更糟。
//
// 单独成头是为了能单测：exifParse.cpp 一包含就把整个 Exiv2 拖进来，测试工程链不了。
namespace ExifValueFormat {

struct Rational {
    long long numerator = 0;
    long long denominator = 1;
};

// 接受 "a/b" 和 "a" 两种写法（EXIF 的 short/long 类型没有分母）。
// 带一个前导负号；分母为 0 视为无效——那不是「整数」，是坏数据。
inline std::optional<Rational> parseRational(std::string_view text) {
    if (text.empty())
        return std::nullopt;

    bool negative = false;
    std::size_t start = 0;
    if (text[0] == '-' || text[0] == '+') {
        negative = text[0] == '-';
        start = 1;
        if (text.size() == 1)
            return std::nullopt;
    }

    std::size_t slash = std::string_view::npos;
    for (std::size_t index = start; index < text.size(); ++index) {
        const char character = text[index];
        if (character >= '0' && character <= '9')
            continue;
        if (character != '/' || slash != std::string_view::npos)
            return std::nullopt;
        slash = index;
    }

    const auto toNumber = [](std::string_view digits) -> std::optional<long long> {
        if (digits.empty() || digits.size() > 18)   // 18 位以内 long long 稳装得下
            return std::nullopt;
        long long value = 0;
        for (const char character : digits)
            value = value * 10 + (character - '0');
        return value;
    };

    long long numerator = 0;
    long long denominator = 1;
    if (slash == std::string_view::npos) {
        const auto parsed = toNumber(text.substr(start));
        if (!parsed)
            return std::nullopt;
        numerator = *parsed;
    }
    else {
        const auto top = toNumber(text.substr(start, slash - start));
        const auto bottom = toNumber(text.substr(slash + 1));
        if (!top || !bottom || *bottom == 0)
            return std::nullopt;
        numerator = *top;
        denominator = *bottom;
    }

    if (negative)
        numerator = -numerator;
    return Rational{ numerator, denominator };
}

inline double toDouble(const Rational& value) {
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

// 去掉小数点后多余的零：8.00 → 8，2.80 → 2.8。
// 顺手消掉「-0」——那不是人会写的数，看着像算错了。
inline std::string trimTrailingZeros(std::string text) {
    if (text.find('.') == std::string::npos)
        return text;
    while (!text.empty() && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    if (text == "-0" || text.empty())
        text = "0";
    return text;
}

inline std::string decimals(double value, int digits) {
    char buffer[64]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    if (written <= 0)
        return {};
    return trimTrailingZeros(std::string(buffer, static_cast<std::size_t>(written)));
}

// 快门：一秒以内摄影界一律写成 1/x，一秒以上写小数。
// 相机写进 EXIF 的分数五花八门（10/600、3/1000、1/60 都见过），所以不照抄分母，
// 按比值重新算一个——10/600 要显示成 1/60 而不是 1/600。
inline std::string formatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        return {};
    if (seconds == 0)
        return "0 s";
    if (seconds >= 1.0)
        return decimals(seconds, 2) + " s";
    const double inverse = 1.0 / seconds;
    if (inverse >= 1e9)         // 再快就不是真实快门了，别印一串天文数字
        return {};
    // 1/1.6 这类慢门：取整会变成 1/2，差了三分之一档，所以小于 10 时保留一位小数
    const std::string shown = inverse < 10.0 ?
        decimals(inverse, 1) : decimals(std::round(inverse), 0);
    return "1/" + shown + " s";
}

// Exif.Photo.ExposureTime —— 直接是秒数
inline std::optional<std::string> exposureTime(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->numerator < 0)
        return std::nullopt;
    auto text = formatSeconds(toDouble(*parsed));
    if (text.empty())
        return std::nullopt;
    return text;
}

// Exif.Photo.ShutterSpeedValue —— APEX 值，秒数 = 2^(-S)
inline std::optional<std::string> shutterSpeedValue(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed)
        return std::nullopt;
    const double apex = toDouble(*parsed);
    if (!std::isfinite(apex) || apex < -32.0 || apex > 32.0)
        return std::nullopt;
    auto text = formatSeconds(std::pow(2.0, -apex));
    if (text.empty())
        return std::nullopt;
    return text;
}

// Exif.Photo.FNumber —— 直接是 f 值
inline std::optional<std::string> fNumber(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->numerator <= 0)
        return std::nullopt;
    const double value = toDouble(*parsed);
    if (!std::isfinite(value) || value > 1000.0)
        return std::nullopt;
    return "f/" + decimals(value, 1);
}

// Exif.Photo.ApertureValue / MaxApertureValue —— APEX 值，f 值 = 2^(A/2)
inline std::optional<std::string> apertureValue(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->numerator < 0)
        return std::nullopt;
    const double apex = toDouble(*parsed);
    if (!std::isfinite(apex) || apex > 32.0)
        return std::nullopt;
    return "f/" + decimals(std::pow(2.0, apex / 2.0), 1);
}

inline std::optional<std::string> focalLength(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->numerator < 0)
        return std::nullopt;
    const double value = toDouble(*parsed);
    if (!std::isfinite(value) || value > 100000.0)
        return std::nullopt;
    return decimals(value, 1) + " mm";
}

// 曝光补偿：带符号才看得出偏哪边。相机常用三分之一档，EXIF 存成 1/3、2/3，
// 折成 0.33 反而不如原样——分母是 2 或 3 时保留分数写法。
inline std::optional<std::string> exposureBias(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed)
        return std::nullopt;

    long long numerator = parsed->numerator;
    long long denominator = parsed->denominator;
    if (numerator == 0)
        return std::string("0 EV");

    const bool negative = numerator < 0;
    long long top = negative ? -numerator : numerator;
    long long bottom = denominator;
    while (bottom != 0) {       // 约分
        const long long remainder = top % bottom;
        top = bottom;
        bottom = remainder;
    }
    const long long divisor = top == 0 ? 1 : top;
    const long long reducedTop = (negative ? -numerator : numerator) / divisor;
    const long long reducedBottom = denominator / divisor;

    const char* sign = negative ? "-" : "+";
    if (reducedBottom == 1)
        return std::string(sign) + std::to_string(reducedTop) + " EV";
    if (reducedBottom == 2 || reducedBottom == 3)
        return std::string(sign) + std::to_string(reducedTop) + "/" +
            std::to_string(reducedBottom) + " EV";

    const double value = toDouble(*parsed);
    if (!std::isfinite(value))
        return std::nullopt;
    const std::string shown = decimals(value, 2);
    if (shown.empty() || shown == "0")
        return std::string("0 EV");
    return (shown[0] == '-' ? shown : "+" + shown) + " EV";
}

inline std::optional<std::string> subjectDistance(std::string_view raw) {
    const auto parsed = parseRational(raw);
    if (!parsed)
        return std::nullopt;
    // 规范里 0xFFFFFFFF/1 表示无穷远，0 表示未知
    if (parsed->numerator == 0xFFFFFFFFLL || parsed->numerator < 0)
        return std::nullopt;
    const double value = toDouble(*parsed);
    if (!std::isfinite(value) || value <= 0 || value > 100000.0)
        return std::nullopt;
    return decimals(value, 2) + " m";
}

// ---- 枚举字段 ----
//
// 规范里这些标签存的是一个光秃秃的整数。面板上「白平衡 0」「测光模式 5」
// 对谁都没有意义，所以逐个翻成词。表以外的值不翻——厂商私有扩展猜不得。

struct EnumLabel {
    long long value;
    const char* zh;
    const char* en;
    const char* tw;
};

struct EnumTable {
    std::string_view tag;
    std::string_view secondTag;     // 同一组取值常被两个标签共用，省掉一份重复
    const EnumLabel* labels;
    std::size_t count;
};

namespace detail {

inline constexpr EnumLabel ORIENTATION[]{
    { 1, "正常", "Normal", "正常" },
    { 2, "水平翻转", "Mirrored", "水平翻轉" },
    { 3, "旋转 180°", "Rotated 180°", "旋轉 180°" },
    { 4, "垂直翻转", "Flipped", "垂直翻轉" },
    { 5, "顺时针 90° 并翻转", "Mirrored and rotated 90° CW", "順時針 90° 並翻轉" },
    { 6, "顺时针 90°", "Rotated 90° CW", "順時針 90°" },
    { 7, "逆时针 90° 并翻转", "Mirrored and rotated 90° CCW", "逆時針 90° 並翻轉" },
    { 8, "逆时针 90°", "Rotated 90° CCW", "逆時針 90°" },
};

inline constexpr EnumLabel RESOLUTION_UNIT[]{
    { 1, "无单位", "None", "無單位" },
    { 2, "英寸", "Inch", "英吋" },
    { 3, "厘米", "Centimeter", "公分" },
};

inline constexpr EnumLabel YCBCR_POSITIONING[]{
    { 1, "居中", "Centered", "置中" },
    { 2, "协同", "Co-sited", "協同" },
};

inline constexpr EnumLabel EXPOSURE_PROGRAM[]{
    { 0, "未定义", "Not defined", "未定義" },
    { 1, "手动", "Manual", "手動" },
    { 2, "程序自动", "Program AE", "程式自動" },
    { 3, "光圈优先", "Aperture priority", "光圈優先" },
    { 4, "快门优先", "Shutter priority", "快門優先" },
    { 5, "创意程序", "Creative", "創意程式" },
    { 6, "运动程序", "Action", "運動程式" },
    { 7, "人像模式", "Portrait", "人像模式" },
    { 8, "风景模式", "Landscape", "風景模式" },
};

inline constexpr EnumLabel METERING_MODE[]{
    { 0, "未知", "Unknown", "未知" },
    { 1, "平均测光", "Average", "平均測光" },
    { 2, "中央重点测光", "Center-weighted average", "中央重點測光" },
    { 3, "点测光", "Spot", "點測光" },
    { 4, "多点测光", "Multi-spot", "多點測光" },
    { 5, "多分区测光", "Multi-segment", "多分區測光" },
    { 6, "局部测光", "Partial", "局部測光" },
    { 255, "其他", "Other", "其他" },
};

inline constexpr EnumLabel LIGHT_SOURCE[]{
    { 0, "未知", "Unknown", "未知" },
    { 1, "日光", "Daylight", "日光" },
    { 2, "荧光灯", "Fluorescent", "螢光燈" },
    { 3, "钨丝灯", "Tungsten", "鎢絲燈" },
    { 4, "闪光灯", "Flash", "閃光燈" },
    { 9, "晴天", "Fine weather", "晴天" },
    { 10, "阴天", "Cloudy weather", "陰天" },
    { 11, "阴影", "Shade", "陰影" },
    { 12, "日光色荧光灯", "Daylight fluorescent", "日光色螢光燈" },
    { 13, "白天白色荧光灯", "Day white fluorescent", "白天白色螢光燈" },
    { 14, "冷白荧光灯", "Cool white fluorescent", "冷白螢光燈" },
    { 15, "白色荧光灯", "White fluorescent", "白色螢光燈" },
    { 17, "标准光源 A", "Standard light A", "標準光源 A" },
    { 18, "标准光源 B", "Standard light B", "標準光源 B" },
    { 19, "标准光源 C", "Standard light C", "標準光源 C" },
    { 20, "D55", "D55", "D55" },
    { 21, "D65", "D65", "D65" },
    { 22, "D75", "D75", "D75" },
    { 23, "D50", "D50", "D50" },
    { 24, "ISO 钨丝灯棚拍", "ISO studio tungsten", "ISO 鎢絲燈棚拍" },
    { 255, "其他", "Other", "其他" },
};

inline constexpr EnumLabel SENSING_METHOD[]{
    { 1, "未定义", "Not defined", "未定義" },
    { 2, "单片彩色传感器", "One-chip color area", "單晶片彩色感測器" },
    { 3, "两片彩色传感器", "Two-chip color area", "兩晶片彩色感測器" },
    { 4, "三片彩色传感器", "Three-chip color area", "三晶片彩色感測器" },
    { 5, "顺序彩色传感器", "Color sequential area", "循序彩色感測器" },
    { 7, "三线传感器", "Trilinear", "三線感測器" },
    { 8, "顺序彩色线性传感器", "Color sequential linear", "循序彩色線性感測器" },
};

inline constexpr EnumLabel FILE_SOURCE[]{
    { 1, "胶片扫描", "Film scanner", "底片掃描" },
    { 2, "反射稿扫描", "Reflection print scanner", "反射稿掃描" },
    { 3, "数码相机", "Digital camera", "數位相機" },
};

inline constexpr EnumLabel SCENE_TYPE[]{
    { 1, "直接拍摄", "Directly photographed", "直接拍攝" },
};

inline constexpr EnumLabel CUSTOM_RENDERED[]{
    { 0, "常规处理", "Normal", "常規處理" },
    { 1, "自定义处理", "Custom", "自訂處理" },
};

inline constexpr EnumLabel EXPOSURE_MODE[]{
    { 0, "自动曝光", "Auto", "自動曝光" },
    { 1, "手动曝光", "Manual", "手動曝光" },
    { 2, "自动包围曝光", "Auto bracket", "自動包圍曝光" },
};

inline constexpr EnumLabel WHITE_BALANCE[]{
    { 0, "自动", "Auto", "自動" },
    { 1, "手动", "Manual", "手動" },
};

inline constexpr EnumLabel SCENE_CAPTURE_TYPE[]{
    { 0, "标准", "Standard", "標準" },
    { 1, "风景", "Landscape", "風景" },
    { 2, "人像", "Portrait", "人像" },
    { 3, "夜景", "Night scene", "夜景" },
};

inline constexpr EnumLabel GAIN_CONTROL[]{
    { 0, "无", "None", "無" },
    { 1, "低增益上调", "Low gain up", "低增益上調" },
    { 2, "高增益上调", "High gain up", "高增益上調" },
    { 3, "低增益下调", "Low gain down", "低增益下調" },
    { 4, "高增益下调", "High gain down", "高增益下調" },
};

inline constexpr EnumLabel NORMAL_LOW_HIGH[]{
    { 0, "正常", "Normal", "正常" },
    { 1, "弱", "Low", "弱" },
    { 2, "强", "High", "強" },
};

inline constexpr EnumLabel SHARPNESS[]{
    { 0, "正常", "Normal", "正常" },
    { 1, "柔和", "Soft", "柔和" },
    { 2, "锐利", "Hard", "銳利" },
};

inline constexpr EnumLabel SUBJECT_DISTANCE_RANGE[]{
    { 0, "未知", "Unknown", "未知" },
    { 1, "微距", "Macro", "微距" },
    { 2, "近景", "Close view", "近景" },
    { 3, "远景", "Distant view", "遠景" },
};

inline constexpr EnumLabel COLOR_SPACE[]{
    { 1, "sRGB", "sRGB", "sRGB" },
    { 2, "Adobe RGB", "Adobe RGB", "Adobe RGB" },
    { 65535, "未校准", "Uncalibrated", "未校準" },
};

inline constexpr EnumLabel COMPOSITE_IMAGE[]{
    { 0, "未记录", "Not recorded", "未記錄" },
    { 1, "非合成", "Not composite", "非合成" },
    { 2, "合成（通用）", "General composite", "合成（通用）" },
    { 3, "合成（单次曝光）", "Composite captured at once", "合成（單次曝光）" },
};

template <std::size_t N>
constexpr EnumTable makeTable(std::string_view tag, std::string_view secondTag,
    const EnumLabel(&labels)[N]) {
    return EnumTable{ tag, secondTag, labels, N };
}

inline constexpr std::array TABLES{
    makeTable("Exif.Image.Orientation", "Exif.Thumbnail.Orientation", ORIENTATION),
    makeTable("Exif.Image.ResolutionUnit", "Exif.Thumbnail.ResolutionUnit", RESOLUTION_UNIT),
    makeTable("Exif.Photo.FocalPlaneResolutionUnit", {}, RESOLUTION_UNIT),
    makeTable("Exif.Image.YCbCrPositioning", {}, YCBCR_POSITIONING),
    makeTable("Exif.Photo.ExposureProgram", {}, EXPOSURE_PROGRAM),
    makeTable("Exif.Photo.MeteringMode", {}, METERING_MODE),
    makeTable("Exif.Photo.LightSource", {}, LIGHT_SOURCE),
    makeTable("Exif.Photo.SensingMethod", {}, SENSING_METHOD),
    makeTable("Exif.Photo.FileSource", {}, FILE_SOURCE),
    makeTable("Exif.Photo.SceneType", {}, SCENE_TYPE),
    makeTable("Exif.Photo.CustomRendered", {}, CUSTOM_RENDERED),
    makeTable("Exif.Photo.ExposureMode", {}, EXPOSURE_MODE),
    makeTable("Exif.Photo.WhiteBalance", {}, WHITE_BALANCE),
    makeTable("Exif.Photo.SceneCaptureType", {}, SCENE_CAPTURE_TYPE),
    makeTable("Exif.Photo.GainControl", {}, GAIN_CONTROL),
    makeTable("Exif.Photo.Contrast", "Exif.Photo.Saturation", NORMAL_LOW_HIGH),
    makeTable("Exif.Photo.Sharpness", {}, SHARPNESS),
    makeTable("Exif.Photo.SubjectDistanceRange", {}, SUBJECT_DISTANCE_RANGE),
    makeTable("Exif.Photo.ColorSpace", {}, COLOR_SPACE),
    makeTable("Exif.Photo.CompositeImage", {}, COMPOSITE_IMAGE),
};

} // namespace detail

// 闪光灯是位域不是枚举：第 0 位「是否闪了」，第 3~4 位是闪光模式，
// 第 5 位「本机没有闪光灯」，第 6 位红眼。只翻这几位，别的位原样不提。
inline std::optional<std::string> flash(std::string_view raw, uint32_t language) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->denominator != 1 || parsed->numerator < 0 || parsed->numerator > 0xFFFF)
        return std::nullopt;
    const auto bits = static_cast<unsigned>(parsed->numerator);

    if ((bits & 0x20u) != 0)
        return std::string(UiLanguage::pick(language, "无闪光灯", "No flash function", "無閃光燈"));

    std::string text((bits & 0x01u) != 0 ?
        UiLanguage::pick(language, "已闪光", "Fired", "已閃光") :
        UiLanguage::pick(language, "未闪光", "Did not fire", "未閃光"));

    switch ((bits >> 3) & 0x03u) {
    case 1:
        text += UiLanguage::pick(language, "，强制开启", ", compulsory on", "，強制開啟");
        break;
    case 2:
        text += UiLanguage::pick(language, "，强制关闭", ", compulsory off", "，強制關閉");
        break;
    case 3:
        text += UiLanguage::pick(language, "，自动", ", auto", "，自動");
        break;
    default:
        break;
    }
    if ((bits & 0x40u) != 0)
        text += UiLanguage::pick(language, "，防红眼", ", red-eye reduction", "，防紅眼");
    return text;
}

// 整数枚举：查到就翻，查不到返回 nullopt 让上层原样显示。
inline std::optional<std::string> enumerated(std::string_view tag, std::string_view raw,
    uint32_t language) {
    const auto parsed = parseRational(raw);
    if (!parsed || parsed->denominator != 1)
        return std::nullopt;

    for (const auto& table : detail::TABLES) {
        if (table.tag != tag && (table.secondTag.empty() || table.secondTag != tag))
            continue;
        for (std::size_t index = 0; index < table.count; ++index) {
            const auto& label = table.labels[index];
            if (label.value == parsed->numerator)
                return std::string(UiLanguage::pick(language, label.zh, label.en, label.tw));
        }
        return std::nullopt;    // 这个标签认得，但这个取值不在规范里，不猜
    }
    return std::nullopt;
}

// 总入口：认得的标签给惯用写法，不认得的返回 nullopt，上层照旧。
inline std::optional<std::string> format(std::string_view tag, std::string_view raw,
    uint32_t language) {
    if (raw.empty())
        return std::nullopt;

    if (tag == "Exif.Photo.ExposureTime" || tag == "Exif.Image.ExposureTime")
        return exposureTime(raw);
    if (tag == "Exif.Photo.ShutterSpeedValue")
        return shutterSpeedValue(raw);
    if (tag == "Exif.Photo.FNumber" || tag == "Exif.Image.FNumber")
        return fNumber(raw);
    if (tag == "Exif.Photo.ApertureValue" || tag == "Exif.Photo.MaxApertureValue")
        return apertureValue(raw);
    if (tag == "Exif.Photo.FocalLength")
        return focalLength(raw);
    if (tag == "Exif.Photo.FocalLengthIn35mmFilm") {
        const auto parsed = parseRational(raw);
        if (!parsed || parsed->denominator != 1 || parsed->numerator <= 0)
            return std::nullopt;
        return std::to_string(parsed->numerator) + " mm";
    }
    if (tag == "Exif.Photo.ExposureBiasValue")
        return exposureBias(raw);
    if (tag == "Exif.Photo.SubjectDistance")
        return subjectDistance(raw);
    if (tag == "Exif.Photo.Flash")
        return flash(raw, language);
    return enumerated(tag, raw, language);
}

} // namespace ExifValueFormat
