#pragma once

#include <cstdint>
#include <vector>

// 实况照片里随视频一起录下的声音，解码后统一成交错排列的 16 位 PCM。
// 只保留画面覆盖的那一段：画面播完，声音也该停。
struct AudioClip {
    int sampleRate = 0;
    int channels = 0;               // 1 或 2，多声道在解码时已混成立体声
    std::vector<int16_t> samples;   // 交错排列，长度 = 采样帧数 × 声道数

    bool empty() const {
        return sampleRate <= 0 || channels <= 0 || samples.empty();
    }

    int64_t frameCount() const {
        return channels > 0 ? static_cast<int64_t>(samples.size()) / channels : 0;
    }

    int64_t durationMs() const {
        return sampleRate > 0 ? frameCount() * 1000 / sampleRate : 0;
    }
};
