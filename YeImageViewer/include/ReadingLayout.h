#pragma once
#include "ReadingPolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace ReadingLayout {
struct Size { double width = 0, height = 0; };
struct Page { int index = -1; double x = 0, y = 0, width = 0, height = 0; };
struct Group {
    double offset = 0, extent = 0, scale = 1;
    std::array<Page, 2> pages;
};
struct Layout {
    std::vector<Group> groups;
    double total = 0;
    int groupAt(double coordinate) const {
        if (groups.empty()) return -1;
        const auto found = std::upper_bound(groups.begin(), groups.end(), coordinate,
            [](double offset, const Group& group) { return offset < group.offset; });
        return (std::max)(0, static_cast<int>(found - groups.begin()) - 1);
    }
};

// 当前布局保留浮点缩放；不要用已量化到 1/65536 的显示倍率反推比例，
// 否则连续缩放会累积一个刻度的误差，插入与标准档位几乎重合的伪档位。
inline double enlargementForScale(double currentScale, double currentEnlargement, double targetScale) {
    if (!std::isfinite(currentScale) || currentScale <= 0 ||
        !std::isfinite(currentEnlargement) || currentEnlargement <= 0 ||
        !std::isfinite(targetScale) || targetScale <= 0) return 1;
    const double baseScale = currentScale / currentEnlargement;
    return (std::clamp)(targetScale / baseScale, 0.01, 100.0);
}

inline Layout build(std::span<const Size> sizes, int viewportWidth, int viewportHeight,
    bool horizontal, bool doublePage, int shift, bool upscale, bool actual,
    int enlargedGroup = -1, double enlargement = 1, double gap = 16,
    std::span<const double> customScales = {}) {
    Layout result;
    if (sizes.empty() || viewportWidth <= 0 || viewportHeight <= 0) return result;
    const double availableWidth = (std::max)(1.0, double(viewportWidth) - gap * 2);
    const double availableHeight = (std::max)(1.0, double(viewportHeight) - gap * 2);
    const int count = static_cast<int>(sizes.size());
    for (int g = 0; g < ReadingPolicy::groupCount(count, doublePage, shift); ++g) {
        Group group;
        const auto indices = ReadingPolicy::pages(g, count, doublePage, shift);
        Size pageSizes[2]{};
        for (int i = 0; i < 2; ++i) if (indices[i] >= 0) {
            pageSizes[i] = sizes[indices[i]];
            if (pageSizes[i].width <= 0 || pageSizes[i].height <= 0)
                pageSizes[i] = {double(viewportWidth) / (doublePage ? 2 : 1), double(viewportHeight)};
        }
        if (doublePage) {
            if (indices[0] < 0) pageSizes[0] = pageSizes[1];
            if (indices[1] < 0) pageSizes[1] = pageSizes[0];
        }
        const double sourceWidth = pageSizes[0].width + (doublePage ? pageSizes[1].width : 0);
        const double sourceHeight = (std::max)(pageSizes[0].height, pageSizes[1].height);
        const double fit = (std::min)((availableWidth - (doublePage ? gap : 0)) /
            (std::max)(1.0, sourceWidth), availableHeight / (std::max)(1.0, sourceHeight));
        group.scale = actual ? 1.0 : (upscale ? fit : (std::min)(1.0, fit));
        if (g < static_cast<int>(customScales.size()) && customScales[g] > 0)
            group.scale = customScales[g];
        if (g == enlargedGroup) group.scale *= (std::clamp)(enlargement, 0.01, 100.0);
        group.scale = (std::max)(0.00001, group.scale);
        const double groupWidth = sourceWidth * group.scale + (doublePage ? gap : 0);
        const double groupHeight = sourceHeight * group.scale;
        group.offset = result.total;
        // 按图片边缘连续衔接，不给每页额外填满一个视口的空白。
        group.extent = horizontal ? groupWidth + gap * 2 : groupHeight + gap * 2;
        double x = horizontal ? group.offset + (group.extent - groupWidth) / 2 :
            (viewportWidth - groupWidth) / 2;
        const double y = horizontal ? (groupHeight > viewportHeight ? gap : (viewportHeight - groupHeight) / 2) :
            group.offset + (group.extent - groupHeight) / 2;
        for (int i = 0; i < (doublePage ? 2 : 1); ++i) {
            group.pages[i] = {indices[i], x, y, pageSizes[i].width * group.scale,
                pageSizes[i].height * group.scale};
            x += pageSizes[i].width * group.scale + gap;
        }
        result.groups.push_back(group);
        result.total += group.extent + gap;
    }
    result.total -= gap;
    return result;
}
inline double clampOffset(double offset, double total, int viewportExtent) {
    return (std::clamp)(offset, 0.0, (std::max)(0.0, total - viewportExtent));
}
}
