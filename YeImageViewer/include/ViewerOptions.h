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
inline constexpr uint32_t STORAGE_VERSION = 3;

inline constexpr std::size_t MAGIC_INDEX = STORAGE_BASE + 0;
inline constexpr std::size_t VERSION_INDEX = STORAGE_BASE + 1;
inline constexpr std::size_t EDGE_ARROWS_INDEX = STORAGE_BASE + 2;
inline constexpr std::size_t DOUBLE_CLICK_INDEX = STORAGE_BASE + 3;
inline constexpr std::size_t OPEN_MODE_INDEX = STORAGE_BASE + 4;
inline constexpr std::size_t DRAG_MOVES_WINDOW_INDEX = STORAGE_BASE + 5;
// 版本 2 新增
inline constexpr std::size_t INFO_PANEL_OPACITY_INDEX = STORAGE_BASE + 6;
inline constexpr std::size_t INFO_HISTOGRAM_INDEX = STORAGE_BASE + 7;
// 版本 3 新增
inline constexpr std::size_t LIVE_PHOTO_SOUND_INDEX = STORAGE_BASE + 8;

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

// 图片信息面板的背景不透明度。存的是档位而不是 alpha 数值，
// 这样将来调整具体数值不会让旧设置文件失效。
enum class InfoPanelOpacity : uint32_t {
    Light = 0,   // 最透，看得清底下的图
    Medium = 1,
    Strong = 2,  // 原有观感
    Opaque = 3,  // 全不透明，纯白/纯黑底图上文字最稳
    Count,
};

// 各档位对应的 alpha。Strong 保持 0xD1，跟没有这个选项之前一致，
// 老用户升级后观感不变。
constexpr uint32_t infoPanelAlpha(InfoPanelOpacity level) {
    switch (level) {
    case InfoPanelOpacity::Light:  return 0x8Cu;   // 55%
    case InfoPanelOpacity::Medium: return 0xB3u;   // 70%
    case InfoPanelOpacity::Opaque: return 0xFFu;
    default:                       return 0xD1u;   // 82%，原值
    }
}

// 把某个档位的 alpha 套到面板底色上。颜色低 24 位不动，只换 alpha。
constexpr uint32_t withPanelAlpha(uint32_t bgra, InfoPanelOpacity level) {
    return (infoPanelAlpha(level) << 24) | (bgra & 0x00FFFFFFu);
}

// 默认值集中在这里，reset 与迁移都从它取，避免两处各写一份。
inline constexpr bool DEFAULT_EDGE_ARROWS = false;
inline constexpr auto DEFAULT_DOUBLE_CLICK = DoubleClickAction::ToggleFullscreen;
inline constexpr auto DEFAULT_OPEN_MODE = OpenMode::ImmersivePreview;
inline constexpr bool DEFAULT_DRAG_MOVES_WINDOW = true;
// 默认沿用加这个选项之前的观感，升级后不变
inline constexpr auto DEFAULT_INFO_PANEL_OPACITY = InfoPanelOpacity::Strong;
inline constexpr bool DEFAULT_INFO_HISTOGRAM = true;
// 打开实况照片时自动播放的那一遍默认静音，与 macOS「照片」一致；
// 主动播放（悬停「实况」标记、空格重播）不受这个设置影响，总是出声。
inline constexpr bool DEFAULT_LIVE_PHOTO_SOUND = false;

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
    storage[INFO_PANEL_OPACITY_INDEX] = static_cast<uint32_t>(DEFAULT_INFO_PANEL_OPACITY);
    storage[INFO_HISTOGRAM_INDEX] = DEFAULT_INFO_HISTOGRAM ? 1u : 0u;
    storage[LIVE_PHOTO_SOUND_INDEX] = DEFAULT_LIVE_PHOTO_SOUND ? 1u : 0u;
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

    // 版本升级必须是增量的：只给新字段补默认值，用户改过的既有配置一律保留。
    // 整块 reset 会把用户设过的打开方式、双击动作全冲掉。
    if (version < 2) {
        storage[INFO_PANEL_OPACITY_INDEX] = static_cast<uint32_t>(DEFAULT_INFO_PANEL_OPACITY);
        storage[INFO_HISTOGRAM_INDEX] = DEFAULT_INFO_HISTOGRAM ? 1u : 0u;
    }
    if (version < 3)
        storage[LIVE_PHOTO_SOUND_INDEX] = DEFAULT_LIVE_PHOTO_SOUND ? 1u : 0u;
    storage[VERSION_INDEX] = STORAGE_VERSION;
    if (storage[EDGE_ARROWS_INDEX] > 1u)
        storage[EDGE_ARROWS_INDEX] = DEFAULT_EDGE_ARROWS ? 1u : 0u;
    if (storage[DRAG_MOVES_WINDOW_INDEX] > 1u)
        storage[DRAG_MOVES_WINDOW_INDEX] = DEFAULT_DRAG_MOVES_WINDOW ? 1u : 0u;
    if (storage[DOUBLE_CLICK_INDEX] >= static_cast<uint32_t>(DoubleClickAction::Count))
        storage[DOUBLE_CLICK_INDEX] = static_cast<uint32_t>(DEFAULT_DOUBLE_CLICK);
    if (storage[OPEN_MODE_INDEX] >= static_cast<uint32_t>(OpenMode::Count))
        storage[OPEN_MODE_INDEX] = static_cast<uint32_t>(DEFAULT_OPEN_MODE);
    if (storage[INFO_PANEL_OPACITY_INDEX] >= static_cast<uint32_t>(InfoPanelOpacity::Count))
        storage[INFO_PANEL_OPACITY_INDEX] = static_cast<uint32_t>(DEFAULT_INFO_PANEL_OPACITY);
    if (storage[INFO_HISTOGRAM_INDEX] > 1u)
        storage[INFO_HISTOGRAM_INDEX] = DEFAULT_INFO_HISTOGRAM ? 1u : 0u;
    if (storage[LIVE_PHOTO_SOUND_INDEX] > 1u)
        storage[LIVE_PHOTO_SOUND_INDEX] = DEFAULT_LIVE_PHOTO_SOUND ? 1u : 0u;
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

inline InfoPanelOpacity infoPanelOpacity(const uint32_t* storage) {
    if (!storage)
        return DEFAULT_INFO_PANEL_OPACITY;
    const uint32_t value = storage[INFO_PANEL_OPACITY_INDEX];
    return value < static_cast<uint32_t>(InfoPanelOpacity::Count) ?
        static_cast<InfoPanelOpacity>(value) : DEFAULT_INFO_PANEL_OPACITY;
}

inline void setInfoPanelOpacity(uint32_t* storage, InfoPanelOpacity level) {
    if (storage && level < InfoPanelOpacity::Count)
        storage[INFO_PANEL_OPACITY_INDEX] = static_cast<uint32_t>(level);
}

inline bool infoHistogramEnabled(const uint32_t* storage) {
    return storage && storage[INFO_HISTOGRAM_INDEX] != 0u;
}

inline void setInfoHistogramEnabled(uint32_t* storage, bool enabled) {
    if (storage)
        storage[INFO_HISTOGRAM_INDEX] = enabled ? 1u : 0u;
}

// 打开实况照片时自动播放的那一遍是否出声
inline bool livePhotoAutoSound(const uint32_t* storage) {
    return storage && storage[LIVE_PHOTO_SOUND_INDEX] != 0u;
}

inline void setLivePhotoAutoSound(uint32_t* storage, bool enabled) {
    if (storage)
        storage[LIVE_PHOTO_SOUND_INDEX] = enabled ? 1u : 0u;
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
