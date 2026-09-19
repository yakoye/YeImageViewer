#pragma once

#include <algorithm>
#include <memory>

#include <windows.h>
#include <mmsystem.h>

#include "AudioClip.h"

#pragma comment(lib, "winmm.lib")

// 播放实况照片的声音。一段只有几秒、已整段解码成 PCM，整块交给 waveOut 一次写出即可，
// 不需要流式喂数据。支持从任意毫秒处开始，用于暂停后接着播。只在界面线程调用。
class LiveAudioPlayer {
public:
    LiveAudioPlayer() = default;
    LiveAudioPlayer(const LiveAudioPlayer&) = delete;
    LiveAudioPlayer& operator=(const LiveAudioPlayer&) = delete;

    ~LiveAudioPlayer() {
        stop();
    }

    // 没有可用的输出设备（例如远程桌面设成不播放声音）时返回 false，调用方按静音处理。
    bool play(std::shared_ptr<const AudioClip> clip, int64_t offsetMs) {
        stop();
        if (!clip || clip->empty())
            return false;

        const int64_t offset = std::clamp<int64_t>(offsetMs, 0, clip->durationMs());
        const std::size_t startSample = static_cast<std::size_t>(offset * clip->sampleRate / 1000) *
            static_cast<std::size_t>(clip->channels);
        if (startSample >= clip->samples.size())
            return false;

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(clip->channels);
        format.nSamplesPerSec = static_cast<DWORD>(clip->sampleRate);
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            device = nullptr;
            return false;
        }

        header = {};
        header.lpData = reinterpret_cast<LPSTR>(const_cast<int16_t*>(clip->samples.data() + startSample));
        header.dwBufferLength = static_cast<DWORD>((clip->samples.size() - startSample) * sizeof(int16_t));
        if (waveOutPrepareHeader(device, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            closeDevice();
            return false;
        }
        if (waveOutWrite(device, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(device, &header, sizeof(header));
            closeDevice();
            return false;
        }
        playing = std::move(clip);
        return true;
    }

    void stop() {
        if (!device)
            return;
        waveOutReset(device);   // 立即停下并把缓冲标记为已完成，之后才能反准备
        waveOutUnprepareHeader(device, &header, sizeof(header));
        closeDevice();
    }

private:
    void closeDevice() {
        waveOutClose(device);
        device = nullptr;
        playing.reset();
    }

    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    // 播放期间保住这段 PCM：图片被挤出缓存时，waveOut 读的内存不会被释放
    std::shared_ptr<const AudioClip> playing;
};
