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
//   复制和移动各自记住一个默认目标，互不影响；
//   第一次按某个快捷键时若还没有对应目标，弹文件夹选择框并记住；
//   也可以在快捷键设置页预先指定，之后按键直接执行；
//   想换位置或多攒几个，走右键菜单——最多 5 组，各自的当前目标打勾。
//
// 目标文件夹不存在就创建（含中间层级）；目标里已有同名文件不覆盖，按资源管理器的
// 习惯加 (2)、(3)。静默覆盖用户的文件是不可接受的，这里宁可多出一个文件。
namespace FileTargetConfig {

inline constexpr std::size_t MAX_TARGETS = 5;
inline constexpr std::size_t NO_TARGET = static_cast<std::size_t>(-1);

struct Model {
    std::vector<std::wstring> targets;   // 目标文件夹的完整路径
    std::size_t copyActive = NO_TARGET;   // 复制默认目标在 targets 里的下标
    std::size_t moveActive = NO_TARGET;   // 移动默认目标在 targets 里的下标
};

enum class Operation {
    Copy,
    Move,
};

inline std::size_t& activeIndex(Model& model, Operation operation) {
    return operation == Operation::Move ? model.moveActive : model.copyActive;
}

inline std::size_t activeIndex(const Model& model, Operation operation) {
    return operation == Operation::Move ? model.moveActive : model.copyActive;
}

inline void adjustActiveAfterRemoval(std::size_t& active, std::size_t removed,
    std::size_t remaining) {
    if (active == NO_TARGET)
        return;
    if (remaining == 0) {
        active = NO_TARGET;
    }
    else if (active > removed) {
        --active;
    }
    else if (active >= remaining) {
        active = remaining - 1;
    }
}

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
inline void addTarget(Model& model, std::wstring_view target, Operation operation) {
    if (target.empty())
        return;
    for (std::size_t index = 0; index < model.targets.size(); ++index) {
        if (sameTarget(model.targets[index], target)) {
            activeIndex(model, operation) = index;
            return;
        }
    }
    if (model.targets.size() >= MAX_TARGETS) {
        model.targets.erase(model.targets.begin());
        adjustActiveAfterRemoval(model.copyActive, 0, model.targets.size());
        adjustActiveAfterRemoval(model.moveActive, 0, model.targets.size());
    }
    model.targets.emplace_back(target);
    activeIndex(model, operation) = model.targets.size() - 1;
}

inline void removeTarget(Model& model, std::size_t index) {
    if (index >= model.targets.size())
        return;
    model.targets.erase(model.targets.begin() + index);
    adjustActiveAfterRemoval(model.copyActive, index, model.targets.size());
    adjustActiveAfterRemoval(model.moveActive, index, model.targets.size());
}

inline void setActive(Model& model, std::size_t index, Operation operation) {
    if (index < model.targets.size())
        activeIndex(model, operation) = index;
}

inline void clearActive(Model& model, Operation operation) {
    activeIndex(model, operation) = NO_TARGET;
}

inline bool hasTarget(const Model& model, Operation operation) {
    return !model.targets.empty() && activeIndex(model, operation) < model.targets.size();
}

inline std::wstring activeTarget(const Model& model, Operation operation) {
    return hasTarget(model, operation) ?
        model.targets[activeIndex(model, operation)] : std::wstring{};
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
    if (key == "TargetCount" || key == "TargetActive" ||
        key == "TargetCopyActive" || key == "TargetMoveActive")
        return true;
    for (std::size_t index = 0; index < MAX_TARGETS; ++index) {
        if (key == "Target" + std::to_string(index))
            return true;
    }
    return false;
}

inline Model loadFrom(const std::vector<std::string>& lines) {
    Model model;
    std::vector<std::wstring> slots(MAX_TARGETS);
    std::size_t count = 0;
    std::size_t legacyActive = 0;
    std::size_t copyActive = 0;
    std::size_t moveActive = 0;
    bool hasLegacyActive = false;
    bool hasCopyActive = false;
    bool hasMoveActive = false;
    for (const auto& line : lines) {
        const auto keyView = ConfigFile::keyOf(line);
        if (keyView.empty())
            continue;
        const std::string key(keyView);
        const std::string value(ConfigFile::valueOf(line));
        const auto toIndex = [](const std::string& text) -> std::size_t {
            if (text == "-1")
                return NO_TARGET;
            try { return static_cast<std::size_t>(std::stoull(text)); }
            catch (...) { return NO_TARGET; }
        };
        if (key == "TargetCount") {
            count = std::min<std::size_t>(toIndex(value), MAX_TARGETS);
            continue;
        }
        if (key == "TargetActive") {
            legacyActive = toIndex(value);
            hasLegacyActive = true;
            continue;
        }
        if (key == "TargetCopyActive") {
            copyActive = toIndex(value);
            hasCopyActive = true;
            continue;
        }
        if (key == "TargetMoveActive") {
            moveActive = toIndex(value);
            hasMoveActive = true;
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
    const auto normalized = [&](std::size_t active) {
        if (active == NO_TARGET || model.targets.empty())
            return NO_TARGET;
        return std::min(active, model.targets.size() - 1);
    };
    model.copyActive = normalized(hasCopyActive ? copyActive :
        (hasLegacyActive ? legacyActive : NO_TARGET));
    model.moveActive = normalized(hasMoveActive ? moveActive :
        (hasLegacyActive ? legacyActive : NO_TARGET));
    return model;
}

inline Model load(const std::wstring& filePath) {
    return loadFrom(ConfigFile::readLines(filePath));
}

inline Model loadLegacy(const std::wstring& filePath) {
    return loadFrom(ConfigFile::readPlainLines(filePath));
}

inline bool save(const std::wstring& filePath, const Model& model) {
    if (filePath.empty() || model.targets.size() > MAX_TARGETS)
        return false;
    auto lines = ConfigFile::foreignLines(ConfigFile::readLines(filePath), isTargetKey);
    lines.push_back("TargetCount=" + std::to_string(model.targets.size()));
    const auto activeText = [](std::size_t active) {
        return active == NO_TARGET ? std::string("-1") : std::to_string(active);
    };
    lines.push_back("TargetCopyActive=" + activeText(model.copyActive));
    lines.push_back("TargetMoveActive=" + activeText(model.moveActive));
    for (std::size_t index = 0; index < model.targets.size(); ++index) {
        lines.push_back("Target" + std::to_string(index) + "=" +
            ExternalEditorConfig::escape(model.targets[index]));
    }
    return ConfigFile::writeLines(filePath, lines);
}

}
