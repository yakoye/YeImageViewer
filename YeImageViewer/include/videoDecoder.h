#pragma once

#include "jarkUtils.h"
#include "AudioClip.h"

// 实况照片（动态照片）的视频部分。
struct MotionClip {
    std::vector<cv::Mat> frames;
    std::vector<int> frameDurationsMs;        // 与 frames 等长，按每帧的呈现时间戳算出
    std::shared_ptr<const AudioClip> audio;   // 没有音轨或音轨解不开时为空
};

// 解码内存中的视频。maxFrames > 0 时限制帧数；withAudio 为 false 时跳过音轨
// （强行打开的视频文件、webm 动图不播声音，没必要解）。
MotionClip DecodeMotionClip(const uint8_t* videoBuffer, size_t size, size_t maxFrames = 0, bool withAudio = true);

// 只取画面帧。
std::vector<cv::Mat> DecodeVideoFrames(const uint8_t* videoBuffer, size_t size, size_t maxFrames = 0);
