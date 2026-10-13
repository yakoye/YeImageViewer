#pragma once
#include <windows.h>
#include <string>
#include <string_view>

// 原生回归测试专用进程隔离，不替换生产注册逻辑或伪造成功结果。
// 必须同时开启交互探针且给出限定前缀的随机键；失败时拒绝运行，不能回落到真实 HKCU。
class RegistryTestSandbox {
    HKEY root = nullptr;
    bool overridden = false;
public:
    bool valid = true;
    RegistryTestSandbox() {
        wchar_t key[256]{}, probe[32768]{};
        if (!GetEnvironmentVariableW(L"YEIMAGEVIEWER_TEST_REGISTRY_SANDBOX",key,256)) return;
        constexpr std::wstring_view prefix = L"Software\\YeImageViewerTests\\Native-";
        const std::wstring name(key);
        valid = GetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",probe,32768) != 0 &&
            name.starts_with(prefix) && name.size() == prefix.size()+32;
        for (size_t i=prefix.size(); valid && i<name.size(); ++i)
            valid = (name[i]>=L'0' && name[i]<=L'9') || (name[i]>=L'a' && name[i]<=L'f');
        if (!valid) return;
        valid = RegCreateKeyExW(HKEY_CURRENT_USER,name.c_str(),0,nullptr,0,KEY_ALL_ACCESS,
            nullptr,&root,nullptr) == ERROR_SUCCESS;
        if (valid) valid = overridden = RegOverridePredefKey(HKEY_CURRENT_USER,root) == ERROR_SUCCESS;
    }
    ~RegistryTestSandbox() {
        if (overridden) RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);
        if (root) RegCloseKey(root);
    }
};
