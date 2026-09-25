#pragma once

#include "jarkUtils.h"

class ColorManager {
public:
    void setWindow(HWND hwnd);
    void applyToImageAsset(ImageAsset& imageAsset);

    static std::vector<uint8_t> readEmbeddedIccProfile(std::wstring_view path, std::span<const uint8_t> buf);

    // 公开是为了让 --color-selftest 能直接比对并行与串行的结果。
    // 返回 false 有两种含义：这张图不需要变换（恒等，已跳过），或者变换建不起来。
    static bool applyToMat(cv::Mat& mat, const std::vector<uint8_t>& sourceIcc, const std::vector<uint8_t>& monitorIcc);

private:
    HWND hwnd = nullptr;

    std::vector<uint8_t> readMonitorIccProfile() const;
    std::vector<uint8_t>& readMonitorIccProfileCached();
};
