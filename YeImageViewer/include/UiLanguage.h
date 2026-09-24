#pragma once

#include <cstdint>

// 界面语言：0 简体中文 / 1 English / 2 繁體中文。
//
// 单独成一个头文件，是因为 ShortcutConfig、ImageInfoPresentation 这些纯逻辑头
// 不依赖 GlobalVar（单元测试直接包含它们跑），它们也要能三选一。
// 加繁體之前这些地方一律写成 `chinese ? 简体 : English`，二选一的写法接不住第三种，
// 而改成嵌套三目会让每一行都变得难读，所以统一收敛到 pick()。
namespace UiLanguage {

inline constexpr uint32_t SIMPLIFIED = 0;
inline constexpr uint32_t ENGLISH = 1;
inline constexpr uint32_t TRADITIONAL = 2;
inline constexpr uint32_t COUNT = 3;

// 繁體也是中文。凡是「中文一套、英文一套」的排版差异都按这个判断，
// 写成 language == SIMPLIFIED 会让繁體界面掉进英文分支。
inline constexpr bool isChinese(uint32_t language) {
    return language != ENGLISH;
}

inline constexpr uint32_t clamp(uint32_t language) {
    return language < COUNT ? language : SIMPLIFIED;
}

template <typename T>
constexpr T pick(uint32_t language, T simplified, T english, T traditional) {
    if (language == ENGLISH)
        return english;
    if (language == TRADITIONAL)
        return traditional;
    return simplified;
}

}  // namespace UiLanguage
