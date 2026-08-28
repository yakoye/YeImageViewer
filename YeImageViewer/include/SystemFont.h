#pragma once

#include <Windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace SystemFont {

inline constexpr std::array<std::wstring_view, 4> PREFERRED_FILE_NAMES{
    L"msyh.ttc",
    L"msyhl.ttc",
    L"simhei.ttf",
    L"segoeui.ttf",
};

inline std::optional<std::filesystem::path> findPreferredPath() {
    std::array<wchar_t, MAX_PATH> windowsDirectory{};
    const UINT length = GetWindowsDirectoryW(
        windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (length == 0 || length >= windowsDirectory.size())
        return std::nullopt;

    const std::filesystem::path fontDirectory =
        std::filesystem::path(windowsDirectory.data()) / L"Fonts";
    for (const auto fileName : PREFERRED_FILE_NAMES) {
        const auto candidate = fontDirectory / fileName;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error)
            return candidate;
    }
    return std::nullopt;
}

inline bool readPreferredFont(std::vector<uint8_t>& bytes) {
    bytes.clear();
    const auto path = findPreferredPath();
    if (!path)
        return false;

    std::ifstream file(*path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;

    constexpr std::streamoff MAX_SYSTEM_FONT_BYTES = 64LL * 1024 * 1024;
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > MAX_SYSTEM_FONT_BYTES)
        return false;

    bytes.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        bytes.clear();
        return false;
    }
    return true;
}

}
