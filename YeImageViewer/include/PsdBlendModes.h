#pragma once
#include <array>
#include <string_view>

// 菜单顺序与4字节键移植自PhotoCraft crates/color/src/blend.rs。
// Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors. MIT许可见THIRD_PARTY_NOTICES.md。
namespace PsdBlendModes {
struct Entry {std::string_view key;const char* chinese;const char* english;const char* traditional;};
inline constexpr std::array<Entry,28> entries{{
    {"pass","穿透","Pass Through","穿透"},{"norm","正常","Normal","正常"},{"diss","溶解","Dissolve","溶解"},
    {"dark","变暗","Darken","變暗"},{"mul ","正片叠底","Multiply","色彩增值"},{"idiv","颜色加深","Color Burn","加深顏色"},
    {"lbrn","线性加深","Linear Burn","線性加深"},{"dkCl","深色","Darker Color","較深的顏色"},{"lite","变亮","Lighten","變亮"},
    {"scrn","滤色","Screen","濾色"},{"div ","颜色减淡","Color Dodge","加亮顏色"},{"lddg","线性减淡（添加）","Linear Dodge (Add)","線性加亮（增加）"},
    {"lgCl","浅色","Lighter Color","較淺的顏色"},{"over","叠加","Overlay","覆蓋"},{"sLit","柔光","Soft Light","柔光"},
    {"hLit","强光","Hard Light","實光"},{"vLit","亮光","Vivid Light","強烈光源"},{"lLit","线性光","Linear Light","線性光源"},
    {"pLit","点光","Pin Light","小光源"},{"hMix","实色混合","Hard Mix","實色疊印"},{"diff","差值","Difference","差異化"},
    {"smud","排除","Exclusion","排除"},{"fsub","减去","Subtract","減去"},{"fdiv","划分","Divide","分割"},
    {"hue ","色相","Hue","色相"},{"sat ","饱和度","Saturation","飽和度"},{"colr","颜色","Color","顏色"},{"lum ","明度","Luminosity","明度"}
}};
inline const Entry* find(std::string_view key){for(const auto& e:entries)if(e.key==key)return &e;return nullptr;}
}
