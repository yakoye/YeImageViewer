#pragma once

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

// EXIF 里大量数值是以「分子/分母」的形式存的：曝光时间 1/125、光圈 28/10、
// 焦距 240/10、曝光补偿 -1/3。这里把它们折成人看得懂的小数。
//
// 单独拎出来是为了能单测：原先它长在 exifParse.cpp 里，而那个文件一包含就把
// 整个 Exiv2 拖进来，测试工程链不了。这段逻辑本身一个外部依赖都不需要。
namespace RationalText {

// 把 "a/b" 折成最多两位小数的字符串；整数就不带小数点。
// 格式不对（空、多个斜杠、非数字、斜杠紧挨着两端）一律返回空串，
// 让上层原样显示原始值，而不是显示一个瞎算出来的数。
inline std::string fromFraction(std::string_view text) {
    if (text.empty())
        return {};

    bool negative = false;
    std::size_t start = 0;
    if (text[0] == '-') {
        negative = true;
        start = 1;
        if (text.size() == 1)   // 单独一个「-」不是数
            return {};
    }

    // 只允许数字和恰好一个斜杠
    std::size_t slash = std::string_view::npos;
    for (std::size_t index = start; index < text.size(); ++index) {
        const char character = text[index];
        if (character >= '0' && character <= '9')
            continue;
        if (character != '/')
            return {};
        if (slash != std::string_view::npos)   // 第二个斜杠
            return {};
        slash = index;
    }

    // 斜杠必须有，且两边都得有数字
    if (slash == std::string_view::npos || slash == start || slash == text.size() - 1)
        return {};

    long long numerator = 0;
    long long denominator = 0;
    try {
        numerator = std::stoll(std::string(text.substr(start, slash - start)));
        denominator = std::stoll(std::string(text.substr(slash + 1)));
    }
    catch (const std::exception&) {
        // 位数多到 long long 装不下，这种值显示原样比显示一个截断的数好
        return {};
    }

    if (denominator == 0)
        denominator = 1;

    double value = static_cast<double>(numerator) / static_cast<double>(denominator);
    if (negative)
        value = -value;         // 负号只作用一次

    std::string result = std::format("{:.2f}", value);
    if (result.size() >= 3 && result.compare(result.size() - 3, 3, ".00") == 0)
        result.erase(result.size() - 3);

    // 「-0」不是人会写的数。-1/1000 这类小负数折完正好是 -0.00，去掉小数部分
    // 就剩一个 -0，看着像出错了。
    if (result == "-0")
        result = "0";

    return result;
}

} // namespace RationalText
