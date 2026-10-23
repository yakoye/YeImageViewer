#pragma once
#include "PickerCore.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace PickerPreferences {
inline PickerCore::Settings load(const std::filesystem::path& path) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > 1024) return {};
    std::ifstream file(path);
    std::string magic, remainder;
    int version = 0;
    PickerCore::Settings result;
    if (!(file >> magic >> version >> result.format >> result.grid >> result.zoom >> result.relative >> result.showGrid >> result.crosshair) ||
        magic != "YeImageViewer.ColorPicker" || (version != 1 && version != 2 && version != 3)) return {};
    if(version==3 && !(file>>result.showHints))return {};
    if((version==1 && result.format>=4) || !result.valid() || (file >> remainder)) return {};
    return result;
}
// 只写自己的偏好，不碰4096字节 DB。独立临时名，失败不删除旧文件。
inline bool save(const std::filesystem::path& path, const PickerCore::Settings& value) {
    if (!value.valid()) return false;
    const auto temporary = std::filesystem::path(path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    const std::string data = "YeImageViewer.ColorPicker 3\n" + std::to_string(value.format) + " " + std::to_string(value.grid) + " " +
        std::to_string(value.zoom) + " " + std::to_string(value.relative) + " " + std::to_string(value.showGrid) + " " + std::to_string(value.crosshair) + " " + std::to_string(value.showHints) + "\n";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool okay = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (okay) okay = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!okay) DeleteFileW(temporary.c_str());
    return okay;
}
}
