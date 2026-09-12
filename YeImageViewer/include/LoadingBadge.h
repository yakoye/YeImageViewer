#pragma once

#include <cstdint>
#include <format>
#include <string>

// 加载提示的文案合成。
//
// 单独摘出来是因为倒计时的算术是这块唯一会算错的地方，而它背后的绘制
// （圆角底 + 居中文字）复用的是工具栏那套已经验证过的代码。
// 锁屏时截不到屏，这部分必须能脱离屏幕验证。
namespace LoadingBadge {

    // estimatedMs <= 0 表示估不出来（尺寸没查到、或短到不值得显示倒计时）。
    // 估值用完（remain <= 0）时只留文字：继续显示 0.000 等于在撒谎——
    // 解码没有进度回调，估短了就是估短了，不该假装还剩一点点。
    inline std::string compose(const char* label, int64_t estimatedMs, int64_t elapsedMs) {
        std::string text = label ? label : "";
        if (estimatedMs <= 0)
            return text;

        const int64_t remainMs = estimatedMs - elapsedMs;
        if (remainMs <= 0)
            return text;

        // 毫秒三位是为了让数字明显在动——主循环空闲时每帧都会重画
        text += std::format("  {:.3f}s", static_cast<double>(remainMs) / 1000.0);
        return text;
    }

}
