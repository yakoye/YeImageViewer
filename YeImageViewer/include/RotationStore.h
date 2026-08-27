#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

class RotationStore {
public:
    RotationStore() = default;
    explicit RotationStore(std::filesystem::path storagePath);

    void setStoragePath(std::filesystem::path storagePath);
    bool load();
    bool save() const;

    int get(std::wstring_view imagePath) const;
    void set(std::wstring_view imagePath, int quarterTurnsCounterClockwise);
    void erase(std::wstring_view imagePath);
    size_t size() const { return rotations_.size(); }

private:
    static std::wstring normalizePath(std::wstring_view imagePath);

    std::filesystem::path storagePath_;
    std::map<std::wstring, uint8_t> rotations_;
};
