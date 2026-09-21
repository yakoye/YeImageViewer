#pragma once

#include <cwctype>
#include <string>
#include <string_view>

namespace FileAssociationNaming {
inline std::wstring NormalizeExtension(std::wstring_view extension) {
    while (!extension.empty() && extension.front() == L'.') {
        extension.remove_prefix(1);
    }

    std::wstring normalized(extension);
    for (auto& ch : normalized) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return normalized;
}

inline std::wstring BuildProgId(std::wstring_view extension) {
    return L"YeImageViewer.ImageFile." + NormalizeExtension(extension);
}

inline std::wstring BuildTypeName(std::wstring_view extension) {
    auto normalized = NormalizeExtension(extension);
    return normalized.empty() ? L"YeImageViewer 图像" : L"YeImageViewer " + normalized + L" 图像";
}

inline std::wstring BuildExtensionKey(std::wstring_view extension) {
    return L"Software\\Classes\\." + NormalizeExtension(extension);
}

inline std::wstring BuildProgIdKey(std::wstring_view extension) {
    return L"Software\\Classes\\" + BuildProgId(extension);
}

// 「打开方式」列表读的是扩展名下的 OpenWithProgids：写进去就能在右键菜单里出现，
// 而且不会动默认程序——默认程序是扩展名键的默认值，两回事。
inline std::wstring BuildOpenWithProgidsKey(std::wstring_view extension) {
    return BuildExtensionKey(extension) + L"\\OpenWithProgids";
}

// Windows 按 exe 的文件名在 Applications 下找程序，路径不参与。
inline std::wstring ExecutableFileName(std::wstring_view applicationPath) {
    const auto separator = applicationPath.find_last_of(L"\\/");
    return std::wstring(separator == std::wstring_view::npos ?
        applicationPath : applicationPath.substr(separator + 1));
}

inline std::wstring BuildApplicationKey(std::wstring_view applicationPath) {
    return L"Software\\Classes\\Applications\\" + ExecutableFileName(applicationPath);
}
}
