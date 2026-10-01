#pragma once

#include <algorithm>
#include <cmath>

// 打印路径上的尺寸计算。
//
// 单独拎出来是因为这里曾经出过一个只在极端长宽比下才犯的错：按比例缩放时
// 只算缩放系数、不管算出来的边长，一张 10000x1 的图缩到 800 宽，高就成了
// round(1 * 0.08) = 0，于是「打印」点下去什么都不发生——预览窗口压根建不起来，
// 也没有任何提示。页面排版那边同样的算法会得出一个 0 高的 ROI。
//
// 所以这里的每个函数都保证：输出的边长至少 1 像素，而且绝不超出容器；
// 输入不合法时明确说「不行」，而不是返回一个会让下游崩掉的尺寸。
namespace PrintLayout {

struct FittedSize {
    int width = 0;
    int height = 0;
    bool valid = false;
};

// 预览：按最长边缩进 boxSize x boxSize 的方形画布。
// 极端长宽比下短边会被抬到 1 像素——这时比例已经没法保住了，但「看得见」
// 比「按比例但看不见」有用。
inline FittedSize previewSize(int imageWidth, int imageHeight, int boxSize) {
    if (imageWidth <= 0 || imageHeight <= 0 || boxSize <= 0)
        return {};

    const int longest = std::max(imageWidth, imageHeight);
    const double scale = static_cast<double>(boxSize) / longest;

    FittedSize fitted;
    fitted.width = std::clamp(static_cast<int>(std::lround(imageWidth * scale)), 1, boxSize);
    fitted.height = std::clamp(static_cast<int>(std::lround(imageHeight * scale)), 1, boxSize);
    fitted.valid = true;
    return fitted;
}

struct Placement {
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    bool valid = false;
};

// 打印页排版：按 fillRatio（0.9 即四周留 5% 白边）等比缩放后居中。
// 返回的矩形保证完整落在页面里，可以直接拿去建 ROI。
inline Placement pagePlacement(int imageWidth, int imageHeight,
    int pageWidth, int pageHeight, double fillRatio) {
    if (imageWidth <= 0 || imageHeight <= 0 || pageWidth <= 0 || pageHeight <= 0)
        return {};
    if (!(fillRatio > 0.0))
        return {};

    const double scale = fillRatio * std::min(
        static_cast<double>(pageWidth) / imageWidth,
        static_cast<double>(pageHeight) / imageHeight);

    Placement placement;
    placement.width = std::clamp(static_cast<int>(std::lround(imageWidth * scale)), 1, pageWidth);
    placement.height = std::clamp(static_cast<int>(std::lround(imageHeight * scale)), 1, pageHeight);
    // +1 让差值为奇数时也居中偏左/上一个像素，而不是漏出一侧
    placement.offsetX = (pageWidth - placement.width + 1) / 2;
    placement.offsetY = (pageHeight - placement.height + 1) / 2;
    placement.valid = true;
    return placement;
}

// 把超大图压到 limit 以内。没超就原样返回，省掉一次没必要的重采样。
inline FittedSize cappedSize(int imageWidth, int imageHeight, int limit) {
    if (imageWidth <= 0 || imageHeight <= 0 || limit <= 0)
        return {};

    if (imageWidth <= limit && imageHeight <= limit)
        return { imageWidth, imageHeight, true };

    const double scale = std::min(
        static_cast<double>(limit) / imageWidth,
        static_cast<double>(limit) / imageHeight);

    FittedSize fitted;
    fitted.width = std::clamp(static_cast<int>(std::lround(imageWidth * scale)), 1, limit);
    fitted.height = std::clamp(static_cast<int>(std::lround(imageHeight * scale)), 1, limit);
    fitted.valid = true;
    return fitted;
}

} // namespace PrintLayout
