#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace ExternalEditorConfig {

inline constexpr std::size_t MAX_EDITORS = 10;
inline constexpr wchar_t FILE_NAME[] = L"YeImageViewer.editors.ini";

struct Entry {
    std::wstring name;
    std::wstring path;

    bool operator==(const Entry&) const = default;
};

inline std::wstring configPath(std::wstring_view settingsPath) {
    std::filesystem::path path(settingsPath);
    path.replace_filename(FILE_NAME);
    return path.wstring();
}

inline std::wstring defaultName(std::wstring_view executablePath) {
    std::filesystem::path path(executablePath);
    std::wstring name = path.stem().wstring();
    return name.empty() ? path.filename().wstring() : name;
}

inline std::wstring resolvedName(std::wstring_view requestedName,
    std::wstring_view executablePath) {
    const auto first = requestedName.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos)
        return defaultName(executablePath);
    const auto last = requestedName.find_last_not_of(L" \t\r\n");
    return std::wstring(requestedName.substr(first, last - first + 1));
}

inline std::wstring menuLabel(const Entry& entry, bool chinese) {
    return (chinese ? L"在 " : L"Open in ") + entry.name;
}

inline void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

inline std::string toUtf8(std::wstring_view value) {
    std::string output;
    output.reserve(value.size() * 2);
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(value[index]);
#if WCHAR_MAX <= 0xFFFF
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF &&
            index + 1 < value.size()) {
            const std::uint32_t low = static_cast<std::uint32_t>(value[index + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) +
                    (low - 0xDC00);
                ++index;
            }
        }
#endif
        if (codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            codePoint = 0xFFFD;
        }
        appendUtf8(output, codePoint);
    }
    return output;
}

inline void appendWide(std::wstring& output, std::uint32_t codePoint) {
#if WCHAR_MAX <= 0xFFFF
    if (codePoint > 0xFFFF) {
        codePoint -= 0x10000;
        output.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
        output.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
        return;
    }
#endif
    output.push_back(static_cast<wchar_t>(codePoint));
}

inline std::wstring fromUtf8(std::string_view value) {
    std::wstring output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::uint32_t codePoint = 0;
        std::size_t length = 0;
        if (first <= 0x7F) {
            codePoint = first;
            length = 1;
        }
        else if ((first & 0xE0) == 0xC0) {
            codePoint = first & 0x1F;
            length = 2;
        }
        else if ((first & 0xF0) == 0xE0) {
            codePoint = first & 0x0F;
            length = 3;
        }
        else if ((first & 0xF8) == 0xF0) {
            codePoint = first & 0x07;
            length = 4;
        }
        else {
            appendWide(output, 0xFFFD);
            ++index;
            continue;
        }
        bool valid = index + length <= value.size();
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }
        const std::uint32_t minimum = length == 1 ? 0 :
            (length == 2 ? 0x80 : (length == 3 ? 0x800 : 0x10000));
        valid = valid && codePoint >= minimum && codePoint <= 0x10FFFF &&
            !(codePoint >= 0xD800 && codePoint <= 0xDFFF);
        appendWide(output, valid ? codePoint : 0xFFFD);
        index += valid ? length : 1;
    }
    return output;
}

inline std::string escape(std::wstring_view value) {
    const std::string utf8 = toUtf8(value);
    std::string result;
    result.reserve(utf8.size());
    constexpr char digits[] = "0123456789ABCDEF";
    for (const unsigned char character : utf8) {
        if (character == '%' || character == '\r' || character == '\n') {
            result.push_back('%');
            result.push_back(digits[character >> 4]);
            result.push_back(digits[character & 0x0F]);
        }
        else {
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

constexpr int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

inline std::wstring unescape(std::string_view value) {
    std::string utf8;
    utf8.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                utf8.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        utf8.push_back(value[index]);
    }
    return fromUtf8(utf8);
}

inline std::vector<Entry> load(const std::wstring& filePath) {
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file)
        return {};
    std::array<std::wstring, MAX_EDITORS> names;
    std::array<std::wstring, MAX_EDITORS> paths;
    std::size_t count = 0;
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (firstLine && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3);
        firstLine = false;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string_view key(line.data(), equals);
        const std::string_view value(line.data() + equals + 1,
            line.size() - equals - 1);
        if (key == "Count") {
            try {
                count = std::min<std::size_t>(std::stoul(std::string(value)), MAX_EDITORS);
            }
            catch (...) {
                count = 0;
            }
            continue;
        }
        for (std::size_t index = 0; index < MAX_EDITORS; ++index) {
            if (key == "Name" + std::to_string(index))
                names[index] = unescape(value);
            else if (key == "Path" + std::to_string(index))
                paths[index] = unescape(value);
        }
    }
    std::vector<Entry> result;
    for (std::size_t index = 0; index < count; ++index) {
        if (!paths[index].empty()) {
            result.push_back({ resolvedName(names[index], paths[index]),
                std::move(paths[index]) });
        }
    }
    return result;
}

inline bool save(const std::wstring& filePath,
    const std::vector<Entry>& entries) {
    if (filePath.empty() || entries.size() > MAX_EDITORS)
        return false;
    for (const auto& entry : entries) {
        if (entry.name.empty() || entry.path.empty())
            return false;
    }
    std::ofstream file(std::filesystem::path(filePath),
        std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << "\xEF\xBB\xBF[Editors]\r\nCount=" << entries.size() << "\r\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        file << "Name" << index << '=' << escape(entries[index].name) << "\r\n";
        file << "Path" << index << '=' << escape(entries[index].path) << "\r\n";
    }
    file.flush();
    return file.good();
}

inline bool pathEquals(std::wstring_view left, std::wstring_view right) {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(),
            [](wchar_t a, wchar_t b) {
                return std::towlower(a) == std::towlower(b);
            });
}

inline bool add(std::vector<Entry>& entries, Entry entry) {
    if (entry.path.empty())
        return false;
    entry.name = resolvedName(entry.name, entry.path);
    const auto duplicate = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& existing) {
            return pathEquals(existing.path, entry.path);
        });
    if (duplicate != entries.end()) {
        *duplicate = std::move(entry);
        return true;
    }
    if (entries.size() >= MAX_EDITORS)
        return false;
    entries.push_back(std::move(entry));
    return true;
}

inline std::wstring quoteImageArgument(std::wstring_view imagePath) {
    std::wstring result;
    result.reserve(imagePath.size() + 2);
    result.push_back(L'"');
    result.append(imagePath);
    result.push_back(L'"');
    return result;
}

} // namespace ExternalEditorConfig
