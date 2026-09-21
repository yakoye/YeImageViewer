#pragma once

#include "FileAssociationNaming.h"
#include "RegistryWriter.h"

#include <shlobj.h>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

// 把程序登记进 Windows 的「打开方式」。这和「文件关联」是两件事：
//   关联   —— 改扩展名键的默认值，抢走双击行为；
//   打开方式 —— 只是让 YeImageViewer 出现在右键「打开方式」的候选里，默认程序不动。
// 不登记的话，用户每次选「打开方式」都得翻到 exe 的安装路径再手动选一次，
// 装完就能在列表里看到，是安装程序该做的事。
namespace OpenWithRegistrar {

inline constexpr const wchar_t* FRIENDLY_NAME = L"YeImageViewer";

// 一条扩展名的登记：确保 ProgId 存在（否则「打开方式」里点了也打不开），
// 再把 ProgId 挂到该扩展名的 OpenWithProgids 下。
inline bool registerExtension(const std::wstring& applicationPath,
    const std::wstring& extension) {
    const std::wstring progId = FileAssociationNaming::BuildProgId(extension);
    const std::wstring progIdKey = FileAssociationNaming::BuildProgIdKey(extension);

    if (!RegistryWriter::setString(HKEY_CURRENT_USER, progIdKey, L"",
        FileAssociationNaming::BuildTypeName(extension)))
        return false;
    // 不写 DefaultIcon：图标留给系统缩略图，和文件关联那边保持一致。
    if (!RegistryWriter::setString(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open\\command",
        L"", L"\"" + applicationPath + L"\" \"%1\""))
        return false;

    return RegistryWriter::setString(HKEY_CURRENT_USER,
        FileAssociationNaming::BuildOpenWithProgidsKey(extension), progId, L"");
}

inline bool unregisterExtension(const std::wstring& extension) {
    return RegistryWriter::deleteValue(HKEY_CURRENT_USER,
        FileAssociationNaming::BuildOpenWithProgidsKey(extension),
        FileAssociationNaming::BuildProgId(extension));
}

// Applications\YeImageViewer.exe 这一份是给「更多应用」用的：SupportedTypes 列出
// 支持的扩展名，Windows 才肯在这些类型的候选里显示本程序。
inline bool registerApplication(const std::wstring& applicationPath,
    const std::vector<std::wstring>& extensions) {
    const std::wstring appKey = FileAssociationNaming::BuildApplicationKey(applicationPath);
    bool ok = RegistryWriter::setString(HKEY_CURRENT_USER, appKey,
        L"FriendlyAppName", FRIENDLY_NAME);
    ok = RegistryWriter::setString(HKEY_CURRENT_USER, appKey + L"\\shell\\open\\command",
        L"", L"\"" + applicationPath + L"\" \"%1\"") && ok;
    ok = RegistryWriter::setString(HKEY_CURRENT_USER, appKey + L"\\shell\\open",
        L"FriendlyAppName", FRIENDLY_NAME) && ok;

    const std::wstring supportedTypesKey = appKey + L"\\SupportedTypes";
    for (const auto& extension : extensions) {
        const std::wstring normalized = FileAssociationNaming::NormalizeExtension(extension);
        if (normalized.empty())
            continue;
        ok = RegistryWriter::setString(HKEY_CURRENT_USER, supportedTypesKey,
            L"." + normalized, L"") && ok;
        ok = registerExtension(applicationPath, normalized) && ok;
    }
    return ok;
}

inline bool unregisterApplication(const std::wstring& applicationPath,
    const std::vector<std::wstring>& extensions) {
    bool ok = true;
    for (const auto& extension : extensions)
        ok = unregisterExtension(FileAssociationNaming::NormalizeExtension(extension)) && ok;
    return RegistryWriter::deleteKey(HKEY_CURRENT_USER,
        FileAssociationNaming::BuildApplicationKey(applicationPath)) && ok;
}

// 资源管理器缓存着这份列表，不通知的话要等下次登录才看得到新条目。
// 用 FLUSHNOWAIT：通知照发，但不等资源管理器处理完。安装脚本是同步等这个进程退出的，
// 一旦 shell 忙着（远程桌面下尤其常见），同步等待会把安装整个挂住。
inline void notifyShell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
}

} // namespace OpenWithRegistrar
