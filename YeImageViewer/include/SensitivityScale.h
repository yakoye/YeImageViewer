#pragma once
#include <algorithm>
#include <cstdint>

namespace SensitivityScale {
inline constexpr uint32_t LEVELS = 20, DEFAULT = 4;
constexpr uint32_t fromLegacy(uint32_t level) { return (std::min)(level, 8u); }
constexpr double fraction(uint32_t level) { return (std::min)(level, LEVELS - 1) / 19.0; }
}
