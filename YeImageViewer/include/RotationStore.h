#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class RotationStore {
public:
    RotationStore() = default;
    explicit RotationStore(std::filesystem::path storagePath);

    void setStoragePath(std::filesystem::path storagePath);
    bool load();
    bool save() const;

    // 迁移用：读旧的 YeImageViewer.rotations.db（自带魔数的二进制格式）。
    // 读到内存后调用 save() 就写进新的设置文件，旧文件由调用方删除。
    bool loadLegacyBinary(const std::filesystem::path& legacyPath);

    int get(std::wstring_view imagePath) const;
    void set(std::wstring_view imagePath, int quarterTurnsCounterClockwise);
    void erase(std::wstring_view imagePath);
    size_t size() const { return rotations_.size(); }

private:
    bool loadFrom(const std::vector<std::string>& lines);
    static std::wstring normalizePath(std::wstring_view imagePath);

    std::filesystem::path storagePath_;
    std::map<std::wstring, uint8_t> rotations_;
};
