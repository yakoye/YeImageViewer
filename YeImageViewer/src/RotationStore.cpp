#include "RotationStore.h"

#include "ConfigFile.h"
#include "ExternalEditorConfig.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>

namespace {

constexpr uint32_t MAX_ENTRY_COUNT = 100000;
constexpr uint32_t MAX_PATH_CHARS = 32768;

// 旋转记录原来是自己一个二进制文件。现在和外部编辑器、复制/移动目标一起
// 写进 YeImageViewer.db 的文本区，绿色版落地就只剩本体、缩略图 DLL 和一个配置。
// 路径里什么字符都可能有（包括换行），所以复用编辑器那套转义。
constexpr char ROTATION_PREFIX[] = "Rot";

bool isRotationKey(std::string_view key) {
    return key.starts_with(ROTATION_PREFIX);
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

bool RotationStore::loadFrom(const std::vector<std::string>& lines) {
    std::map<std::wstring, uint8_t> loaded;
    for (const auto& line : lines) {
        const auto key = ConfigFile::keyOf(line);
        if (!isRotationKey(key))
            continue;
        // Rot<序号>=<旋转数>|<转义后的路径>
        const auto value = ConfigFile::valueOf(line);
        const auto bar = value.find('|');
        if (bar == std::string_view::npos || bar == 0)
            continue;
        const int rotation = value[0] - '0';
        if (rotation < 1 || rotation > 3)
            continue;
        auto path = ExternalEditorConfig::unescape(value.substr(bar + 1));
        if (path.empty() || path.size() > MAX_PATH_CHARS)
            continue;
        loaded[std::move(path)] = static_cast<uint8_t>(rotation);
        if (loaded.size() > MAX_ENTRY_COUNT)
            return false;
    }
    rotations_ = std::move(loaded);
    return true;
}

// 返回 false 只表示「没有存储位置可读」。文本区为空是合法状态（设置文件刚建、
// 或者用户从没转过图），读不懂的行一律跳过，绝不把上一次的内存内容留着当结果。
bool RotationStore::load() {
    rotations_.clear();
    if (storagePath_.empty())
        return false;
    return loadFrom(ConfigFile::readLines(storagePath_.wstring()));
}

// 迁移旧的 YeImageViewer.rotations.db：那是自带魔数的二进制文件。
// 读成功就地转写到设置文件里，调用方随后把旧文件删掉。
bool RotationStore::loadLegacyBinary(const std::filesystem::path& legacyPath) {
    rotations_.clear();
    std::ifstream stream(legacyPath, std::ios::binary);
    if (!stream)
        return false;

    constexpr std::array<char, 8> MAGIC{ 'Y', 'E', 'R', 'O', 'T', '1', '\r', '\n' };
    std::array<char, MAGIC.size()> magic{};
    if (!stream.read(magic.data(), magic.size()) || magic != MAGIC)
        return false;

    uint32_t count = 0;
    if (!stream.read(reinterpret_cast<char*>(&count), sizeof(count)) || count > MAX_ENTRY_COUNT)
        return false;

    std::map<std::wstring, uint8_t> loaded;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t pathLength = 0;
        uint8_t rotation = 0;
        if (!stream.read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength)) ||
            pathLength == 0 || pathLength > MAX_PATH_CHARS ||
            !stream.read(reinterpret_cast<char*>(&rotation), sizeof(rotation)) || rotation > 3)
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

    const auto path = storagePath_.wstring();
    auto lines = ConfigFile::foreignLines(ConfigFile::readLines(path), isRotationKey);
    std::size_t index = 0;
    for (const auto& [imagePath, rotation] : rotations_) {
        if (imagePath.empty() || imagePath.size() > MAX_PATH_CHARS)
            return false;
        lines.push_back(std::string(ROTATION_PREFIX) + std::to_string(index++) + "=" +
            static_cast<char>('0' + rotation) + "|" +
            ExternalEditorConfig::escape(imagePath));
    }
    return ConfigFile::writeLines(path, lines);
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
