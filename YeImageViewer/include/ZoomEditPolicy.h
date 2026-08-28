#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace ZoomEditPolicy {

inline constexpr int MIN_PERCENT = 1;
inline constexpr int MAX_PERCENT = 10000;
inline constexpr std::size_t MAX_DIGITS = 5;

inline bool appendDigit(std::string& text, char digit, bool replaceSelection) {
    if (digit < '0' || digit > '9')
        return false;

    if (replaceSelection)
        text.clear();
    if (text.size() >= MAX_DIGITS)
        return false;

    text.push_back(digit);
    return true;
}

inline std::optional<int> parsePercent(std::string_view text) {
    if (text.empty())
        return std::nullopt;

    int value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9')
            return std::nullopt;
        value = value * 10 + character - '0';
    }
    return std::clamp(value, MIN_PERCENT, MAX_PERCENT);
}

}
