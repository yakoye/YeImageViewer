#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ShortcutConfig {

inline constexpr uint32_t STORAGE_MAGIC = 0x594B4559u; // "YKEY"
inline constexpr uint32_t STORAGE_VERSION = 1;
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
};

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

inline void initialize(uint32_t* storage, std::size_t count) {
    if (!storage || count < BINDING_BASE_INDEX + DEFAULT_BINDINGS.size())
        return;
    if (storage[MAGIC_INDEX] != STORAGE_MAGIC || storage[VERSION_INDEX] != STORAGE_VERSION) {
        reset(storage, count);
        return;
    }
    for (std::size_t index = 0; index < DEFAULT_WHEEL_ACTIONS.size(); ++index) {
        if (storage[WHEEL_BASE_INDEX + index] >= static_cast<uint32_t>(WheelAction::Count))
            storage[WHEEL_BASE_INDEX + index] = static_cast<uint32_t>(DEFAULT_WHEEL_ACTIONS[index]);
    }
    for (std::size_t index = 0; index < DEFAULT_BINDINGS.size(); ++index) {
        if (!isStoredBindingValid(storage[BINDING_BASE_INDEX + index]))
            storage[BINDING_BASE_INDEX + index] = DEFAULT_BINDINGS[index];
    }
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

inline std::string keyName(uint32_t value, bool chinese) {
    if (value == 0)
        return chinese ? "未设置" : "Unassigned";
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
        case 0x20: result += chinese ? "空格" : "Space"; break;
        case 0x21: result += "PageUp"; break;
        case 0x22: result += "PageDown"; break;
        case 0x23: result += "End"; break;
        case 0x24: result += "Home"; break;
        case 0x25: result += chinese ? "左方向键" : "Left"; break;
        case 0x26: result += chinese ? "上方向键" : "Up"; break;
        case 0x27: result += chinese ? "右方向键" : "Right"; break;
        case 0x28: result += chinese ? "下方向键" : "Down"; break;
        case 0x2D: result += "Insert"; break;
        case 0x2E: result += "Delete"; break;
        default: result += "VK" + std::to_string(key); break;
        }
    }
    return result;
}

} // namespace ShortcutConfig
