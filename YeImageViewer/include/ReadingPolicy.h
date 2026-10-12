#pragma once
#include <algorithm>
#include <array>

namespace ReadingPolicy {
// shift=1：第 1 页在右边，左侧补空白；shift=0：第 1 页在左边。
constexpr int groupForPage(int page, bool doublePage, int shift) {
    return doublePage ? (std::max(0, page) + (shift & 1)) / 2 : std::max(0, page);
}
constexpr int groupCount(int count, bool doublePage, int shift) {
    return count <= 0 ? 0 : (doublePage ? (count + (shift & 1) + 1) / 2 : count);
}
constexpr std::array<int, 2> pages(int group, int count, bool doublePage, int shift) {
    const int first = doublePage ? group * 2 - (shift & 1) : group;
    return {first >= 0 && first < count ? first : -1,
        doublePage && first + 1 >= 0 && first + 1 < count ? first + 1 : -1};
}
constexpr int shiftForPage(int page, bool right) {
    return (page & 1) ^ (right ? 1 : 0);
}
}
