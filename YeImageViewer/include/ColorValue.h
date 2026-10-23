#pragma once
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace ColorValue
{
struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb &) const = default;
};
inline std::wstring hex(Rgb value)
{
    wchar_t text[8]{};
    swprintf_s(text, L"#%02X%02X%02X", value.r, value.g, value.b);
    return text;
}
inline std::wstring rgb(Rgb value)
{
    return L"RGB(" + std::to_wstring(value.r) + L", " + std::to_wstring(value.g) + L", " +
           std::to_wstring(value.b) + L")";
}
inline std::optional<Rgb> parse(std::wstring_view text)
{
    if (text.starts_with(L'#'))
        text.remove_prefix(1);
    if (text.size() != 6)
        return {};
    unsigned value = 0;
    for (auto c : text)
    {
        unsigned nibble = c >= L'0' && c <= L'9'   ? c - L'0'
                          : c >= L'a' && c <= L'f' ? c - L'a' + 10
                          : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                                   : 16;
        if (nibble > 15)
            return {};
        value = value * 16 + nibble;
    }
    return Rgb{static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
}
inline std::optional<uint8_t> channel(std::wstring_view text)
{
    if (text.empty() || text.size() > 3)
        return {};
    unsigned value = 0;
    for (auto c : text)
    {
        if (c < L'0' || c > L'9')
            return {};
        value = value * 10 + c - L'0';
    }
    if (value > 255)
        return {};
    return static_cast<uint8_t>(value);
}
constexpr bool screenPoint(int x, int y, int left, int top, int width, int height)
{
    return static_cast<int64_t>(x) - left >= 0 && static_cast<int64_t>(x) - left < width &&
           static_cast<int64_t>(y) - top >= 0 && static_cast<int64_t>(y) - top < height;
}
} // namespace ColorValue
