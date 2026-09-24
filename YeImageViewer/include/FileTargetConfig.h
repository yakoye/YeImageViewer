#pragma once

#include "ExternalEditorConfig.h"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// 「复制到指定位置」「移动到指定位置」用的目标文件夹。
//
// 交互按这样设计：
//   第一次按快捷键时还没有目标，弹文件夹选择框，选中的那个记下来当当前目标；
//   之后按同一个快捷键直接复制 / 移动过去，不再打扰；
//   想换位置或多攒几个，走右键菜单——最多 5 组，当前目标打勾。
//
// 目标文件夹不存在就创建（含中间层级）；目标里已有同名文件不覆盖，按资源管理器的
// 习惯加 (2)、(3)。静默覆盖用户的文件是不可接受的，这里宁可多出一个文件。
namespace FileTargetConfig {

inline constexpr std::size_t MAX_TARGETS = 5;

struct Model {
    std::vector<std::wstring> targets;   // 目标文件夹的完整路径
    std::size_t active = 0;              // 当前目标在 targets 里的下标
};

// 菜单上显示的名字：取文件夹名。取不到（盘符根目录之类）就退回完整路径。
inline std::wstring displayName(std::wstring_view target) {
    if (target.empty())
        return {};
    std::filesystem::path path(target);
    std::wstring name = path.filename().wstring();
    if (name.empty())
        name = path.parent_path().filename().wstring();
    return name.empty() ? std::wstring(target) : name;
}

inline bool sameTarget(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        wchar_t a = static_cast<wchar_t>(std::towlower(left[index]));
        wchar_t b = static_cast<wchar_t>(std::towlower(right[index]));
        // 两种路径分隔符视为等价
        if (a == L'/') a = L'\\';
        if (b == L'/') b = L'\\';
        if (a != b)
            return false;
    }
    return true;
}

// 加一个目标并设为当前。已经有同一个目标时不重复添加，只是把它设为当前。
// 满 5 个之后再加，替换掉最早的那个——用户明确说了最多 5 组，
// 默默不生效比替换更让人困惑。
inline void addTarget(Model& model, std::wstring_view target) {
    if (target.empty())
        return;
    for (std::size_t index = 0; index < model.targets.size(); ++index) {
        if (sameTarget(model.targets[index], target)) {
            model.active = index;
            return;
        }
    }
    if (model.targets.size() >= MAX_TARGETS) {
        model.targets.erase(model.targets.begin());
        if (model.active > 0)
            --model.active;
    }
    model.targets.emplace_back(target);
    model.active = model.targets.size() - 1;
}

inline void removeTarget(Model& model, std::size_t index) {
    if (index >= model.targets.size())
        return;
    model.targets.erase(model.targets.begin() + index);
    if (model.targets.empty())
        model.active = 0;
    else if (model.active >= model.targets.size())
        model.active = model.targets.size() - 1;
    else if (model.active > index)
        --model.active;
}

inline void setActive(Model& model, std::size_t index) {
    if (index < model.targets.size())
        model.active = index;
}

inline bool hasTarget(const Model& model) {
    return !model.targets.empty() && model.active < model.targets.size();
}

inline std::wstring activeTarget(const Model& model) {
    return hasTarget(model) ? model.targets[model.active] : std::wstring{};
}

// 目标里已有同名文件时，按 a.png -> a (2).png -> a (3).png 让路。
// exists 由调用方注入，纯逻辑部分才能脱离文件系统单测。
// 一万个候选名全被占时放弃并退回原名，交给上层报错，不在这里死循环。
template <typename ExistsFn>
inline std::wstring uniqueFileName(std::wstring_view fileName, ExistsFn exists) {
    if (!exists(std::wstring(fileName)))
        return std::wstring(fileName);

    const std::filesystem::path path(fileName);
    const std::wstring stem = path.stem().wstring();
    const std::wstring extension = path.extension().wstring();
    for (int index = 2; index < 10000; ++index) {
        std::wstring candidate = stem + L" (" + std::to_wstring(index) + L")" + extension;
        if (!exists(candidate))
            return candidate;
    }
    return std::wstring(fileName);
}

// ---- 持久化 ------------------------------------------------------------------
// 和外部编辑器共用同一个配置文件：用户明确提过「文件越少越好」，不再单开一份。
// 两边各管各的键，保存时都会把对方的行原样留下来（见 ExternalEditorConfig::foreignLines）。
inline bool isTargetKey(std::string_view key) {
    if (key == "TargetCount" || key == "TargetActive")
        return true;
    for (std::size_t index = 0; index < MAX_TARGETS; ++index) {
        if (key == "Target" + std::to_string(index))
            return true;
    }
    return false;
}

inline Model load(const std::wstring& filePath) {
    Model model;
    std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
    if (!file)
        return model;

    std::vector<std::wstring> slots(MAX_TARGETS);
    std::size_t count = 0;
    std::size_t active = 0;
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (firstLine && line.starts_with("\xEF\xBB\xBF"))
            line.erase(0, 3);
        firstLine = false;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key(line.data(), equals);
        const std::string value(line.data() + equals + 1, line.size() - equals - 1);
        const auto toIndex = [](const std::string& text) -> std::size_t {
            try { return static_cast<std::size_t>(std::stoul(text)); }
            catch (...) { return 0; }
        };
        if (key == "TargetCount") {
            count = std::min<std::size_t>(toIndex(value), MAX_TARGETS);
            continue;
        }
        if (key == "TargetActive") {
            active = toIndex(value);
            continue;
        }
        for (std::size_t index = 0; index < MAX_TARGETS; ++index) {
            if (key == "Target" + std::to_string(index))
                slots[index] = ExternalEditorConfig::unescape(value);
        }
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (!slots[index].empty())
            model.targets.push_back(std::move(slots[index]));
    }
    model.active = model.targets.empty() ? 0 : std::min(active, model.targets.size() - 1);
    return model;
}

inline bool save(const std::wstring& filePath, const Model& model) {
    if (filePath.empty() || model.targets.size() > MAX_TARGETS)
        return false;
    const auto kept = ExternalEditorConfig::foreignLines(filePath, isTargetKey);
    std::ofstream file(std::filesystem::path(filePath),
        std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << "\xEF\xBB\xBF";
    for (const auto& line : kept)
        file << line << "\r\n";
    file << "TargetCount=" << model.targets.size() << "\r\n";
    file << "TargetActive=" << model.active << "\r\n";
    for (std::size_t index = 0; index < model.targets.size(); ++index) {
        file << "Target" << index << '='
             << ExternalEditorConfig::escape(model.targets[index]) << "\r\n";
    }
    file.flush();
    return file.good();
}

}
