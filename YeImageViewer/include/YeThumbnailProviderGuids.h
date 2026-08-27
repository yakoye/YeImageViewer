#pragma once

#include <guiddef.h>
#include <string_view>

namespace YeThumbnailProviderGuids {
inline constexpr std::wstring_view ProviderClsidString = L"{F9586ADD-BF40-4B7F-AD92-28869946E34A}";
inline constexpr std::wstring_view ThumbnailProviderHandlerString = L"{E357FCCD-A995-4576-B01F-234630154E96}";

// {F9586ADD-BF40-4B7F-AD92-28869946E34A}
inline constexpr GUID CLSID_YeThumbnailProvider = {
    0xf9586add,
    0xbf40,
    0x4b7f,
    { 0xad, 0x92, 0x28, 0x86, 0x99, 0x46, 0xe3, 0x4a }
};
}
