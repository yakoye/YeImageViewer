#pragma once
#include "SensitivityScale.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace KineticOptions {
inline constexpr std::size_t BASE = 360, SLOTS = 8;
inline constexpr std::size_t ENABLED = BASE + 2, SENSITIVITY = BASE + 3;
inline constexpr uint32_t MAGIC = 0x4B504E31;
inline void initialize(uint32_t* s, std::size_t count) {
    if (!s || count < BASE + SLOTS) return;
    if (s[BASE] == MAGIC && s[BASE + 1] == 1) {
        s[SENSITIVITY] = s[SENSITIVITY] <= 8 ? SensitivityScale::fromLegacy(s[SENSITIVITY]) : SensitivityScale::DEFAULT;
        s[BASE + 1] = 2;
    }
    if (s[BASE] != MAGIC || s[BASE + 1] != 2) {
        std::fill_n(s + BASE, SLOTS, 0u);
        s[BASE] = MAGIC; s[BASE + 1] = 2; s[SENSITIVITY] = SensitivityScale::DEFAULT;
    }
    s[ENABLED] = s[ENABLED] ? 1u : 0u;
    if (s[SENSITIVITY] >= SensitivityScale::LEVELS) s[SENSITIVITY] = SensitivityScale::DEFAULT;
}
}

namespace KineticPan {
struct Vector { double x = 0, y = 0; };

// 固定容量的最近输入，热路径不分配；图片位移必须传入实际限位后的值。
class Samples {
    struct Sample { double end = 0, seconds = 0; Vector mouse, image; };
    std::array<Sample, 64> samples{};
    std::size_t head = 0, count = 0;
    bool precisionUsed = false;
public:
    void reset() { head = count = 0; precisionUsed = false; }
    void add(double end, double seconds, Vector mouse, Vector image, bool precision) {
        precisionUsed |= precision;
        if (precisionUsed || !std::isfinite(end) || !std::isfinite(seconds) ||
            !std::isfinite(mouse.x) || !std::isfinite(mouse.y) ||
            !std::isfinite(image.x) || !std::isfinite(image.y) || seconds <= 0 || seconds > 1) {
            head = count = 0; return;
        }
        if (count) {
            const auto& previous = samples[(head + 63) % 64];
            if (mouse.x * previous.mouse.x + mouse.y * previous.mouse.y < 0) count = 0;
        }
        samples[head] = {end, seconds, mouse, image};
        head = (head + 1) % samples.size(); count = (std::min)(count + 1, samples.size());
    }
    Vector release(double now, int dpi) const {
        if (!count || precisionUsed || !std::isfinite(now)) return {};
        const auto& newest = samples[(head + 63) % 64];
        if (now < newest.end || now - newest.end > .08) return {};
        // 最后已明显减速时，不让更早的快速样本把松手变成意外甩动。
        if (std::hypot(newest.mouse.x, newest.mouse.y) * 96 / (std::max)(1, dpi) / newest.seconds <= 90)
            return {};
        Vector mouse{}, image{};
        double start = now;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& s = samples[(head + 63 - i) % 64];
            const double begin = (std::max)(s.end - s.seconds, now - .12);
            if (s.end <= begin) continue;
            // 渲染较慢／消息合并时，按时间比例取窗口内的部分，不把真实间隔截短成快拖。
            const double portion = (s.end - begin) / s.seconds;
            mouse.x += s.mouse.x * portion; mouse.y += s.mouse.y * portion;
            image.x += s.image.x * portion; image.y += s.image.y * portion;
            start = (std::min)(start, begin);
        }
        const double seconds = now - start;
        // 用鼠标的逻辑速度判断甩动，固定 9× 的慢拖不会误触发惯性。
        if (seconds < .016 || std::hypot(mouse.x, mouse.y) * 96 / (std::max)(1, dpi) / seconds < 240)
            return {};
        return {image.x / seconds, image.y / seconds};
    }
};

// 有限时长的减速：v(t)=v0*(1-t/T)^2；积分得到位置。与帧率无关，无二次倍率。
class Motion {
    Vector initialVelocity{};
    double durationSeconds = 0;
    bool running = false;
public:
    void stop() { running = false; }
    bool active() const { return running; }
    double duration() const { return durationSeconds; }
    Vector velocity() const { return initialVelocity; }
    bool start(Vector velocity, uint32_t sensitivity, double width, double height) {
        stop();
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
            !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            return false;
        initialVelocity = {std::clamp(velocity.x, -6 * width, 6 * width),
            std::clamp(velocity.y, -6 * height, 6 * height)};
        durationSeconds = .18 + .075 * (std::min)(sensitivity, 19u);
        running = std::hypot(initialVelocity.x, initialVelocity.y) >= 1;
        return running;
    }
    Vector at(double seconds) {
        if (!running) return {};
        if (!std::isfinite(seconds)) { stop(); return {}; }
        const double t = std::clamp(seconds / durationSeconds, 0.0, 1.0);
        const double distance = durationSeconds / 3 * (1 - (1 - t) * (1 - t) * (1 - t));
        if (t >= 1) running = false;
        return {initialVelocity.x * distance, initialVelocity.y * distance};
    }
};
}
