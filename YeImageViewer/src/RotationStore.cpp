#include "RotationStore.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>

namespace {

constexpr std::array<char, 8> MAGIC{ 'Y', 'E', 'R', 'O', 'T', '1', '\r', '\n' };
constexpr uint32_t MAX_ENTRY_COUNT = 100000;
constexpr uint32_t MAX_PATH_CHARS = 32768;

template<typename T>
bool readValue(std::istream& stream, T& value) {
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

template<typename T>
void writeValue(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

}

RotationStore::RotationStore(std::filesystem::path storagePath)
    : storagePath_(std::move(storagePath)) {
}

void RotationStore::setStoragePath(std::filesystem::path storagePath) {
    storagePath_ = std::move(storagePath);
}

std::wstring RotationStore::normalizePath(std::wstring_view imagePath) {
    if (imagePath.empty())
        return {};

    std::error_code error;
    auto path = std::filesystem::absolute(std::filesystem::path(imagePath), error);
    if (error)
        path = std::filesystem::path(imagePath);
    path = path.lexically_normal();

    auto normalized = path.wstring();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return normalized;
}

bool RotationStore::load() {
    rotations_.clear();
    if (storagePath_.empty())
        return false;

    std::ifstream stream(storagePath_, std::ios::binary);
    if (!stream)
        return !std::filesystem::exists(storagePath_);

    std::array<char, MAGIC.size()> magic{};
    if (!stream.read(magic.data(), magic.size()) || magic != MAGIC)
        return false;

    uint32_t count = 0;
    if (!readValue(stream, count) || count > MAX_ENTRY_COUNT)
        return false;

    std::map<std::wstring, uint8_t> loaded;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t pathLength = 0;
        uint8_t rotation = 0;
        if (!readValue(stream, pathLength) || pathLength == 0 || pathLength > MAX_PATH_CHARS ||
            !readValue(stream, rotation) || rotation > 3)
            return false;

        std::wstring path(pathLength, L'\0');
        if (!stream.read(reinterpret_cast<char*>(path.data()),
            static_cast<std::streamsize>(pathLength) * sizeof(wchar_t)))
            return false;
        loaded[std::move(path)] = rotation;
    }

    rotations_ = std::move(loaded);
    return true;
}

bool RotationStore::save() const {
    if (storagePath_.empty() || rotations_.size() > MAX_ENTRY_COUNT)
        return false;

    auto temporaryPath = storagePath_;
    temporaryPath += L".tmp";
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;

        stream.write(MAGIC.data(), MAGIC.size());
        const auto count = static_cast<uint32_t>(rotations_.size());
        writeValue(stream, count);
        for (const auto& [path, rotation] : rotations_) {
            if (path.empty() || path.size() > MAX_PATH_CHARS)
                return false;
            const auto pathLength = static_cast<uint32_t>(path.size());
            writeValue(stream, pathLength);
            writeValue(stream, rotation);
            stream.write(reinterpret_cast<const char*>(path.data()),
                static_cast<std::streamsize>(path.size()) * sizeof(wchar_t));
        }
        stream.flush();
        if (!stream)
            return false;
    }

    if (!MoveFileExW(temporaryPath.c_str(), storagePath_.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return false;
    }
    return true;
}

int RotationStore::get(std::wstring_view imagePath) const {
    const auto key = normalizePath(imagePath);
    const auto found = rotations_.find(key);
    return found == rotations_.end() ? 0 : found->second;
}

void RotationStore::set(std::wstring_view imagePath, int quarterTurnsCounterClockwise) {
    const auto key = normalizePath(imagePath);
    if (key.empty())
        return;

    const auto normalizedRotation = static_cast<uint8_t>(quarterTurnsCounterClockwise & 3);
    if (normalizedRotation == 0)
        rotations_.erase(key);
    else
        rotations_[key] = normalizedRotation;
}

void RotationStore::erase(std::wstring_view imagePath) {
    rotations_.erase(normalizePath(imagePath));
}
