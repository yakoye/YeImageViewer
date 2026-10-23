#pragma once
#include <cstdint>
namespace PsdDockStyle {
// 对齐 Photocraft theme.rs 的 Pro / ProLight 中性灰色阶（MIT，归属见第三方声明）。
constexpr uint32_t background(bool dark){return dark?0xFF323232u:0xFFF0F0F0u;}
constexpr uint32_t foreground(bool dark){return dark?0xFFDEDEDEu:0xFF141414u;}
constexpr uint32_t muted(bool dark){return dark?0xFFB2B2B2u:0xFF404040u;}
constexpr uint32_t accent(bool dark){return dark?0xFF6AB9F2u:0xFF0067B1u;}
constexpr uint32_t separator(bool dark){return dark?0xFF1E1E1Eu:0xFFD6D6D6u;}
constexpr uint32_t field(bool dark){return dark?0xFF454545u:0xFFFFFFFFu;}
constexpr uint32_t fieldBorder(bool dark){return dark?0xFF202020u:0xFFBABABAu;}
constexpr uint32_t selected(bool dark){return dark?0xFF525252u:0xFFCCCCCCu;}
constexpr uint32_t hovered(bool dark){return dark?0xFF444A52u:0xFFDCE7F0u;}
constexpr uint32_t tabStrip(bool dark){return dark?0xFF262626u:0xFFE2E2E2u;}
}
