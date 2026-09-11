#pragma once

#include <cstddef>
#include <cstdint>

// 查看器交互选项。和快捷键一样寄存在 SettingParameter::reserve 里——设置文件固定
// 4096 字节且成员顺序不能动，新增配置只能往 reserve 里放。这里从 STORAGE_BASE 开始
// 另开一块，避开 ShortcutConfig 占用的低位区（0 ~ BINDING_BASE_INDEX + 动作数），
// 两者各自独立初始化，也都能脱离 Win32 单独测试。
namespace ViewerOptions {

inline constexpr std::size_t STORAGE_BASE = 256;
inline constexpr std::size_t SLOT_COUNT = 16;
inline constexpr uint32_t STORAGE_MAGIC = 0x564F5054u; // "VOPT"
inline constexpr uint32_t STORAGE_VERSION = 1;

inline constexpr std::size_t MAGIC_INDEX = STORAGE_BASE + 0;
inline constexpr std::size_t VERSION_INDEX = STORAGE_BASE + 1;
inline constexpr std::size_t EDGE_ARROWS_INDEX = STORAGE_BASE + 2;
inline constexpr std::size_t DOUBLE_CLICK_INDEX = STORAGE_BASE + 3;
inline constexpr std::size_t OPEN_MODE_INDEX = STORAGE_BASE + 4;
inline constexpr std::size_t DRAG_MOVES_WINDOW_INDEX = STORAGE_BASE + 5;

enum class DoubleClickAction : uint32_t {
    ToggleFullscreen = 0,
    ToggleMaximize = 1,
    None = 2,
    Count,
};

enum class OpenMode : uint32_t {
    ImmersivePreview = 0, // 无边框沉浸预览，覆盖工作区
    FitImage = 1,         // 普通窗口，客户区贴合图片尺寸
    RememberLastSize = 2, // 普通窗口，沿用上次关闭时的窗口大小
    Count,
};

// 默认值集中在这里，reset 与迁移都从它取，避免两处各写一份。
inline constexpr bool DEFAULT_EDGE_ARROWS = false;
inline constexpr auto DEFAULT_DOUBLE_CLICK = DoubleClickAction::ToggleFullscreen;
inline constexpr auto DEFAULT_OPEN_MODE = OpenMode::ImmersivePreview;
inline constexpr bool DEFAULT_DRAG_MOVES_WINDOW = true;

constexpr bool fits(std::size_t count) {
    return count >= STORAGE_BASE + SLOT_COUNT;
}

inline void reset(uint32_t* storage, std::size_t count) {
    if (!storage || !fits(count))
        return;
    storage[MAGIC_INDEX] = STORAGE_MAGIC;
    storage[VERSION_INDEX] = STORAGE_VERSION;
    storage[EDGE_ARROWS_INDEX] = DEFAULT_EDGE_ARROWS ? 1u : 0u;
    storage[DOUBLE_CLICK_INDEX] = static_cast<uint32_t>(DEFAULT_DOUBLE_CLICK);
    storage[OPEN_MODE_INDEX] = static_cast<uint32_t>(DEFAULT_OPEN_MODE);
    storage[DRAG_MOVES_WINDOW_INDEX] = DEFAULT_DRAG_MOVES_WINDOW ? 1u : 0u;
}

inline void initialize(uint32_t* storage, std::size_t count) {
    if (!storage || !fits(count))
        return;
    const uint32_t version = storage[VERSION_INDEX];
    // 旧设置文件这块区域是全零，magic 对不上就按默认值铺一遍。
    if (storage[MAGIC_INDEX] != STORAGE_MAGIC ||
        version == 0 || version > STORAGE_VERSION) {
        reset(storage, count);
        return;
    }
    if (storage[EDGE_ARROWS_INDEX] > 1u)
        storage[EDGE_ARROWS_INDEX] = DEFAULT_EDGE_ARROWS ? 1u : 0u;
    if (storage[DRAG_MOVES_WINDOW_INDEX] > 1u)
        storage[DRAG_MOVES_WINDOW_INDEX] = DEFAULT_DRAG_MOVES_WINDOW ? 1u : 0u;
    if (storage[DOUBLE_CLICK_INDEX] >= static_cast<uint32_t>(DoubleClickAction::Count))
        storage[DOUBLE_CLICK_INDEX] = static_cast<uint32_t>(DEFAULT_DOUBLE_CLICK);
    if (storage[OPEN_MODE_INDEX] >= static_cast<uint32_t>(OpenMode::Count))
        storage[OPEN_MODE_INDEX] = static_cast<uint32_t>(DEFAULT_OPEN_MODE);
}

inline bool edgeArrowsEnabled(const uint32_t* storage) {
    return storage && storage[EDGE_ARROWS_INDEX] != 0u;
}

inline void setEdgeArrowsEnabled(uint32_t* storage, bool enabled) {
    if (storage)
        storage[EDGE_ARROWS_INDEX] = enabled ? 1u : 0u;
}

inline bool dragMovesWindow(const uint32_t* storage) {
    return storage && storage[DRAG_MOVES_WINDOW_INDEX] != 0u;
}

inline void setDragMovesWindow(uint32_t* storage, bool enabled) {
    if (storage)
        storage[DRAG_MOVES_WINDOW_INDEX] = enabled ? 1u : 0u;
}

inline DoubleClickAction doubleClickAction(const uint32_t* storage) {
    if (!storage)
        return DEFAULT_DOUBLE_CLICK;
    const uint32_t value = storage[DOUBLE_CLICK_INDEX];
    return value < static_cast<uint32_t>(DoubleClickAction::Count) ?
        static_cast<DoubleClickAction>(value) : DEFAULT_DOUBLE_CLICK;
}

inline void setDoubleClickAction(uint32_t* storage, DoubleClickAction action) {
    if (storage && action < DoubleClickAction::Count)
        storage[DOUBLE_CLICK_INDEX] = static_cast<uint32_t>(action);
}

inline OpenMode openMode(const uint32_t* storage) {
    if (!storage)
        return DEFAULT_OPEN_MODE;
    const uint32_t value = storage[OPEN_MODE_INDEX];
    return value < static_cast<uint32_t>(OpenMode::Count) ?
        static_cast<OpenMode>(value) : DEFAULT_OPEN_MODE;
}

inline void setOpenMode(uint32_t* storage, OpenMode mode) {
    if (storage && mode < OpenMode::Count)
        storage[OPEN_MODE_INDEX] = static_cast<uint32_t>(mode);
}

// 打开图片时是否走无边框沉浸预览。另外两种模式都直接给带边框的普通窗口。
constexpr bool opensImmersive(OpenMode mode) {
    return mode == OpenMode::ImmersivePreview;
}

// 图片在窗口里没有可平移的余量时，左键拖拽改成移动窗口而不是空拖。
// 沉浸预览铺满工作区，移动窗口没有意义，那里始终保持平移。
constexpr bool dragShouldMoveWindow(
    bool enabled, bool presentationMode, bool imageHasPanRoom) {
    return enabled && !presentationMode && !imageHasPanRoom;
}

}
