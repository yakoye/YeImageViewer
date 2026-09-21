#pragma once

#include <windows.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

// 注册表读写的共用小工具。文件关联、缩略图挂接、「打开方式」登记三处都要写
// HKCU\Software\Classes 下的字符串值，以前各自抄了一份同样的代码。
namespace RegistryWriter {

inline bool setString(HKEY root, const std::wstring& subKey,
    const std::wstring& valueName, const std::wstring& value) {
    HKEY handle = nullptr;
    DWORD disposition = 0;
    LONG result = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, &disposition);
    if (result != ERROR_SUCCESS)
        return false;

    result = RegSetValueExW(handle, valueName.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    return result == ERROR_SUCCESS;
}

// 键不存在也算成功：调用方要的是「之后它不在」，不是「这次真的删掉了」。
inline bool deleteKey(HKEY root, const std::wstring& subKey) {
    const auto result = SHDeleteKeyW(root, subKey.c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ||
        result == ERROR_PATH_NOT_FOUND;
}

inline bool deleteValue(HKEY root, const std::wstring& subKey, const std::wstring& valueName) {
    HKEY handle = nullptr;
    LONG result = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_WRITE, &handle);
    if (result != ERROR_SUCCESS)
        return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;

    result = RegDeleteValueW(handle, valueName.c_str());
    RegCloseKey(handle);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

inline std::wstring queryString(HKEY root, const std::wstring& subKey,
    const std::wstring& valueName) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
        return {};

    wchar_t buffer[512]{};
    DWORD bytes = sizeof(buffer);
    const LONG result = RegQueryValueExW(handle, valueName.c_str(), nullptr, nullptr,
        reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(handle);
    return result == ERROR_SUCCESS ? std::wstring(buffer) : std::wstring{};
}

} // namespace RegistryWriter
