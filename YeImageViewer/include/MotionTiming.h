#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

// 实况照片（动态照片）的时间轴。画面按每帧的呈现时间戳播放，声音按采样率播放，
// 两者从同一时刻起、沿同一根时间轴走，才能对上嘴型。纯函数，可脱离 FFmpeg 单独测试。
namespace MotionTiming {

inline constexpr int64_t NO_TIMESTAMP = std::numeric_limits<int64_t>::min();
inline constexpr int FALLBACK_FRAME_MS = 33;   // 取不到可信时间戳时按 30 fps
inline constexpr int MIN_FRAME_MS = 1;
inline constexpr int MAX_FRAME_MS = 1000;
// 声音与画面的起点差超过这个值就不可信了（实况只有几秒），当作无声处理
inline constexpr int64_t MAX_AUDIO_LEAD_MS = 10'000;

// 由帧率（num/den 帧每秒）换算单帧时长；帧率无效时用 FALLBACK_FRAME_MS。
constexpr int frameMsFromRate(int num, int den) {
    if (num <= 0 || den <= 0)
        return FALLBACK_FRAME_MS;
    const int64_t ms = (static_cast<int64_t>(den) * 1000 + num / 2) / num;
    return static_cast<int>(std::clamp<int64_t>(ms, MIN_FRAME_MS, MAX_FRAME_MS));
}

// 按每帧的呈现时间戳（毫秒）算每帧该显示多久。
// 相邻差值不可信（缺失、不递增、超过 1 秒）的帧用 fallbackMs；最后一帧没有下一帧
// 可减，取前面可信时长的中位数，一个可信值都没有才用 fallbackMs。
// 此前一律按 33 ms 播放，60 fps 的动态照片会慢放一倍。
inline std::vector<int> durationsFromTimestamps(const std::vector<int64_t>& ptsMs, int fallbackMs) {
    const int fallback = std::clamp(fallbackMs, MIN_FRAME_MS, MAX_FRAME_MS);
    std::vector<int> durations(ptsMs.size(), fallback);
    std::vector<int> trusted;
    for (std::size_t i = 0; i + 1 < ptsMs.size(); ++i) {
        if (ptsMs[i] == NO_TIMESTAMP || ptsMs[i + 1] == NO_TIMESTAMP)
            continue;
        const int64_t delta = ptsMs[i + 1] - ptsMs[i];
        if (delta < MIN_FRAME_MS || delta > MAX_FRAME_MS)
            continue;
        durations[i] = static_cast<int>(delta);
        trusted.push_back(static_cast<int>(delta));
    }
    if (!durations.empty() && !trusted.empty()) {
        std::nth_element(trusted.begin(), trusted.begin() + trusted.size() / 2, trusted.end());
        durations.back() = trusted[trusted.size() / 2];
    }
    return durations;
}

inline int64_t totalMs(const std::vector<int>& durations) {
    int64_t total = 0;
    for (const int duration : durations)
        total += std::max(duration, 0);
    return total;
}

// 第 index 帧在时间轴上从哪一毫秒开始显示。
inline int64_t frameStartMs(const std::vector<int>& durations, int index) {
    int64_t start = 0;
    for (int i = 0; i < index && i < static_cast<int>(durations.size()); ++i)
        start += std::max(durations[i], 0);
    return start;
}

// 时间轴走到 elapsedMs 时该显示哪一帧；已经走完返回 -1。
// 按时间轴取帧而不是逐帧累加延时：累加会把每一帧的超时都攒下来，
// 几秒下来画面就落后声音一截。
inline int frameIndexAt(const std::vector<int>& durations, int64_t elapsedMs) {
    if (durations.empty())
        return -1;
    if (elapsedMs < 0)
        return 0;
    int64_t end = 0;
    for (int i = 0; i < static_cast<int>(durations.size()); ++i) {
        end += std::max(durations[i], 0);
        if (elapsedMs < end)
            return i;
    }
    return -1;
}

// 把声音对齐到画面。leadMs 是声音第一个采样相对第一帧画面的时间差：正数表示声音
// 晚开始，前面补静音；负数表示声音早开始，裁掉多出来的开头。再截到画面总长
// limitMs，画面播完声音不再继续。samples 交错排列，按采样帧（每声道一个采样）处理。
inline void alignAudio(std::vector<int16_t>& samples, int sampleRate, int channels,
    int64_t leadMs, int64_t limitMs) {
    if (sampleRate <= 0 || channels <= 0 || limitMs <= 0 ||
        leadMs >= limitMs || leadMs > MAX_AUDIO_LEAD_MS || leadMs < -MAX_AUDIO_LEAD_MS) {
        samples.clear();
        return;
    }
    const auto sampleCount = [&](int64_t ms) {
        return static_cast<std::size_t>(ms * sampleRate / 1000) * static_cast<std::size_t>(channels);
    };
    if (leadMs > 0) {
        samples.insert(samples.begin(), sampleCount(leadMs), int16_t{ 0 });
    }
    else if (leadMs < 0) {
        const std::size_t drop = std::min(samples.size(), sampleCount(-leadMs));
        samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    const std::size_t keep = sampleCount(limitMs);
    if (samples.size() > keep)
        samples.resize(keep);
}

}
