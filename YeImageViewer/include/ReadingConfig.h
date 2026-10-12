#pragma once
#include "ConfigFile.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <string>

// 配对/每图缩放独立键前缀，保存时重新读取其他模块数据，不能用陈旧快照覆盖。
class ReadingConfig {
    std::wstring path_;
    std::map<std::string, int> pairs_;
    std::map<std::string, int64_t> zooms_;
    static std::string encode(std::string_view value) {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
        return result;
    }
    static bool mine(std::string_view key) {
        return key.starts_with("reader.pair.") || key.starts_with("reader.zoom.");
    }
public:
    void load(const std::wstring& path) {
        path_ = path;
        pairs_.clear(); zooms_.clear();
        for (const auto& line : ConfigFile::readLines(path_)) {
            const auto key = ConfigFile::keyOf(line);
            const auto value = ConfigFile::valueOf(line);
            int64_t number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) continue;
            if (key.starts_with("reader.pair.") && number >= 0 && number <= 1)
                pairs_[std::string(key.substr(12))] = static_cast<int>(number);
            if (key.starts_with("reader.zoom.") && number >= 655 && number <= 6553600)
                zooms_[std::string(key.substr(12))] = number;
        }
    }
    int pairing(std::string_view collection) const {
        const auto found = pairs_.find(encode(collection));
        return found == pairs_.end() ? 0 : found->second;
    }
    void setPairing(std::string_view collection, int shift) { pairs_[encode(collection)] = shift & 1; }
    int64_t zoom(std::string_view image) const {
        const auto found = zooms_.find(encode(image));
        return found == zooms_.end() ? 0 : found->second;
    }
    void setZoom(std::string_view image, int64_t zoom) {
        zooms_[encode(image)] = (std::clamp<int64_t>)(zoom, 655, 6553600);
    }
    bool save() const {
        auto lines = ConfigFile::foreignLines(ConfigFile::readLines(path_), mine);
        for (const auto& [key, value] : pairs_) lines.push_back("reader.pair." + key + "=" + std::to_string(value));
        for (const auto& [key, value] : zooms_) lines.push_back("reader.zoom." + key + "=" + std::to_string(value));
        return ConfigFile::writeLines(path_, lines);
    }
};
