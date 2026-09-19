// 生成实况照片测试用的视频片段：3 秒、30 fps、640x480 H.264 + 48 kHz 立体声 AAC。
//
// 用 Windows 自带的 Media Foundation 编码，不引入额外依赖。内容为验证音画对齐而设计：
//   画面  灰色渐变底上一根移动的竖条；第 30 帧（t = 1.000 s）整帧纯白
//   声音  全程一个轻微的 440 Hz 底音，让电平表始终有读数；
//         t = 1.000 s 起叠加 50 ms 的 1 kHz 响声
// 解码后「白帧的起始时间」与「响声的起点」之差，就是音画偏差。
//
// 编译与使用见 scripts/generate-live-photo-fixtures.ps1。

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr UINT32 WIDTH = 640;
constexpr UINT32 HEIGHT = 480;
constexpr UINT32 FPS = 30;
constexpr UINT32 SECONDS = 3;
constexpr UINT32 FLASH_FRAME = FPS * 1;          // t = 1.000 s
constexpr UINT32 SAMPLE_RATE = 48000;
constexpr UINT32 CHANNELS = 2;
constexpr UINT32 SAMPLES_PER_FRAME = SAMPLE_RATE / FPS;
constexpr double BEEP_START_S = 1.0;
constexpr double BEEP_LENGTH_S = 0.05;
constexpr LONGLONG TICKS_PER_SECOND = 10'000'000;  // Media Foundation 以 100 ns 为单位

template <typename T>
void release(T*& pointer) {
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

#define CHECK(expression)                                                              \
    do {                                                                               \
        const HRESULT hr_ = (expression);                                              \
        if (FAILED(hr_)) {                                                             \
            std::fprintf(stderr, "%s failed: 0x%08lX\n", #expression, (unsigned long)hr_); \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

int writeClip(const wchar_t* path) {
    IMFSinkWriter* writer = nullptr;
    CHECK(MFCreateSinkWriterFromURL(path, nullptr, nullptr, &writer));

    IMFMediaType* videoOut = nullptr;
    CHECK(MFCreateMediaType(&videoOut));
    videoOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    videoOut->SetUINT32(MF_MT_AVG_BITRATE, 500'000);
    videoOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(videoOut, MF_MT_FRAME_SIZE, WIDTH, HEIGHT);
    MFSetAttributeRatio(videoOut, MF_MT_FRAME_RATE, FPS, 1);
    MFSetAttributeRatio(videoOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD videoStream = 0;
    CHECK(writer->AddStream(videoOut, &videoStream));

    IMFMediaType* videoIn = nullptr;
    CHECK(MFCreateMediaType(&videoIn));
    videoIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    videoIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    videoIn->SetUINT32(MF_MT_DEFAULT_STRIDE, WIDTH * 4);   // 自上而下
    MFSetAttributeSize(videoIn, MF_MT_FRAME_SIZE, WIDTH, HEIGHT);
    MFSetAttributeRatio(videoIn, MF_MT_FRAME_RATE, FPS, 1);
    MFSetAttributeRatio(videoIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    CHECK(writer->SetInputMediaType(videoStream, videoIn, nullptr));

    IMFMediaType* audioOut = nullptr;
    CHECK(MFCreateMediaType(&audioOut));
    audioOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audioOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    audioOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    audioOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SAMPLE_RATE);
    audioOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, CHANNELS);
    audioOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);   // 128 kbps
    DWORD audioStream = 0;
    CHECK(writer->AddStream(audioOut, &audioStream));

    IMFMediaType* audioIn = nullptr;
    CHECK(MFCreateMediaType(&audioIn));
    audioIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audioIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    audioIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    audioIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SAMPLE_RATE);
    audioIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, CHANNELS);
    audioIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, CHANNELS * 2);
    audioIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, SAMPLE_RATE * CHANNELS * 2);
    CHECK(writer->SetInputMediaType(audioStream, audioIn, nullptr));

    CHECK(writer->BeginWriting());

    const LONGLONG frameTicks = TICKS_PER_SECOND / FPS;
    std::vector<int16_t> pcm(SAMPLES_PER_FRAME * CHANNELS);
    for (UINT32 frame = 0; frame < FPS * SECONDS; ++frame) {
        // 画面
        IMFMediaBuffer* videoBuffer = nullptr;
        CHECK(MFCreateMemoryBuffer(WIDTH * HEIGHT * 4, &videoBuffer));
        BYTE* pixels = nullptr;
        CHECK(videoBuffer->Lock(&pixels, nullptr, nullptr));
        const UINT32 barX = frame * (WIDTH - 40) / (FPS * SECONDS - 1);
        for (UINT32 y = 0; y < HEIGHT; ++y) {
            auto* row = reinterpret_cast<DWORD*>(pixels + static_cast<size_t>(y) * WIDTH * 4);
            for (UINT32 x = 0; x < WIDTH; ++x) {
                // 逐帧变化的细噪点：让码流有足够体积（解码器低于 64 KiB 的视频直接跳过），
                // 也更接近真实拍摄的画面
                const UINT32 noise = ((x * 7919u) ^ (y * 104729u) ^ (frame * 2654435761u)) % 6u;
                BYTE value = static_cast<BYTE>(40 + (x + y) * 80 / (WIDTH + HEIGHT) + noise);
                if (x >= barX && x < barX + 40)
                    value = 150;
                if (frame == FLASH_FRAME)
                    value = 255;
                row[x] = 0xFF000000u | (value << 16) | (value << 8) | value;
            }
        }
        videoBuffer->Unlock();
        videoBuffer->SetCurrentLength(WIDTH * HEIGHT * 4);
        IMFSample* videoSample = nullptr;
        CHECK(MFCreateSample(&videoSample));
        videoSample->AddBuffer(videoBuffer);
        videoSample->SetSampleTime(frame * frameTicks);
        videoSample->SetSampleDuration(frameTicks);
        CHECK(writer->WriteSample(videoStream, videoSample));
        release(videoSample);
        release(videoBuffer);

        // 这一帧时间段内的声音，与画面交错写入
        for (UINT32 i = 0; i < SAMPLES_PER_FRAME; ++i) {
            const double t = static_cast<double>(frame * SAMPLES_PER_FRAME + i) / SAMPLE_RATE;
            double value = 0.12 * std::sin(2.0 * 3.14159265358979 * 440.0 * t);
            if (t >= BEEP_START_S && t < BEEP_START_S + BEEP_LENGTH_S)
                value += 0.6 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
            const auto sample = static_cast<int16_t>(std::lround(value * 32767.0));
            for (UINT32 channel = 0; channel < CHANNELS; ++channel)
                pcm[i * CHANNELS + channel] = sample;
        }
        IMFMediaBuffer* audioBuffer = nullptr;
        const DWORD audioBytes = static_cast<DWORD>(pcm.size() * sizeof(int16_t));
        CHECK(MFCreateMemoryBuffer(audioBytes, &audioBuffer));
        BYTE* audioData = nullptr;
        CHECK(audioBuffer->Lock(&audioData, nullptr, nullptr));
        std::memcpy(audioData, pcm.data(), audioBytes);
        audioBuffer->Unlock();
        audioBuffer->SetCurrentLength(audioBytes);
        IMFSample* audioSample = nullptr;
        CHECK(MFCreateSample(&audioSample));
        audioSample->AddBuffer(audioBuffer);
        audioSample->SetSampleTime(frame * frameTicks);
        audioSample->SetSampleDuration(frameTicks);
        CHECK(writer->WriteSample(audioStream, audioSample));
        release(audioSample);
        release(audioBuffer);
    }

    CHECK(writer->Finalize());
    release(audioIn);
    release(audioOut);
    release(videoIn);
    release(videoOut);
    release(writer);
    return 0;
}

}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"usage: make-motion-clip <output.mp4>\n");
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) || FAILED(MFStartup(MF_VERSION))) {
        std::fwprintf(stderr, L"Media Foundation is not available on this system.\n");
        return 3;
    }
    const int result = writeClip(argv[1]);
    MFShutdown();
    CoUninitialize();
    return result;
}
