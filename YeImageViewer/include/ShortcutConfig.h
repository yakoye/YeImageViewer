#pragma once

#include "UiLanguage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ShortcutConfig {

inline constexpr uint32_t STORAGE_MAGIC = 0x594B4559u; // "YKEY"
inline constexpr uint32_t STORAGE_VERSION = 4;

// 各历史版本的动作数量。新动作只能追加在 Action 末尾：动作的枚举值就是它在存储里的
// 下标，中间插入会让其后所有已保存的自定义绑定整体错位。升级时要按「存的是哪一版」
// 决定从第几个动作开始补默认值，按固定的版本 1 数量去补会把后来版本的绑定洗掉。
inline constexpr std::size_t VERSION1_BINDING_COUNT = 30;
inline constexpr std::size_t VERSION2_BINDING_COUNT = 31;
inline constexpr std::size_t VERSION3_BINDING_COUNT = 32;
inline constexpr std::size_t MAGIC_INDEX = 0;
inline constexpr std::size_t VERSION_INDEX = 1;
inline constexpr std::size_t WHEEL_BASE_INDEX = 2;
inline constexpr std::size_t BINDING_BASE_INDEX = 16;

inline constexpr uint32_t MODIFIER_CONTROL = 1u << 16;
inline constexpr uint32_t MODIFIER_SHIFT = 1u << 17;
inline constexpr uint32_t MODIFIER_ALT = 1u << 18;
inline constexpr uint32_t MODIFIER_MASK = MODIFIER_CONTROL | MODIFIER_SHIFT | MODIFIER_ALT;
inline constexpr uint32_t KEY_MASK = 0xFFFFu;

enum class Action : uint32_t {
    OpenFile,
    ExportFrames,
    CopyImage,
    PrintImage,
    CloseViewer,
    PreviousFrame,
    ToggleAnimation,
    NextFrame,
    CopyImageInfo,
    ToggleFullscreen,
    RotateLeft,
    RotateRight,
    PanUp,
    PanDown,
    PanLeft,
    PanRight,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    PreviousImage,
    NextImage,
    FirstImage,
    LastImage,
    PlayPause,
    ToggleImageInfo,
    OpenSettings,
    RenameImage,
    OpenShortcuts,
    OpenAbout,
    DeleteImage,
    ZoomActual,
    CloseImage,
    CopyToTarget,
    MoveToTarget,
    Count,
};

enum class WheelAction : uint32_t {
    Zoom,
    PanVertical,
    PanHorizontal,
    SwitchImage,
    Count,
};

constexpr uint32_t binding(uint16_t virtualKey, uint32_t modifiers = 0) {
    return static_cast<uint32_t>(virtualKey) | (modifiers & MODIFIER_MASK);
}

// Numeric virtual-key values keep this policy header independently testable.
inline constexpr std::array<uint32_t, static_cast<std::size_t>(Action::Count)> DEFAULT_BINDINGS{
    binding('O', MODIFIER_CONTROL), // OpenFile
    binding('S', MODIFIER_CONTROL), // ExportFrames
    binding('C', MODIFIER_CONTROL), // CopyImage
    binding('P', MODIFIER_CONTROL), // PrintImage
    binding('W', MODIFIER_CONTROL), // CloseViewer
    binding('J'),              // PreviousFrame
    binding('K'),              // ToggleAnimation
    binding('L'),              // NextFrame
    binding('C'),              // CopyImageInfo
    binding('F'),              // ToggleFullscreen
    binding('Q'),              // RotateLeft
    binding('E'),              // RotateRight
    binding('W'),              // PanUp
    binding('S'),              // PanDown
    binding('A'),              // PanLeft
    binding('D'),              // PanRight
    binding(0x26),             // ZoomIn: VK_UP
    binding(0x28),             // ZoomOut: VK_DOWN
    binding('5'),              // ZoomFit
    binding(0x25),             // PreviousImage: VK_LEFT
    binding(0x27),             // NextImage: VK_RIGHT
    binding(0x24),             // FirstImage: VK_HOME
    binding(0x23),             // LastImage: VK_END
    binding(0x20),             // PlayPause: VK_SPACE
    binding('I'),              // ToggleImageInfo
    binding(0x70),             // OpenSettings: VK_F1
    binding(0x71),             // RenameImage: VK_F2
    binding(0x72),             // OpenShortcuts: VK_F3
    binding(0x73),             // OpenAbout: VK_F4
    binding(0x2E),             // DeleteImage: VK_DELETE
    binding('1'),              // ZoomActual
    binding(0x1B),             // CloseImage: VK_ESCAPE
    // 复制 / 移动到指定位置默认不绑键：这两个动作会往磁盘上写文件，
    // 误触的代价比其他动作大，让用户自己指定。
    0,                         // CopyToTarget
    0,                         // MoveToTarget
};

static_assert(DEFAULT_BINDINGS.size() == static_cast<std::size_t>(Action::Count));
static_assert(VERSION1_BINDING_COUNT <= DEFAULT_BINDINGS.size());
static_assert(VERSION2_BINDING_COUNT <= DEFAULT_BINDINGS.size());
static_assert(VERSION3_BINDING_COUNT <= DEFAULT_BINDINGS.size());

// 某个历史版本的存储里有多少个动作。未知版本按当前版本处理，migrate 会因此什么都不补。
constexpr std::size_t bindingCountForVersion(uint32_t version) {
    switch (version) {
    case 1: return VERSION1_BINDING_COUNT;
    case 2: return VERSION2_BINDING_COUNT;
    case 3: return VERSION3_BINDING_COUNT;
    default: return DEFAULT_BINDINGS.size();
    }
}

inline constexpr std::array<WheelAction, 3> DEFAULT_WHEEL_ACTIONS{
    WheelAction::PanVertical,
    WheelAction::Zoom,
    WheelAction::PanHorizontal,
};

constexpr std::size_t actionIndex(Action action) {
    return static_cast<std::size_t>(action);
}

constexpr bool isModifierKey(uint32_t virtualKey) {
    return virtualKey == 0x10 || virtualKey == 0x11 || virtualKey == 0x12;
}

constexpr bool isStoredBindingValid(uint32_t value) {
    return (value & ~(KEY_MASK | MODIFIER_MASK)) == 0 &&
        ((value & KEY_MASK) == 0 || !isModifierKey(value & KEY_MASK));
}

inline void reset(uint32_t* storage, std::size_t count) {
    if (!storage || count < BINDING_BASE_INDEX + DEFAULT_BINDINGS.size())
        return;
    storage[MAGIC_INDEX] = STORAGE_MAGIC;
    storage[VERSION_INDEX] = STORAGE_VERSION;
    for (std::size_t index = 0; index < DEFAULT_WHEEL_ACTIONS.size(); ++index)
        storage[WHEEL_BASE_INDEX + index] = static_cast<uint32_t>(DEFAULT_WHEEL_ACTIONS[index]);
    for (std::size_t index = 0; index < DEFAULT_BINDINGS.size(); ++index)
        storage[BINDING_BASE_INDEX + index] = DEFAULT_BINDINGS[index];
}

// 追加动作后把旧配置升级到当前版本：只给新动作填默认键位，已有的绑定一概不动。
// 直接 reset 会清空用户全部自定义快捷键，所以升级必须是增量的。
inline void migrate(uint32_t* storage, std::size_t storedBindingCount) {
    for (std::size_t index = storedBindingCount; index < DEFAULT_BINDINGS.size(); ++index) {
        const uint32_t preferred = DEFAULT_BINDINGS[index];
        bool alreadyTaken = false;
        for (std::size_t probe = 0; probe < storedBindingCount; ++probe) {
            if (storage[BINDING_BASE_INDEX + probe] == preferred) {
                alreadyTaken = true;
                break;
            }
        }
        // 默认键位已被用户改派给别的动作时留空，升级不该悄悄夺走既有快捷键。
        storage[BINDING_BASE_INDEX + index] = alreadyTaken ? 0 : preferred;
    }
}

// 返回这次读到的旧配置版本；0 表示没有可用的旧配置、已经整体写回默认值。
// 调用方据此把老版本里存在别处的开关（如「Esc 关闭图片」）迁移成快捷键绑定。
inline uint32_t initialize(uint32_t* storage, std::size_t count) {
    if (!storage || count < BINDING_BASE_INDEX + DEFAULT_BINDINGS.size())
        return 0;
    const uint32_t storedVersion = storage[VERSION_INDEX];
    if (storage[MAGIC_INDEX] != STORAGE_MAGIC ||
        storedVersion == 0 || storedVersion > STORAGE_VERSION) {
        reset(storage, count);
        return 0;
    }
    if (storedVersion < STORAGE_VERSION) {
        migrate(storage, bindingCountForVersion(storedVersion));
        storage[VERSION_INDEX] = STORAGE_VERSION;
    }
    for (std::size_t index = 0; index < DEFAULT_WHEEL_ACTIONS.size(); ++index) {
        if (storage[WHEEL_BASE_INDEX + index] >= static_cast<uint32_t>(WheelAction::Count))
            storage[WHEEL_BASE_INDEX + index] = static_cast<uint32_t>(DEFAULT_WHEEL_ACTIONS[index]);
    }
    for (std::size_t index = 0; index < DEFAULT_BINDINGS.size(); ++index) {
        if (!isStoredBindingValid(storage[BINDING_BASE_INDEX + index]))
            storage[BINDING_BASE_INDEX + index] = DEFAULT_BINDINGS[index];
    }
    return storedVersion;
}

inline uint32_t getBinding(const uint32_t* storage, Action action) {
    return storage[BINDING_BASE_INDEX + actionIndex(action)];
}

inline void setBinding(uint32_t* storage, Action action, uint32_t value) {
    if (!isStoredBindingValid(value))
        return;
    // One shortcut must dispatch to one action. Reassigning it removes the old use.
    if (value != 0) {
        for (std::size_t index = 0; index < DEFAULT_BINDINGS.size(); ++index) {
            if (storage[BINDING_BASE_INDEX + index] == value)
                storage[BINDING_BASE_INDEX + index] = 0;
        }
    }
    storage[BINDING_BASE_INDEX + actionIndex(action)] = value;
}

inline WheelAction getWheelAction(const uint32_t* storage, std::size_t modifierIndex) {
    if (modifierIndex >= DEFAULT_WHEEL_ACTIONS.size())
        return WheelAction::PanVertical;
    const uint32_t value = storage[WHEEL_BASE_INDEX + modifierIndex];
    return value < static_cast<uint32_t>(WheelAction::Count) ?
        static_cast<WheelAction>(value) : DEFAULT_WHEEL_ACTIONS[modifierIndex];
}

inline void setWheelAction(uint32_t* storage, std::size_t modifierIndex, WheelAction action) {
    if (modifierIndex < DEFAULT_WHEEL_ACTIONS.size() && action < WheelAction::Count)
        storage[WHEEL_BASE_INDEX + modifierIndex] = static_cast<uint32_t>(action);
}

constexpr bool matches(uint32_t storedBinding, uint32_t virtualKey, uint32_t modifiers) {
    return storedBinding != 0 &&
        (storedBinding & KEY_MASK) == virtualKey &&
        (storedBinding & MODIFIER_MASK) == (modifiers & MODIFIER_MASK);
}

// language 取 UiLanguage 里的三个值。这里不能只传 bool：繁體也是中文，
// 二选一会让繁體界面显示简体键名。
inline std::string keyName(uint32_t value, uint32_t language) {
    if (value == 0)
        return UiLanguage::pick(language, "未设置", "Unassigned", "未設定");
    std::string result;
    if ((value & MODIFIER_CONTROL) != 0) result += "Ctrl+";
    if ((value & MODIFIER_SHIFT) != 0) result += "Shift+";
    if ((value & MODIFIER_ALT) != 0) result += "Alt+";
    const uint32_t key = value & KEY_MASK;
    if (key >= 'A' && key <= 'Z')
        result.push_back(static_cast<char>(key));
    else if (key >= '0' && key <= '9')
        result.push_back(static_cast<char>(key));
    else if (key >= 0x70 && key <= 0x87)
        result += "F" + std::to_string(key - 0x6F);
    else {
        switch (key) {
        case 0x08: result += "Backspace"; break;
        case 0x09: result += "Tab"; break;
        case 0x0D: result += "Enter"; break;
        case 0x1B: result += "Esc"; break;
        case 0x20: result += UiLanguage::pick(language, "空格", "Space", "空白鍵"); break;
        case 0x21: result += "PageUp"; break;
        case 0x22: result += "PageDown"; break;
        case 0x23: result += "End"; break;
        case 0x24: result += "Home"; break;
        case 0x25: result += UiLanguage::pick(language, "左方向键", "Left", "左方向鍵"); break;
        case 0x26: result += UiLanguage::pick(language, "上方向键", "Up", "上方向鍵"); break;
        case 0x27: result += UiLanguage::pick(language, "右方向键", "Right", "右方向鍵"); break;
        case 0x28: result += UiLanguage::pick(language, "下方向键", "Down", "下方向鍵"); break;
        case 0x2D: result += "Insert"; break;
        case 0x2E: result += "Delete"; break;
        default: result += "VK" + std::to_string(key); break;
        }
    }
    return result;
}

} // namespace ShortcutConfig
