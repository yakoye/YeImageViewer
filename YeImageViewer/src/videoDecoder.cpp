#include "videoDecoder.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <iostream>

// FFmpeg Headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "MotionTiming.h"

// OpenCV Headers
#include <opencv2/opencv.hpp>


// 自定义删除器，用于 RAII 管理 FFmpeg 资源
struct AvMallocDeleter {
    void operator()(void* ptr) const {
        av_free(ptr);
    }
};

struct AvioContextDeleter {
    void operator()(AVIOContext* ctx) const {
        if (ctx) {
            av_freep(&ctx->buffer);
            avio_context_free(&ctx);
        }
    }
};

struct AvFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) avformat_close_input(&ctx);
    }
};

struct AvCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AvFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) av_frame_free(&frame);
    }
};

struct AvPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) sws_freeContext(ctx);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) swr_free(&ctx);
    }
};

// 实况的声音只有几秒；超过这个长度多半是文件异常，不再往下攒，免得占用失控
constexpr int64_t MAX_AUDIO_SECONDS = 60;

// 把解码出的音频帧转成交错排列的 16 位 PCM。多声道混成立体声，采样率保持原样，
// 交给系统混音器去重采样。第一个音频帧的时间戳记下来，用于和画面对齐。
class AudioCollector {
public:
    void add(const AVFrame* frame, AVRational timeBase) {
        if (!frame || frame->nb_samples <= 0 || frame->sample_rate <= 0 || frame->ch_layout.nb_channels <= 0)
            return;
        if (!swr && !open(frame))
            return;
        if (frame->sample_rate != sampleRate)
            return; // 中途变采样率的流极少见，丢掉这些帧比播出变调的声音好

        if (firstPtsMs == MotionTiming::NO_TIMESTAMP && frame->best_effort_timestamp != AV_NOPTS_VALUE)
            firstPtsMs = av_rescale_q(frame->best_effort_timestamp, timeBase, AVRational{ 1, 1000 });
        convert(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    }

    // 解码结束后把重采样器里剩下的样本取出来
    void flush() {
        if (swr)
            convert(nullptr, 0);
    }

    int sampleRate = 0;
    int channels = 0;
    std::vector<int16_t> samples;
    int64_t firstPtsMs = MotionTiming::NO_TIMESTAMP;

private:
    bool open(const AVFrame* frame) {
        AVChannelLayout inLayout{};
        if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&inLayout, frame->ch_layout.nb_channels);
        else if (av_channel_layout_copy(&inLayout, &frame->ch_layout) < 0)
            return false;

        const int outChannels = frame->ch_layout.nb_channels >= 2 ? 2 : 1;
        AVChannelLayout outLayout{};
        av_channel_layout_default(&outLayout, outChannels);

        SwrContext* context = nullptr;
        const int result = swr_alloc_set_opts2(&context, &outLayout, AV_SAMPLE_FMT_S16, frame->sample_rate,
            &inLayout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
        if (result < 0 || !context || swr_init(context) < 0) {
            if (context)
                swr_free(&context);
            JARK_LOG("Failed to set up audio resampler");
            return false;
        }
        swr.reset(context);
        sampleRate = frame->sample_rate;
        channels = outChannels;
        return true;
    }

    void convert(const uint8_t** input, int inputSamples) {
        const size_t limit = static_cast<size_t>(MAX_AUDIO_SECONDS * sampleRate) * static_cast<size_t>(channels);
        while (samples.size() < limit) {
            const int capacity = swr_get_out_samples(swr.get(), inputSamples);
            if (capacity <= 0)
                return;
            std::vector<int16_t> buffer(static_cast<size_t>(capacity) * static_cast<size_t>(channels));
            uint8_t* output[1] = { reinterpret_cast<uint8_t*>(buffer.data()) };
            const int converted = swr_convert(swr.get(), output, capacity, input, inputSamples);
            if (converted <= 0)
                return;
            const size_t count = std::min(static_cast<size_t>(converted) * static_cast<size_t>(channels),
                limit - samples.size());
            samples.insert(samples.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
            if (input)
                return; // 有输入时一次就转完；冲刷（input 为空）要循环到取空为止
        }
    }

    std::unique_ptr<SwrContext, SwrContextDeleter> swr;
};

// 用于自定义 AVIO 上下文的私有数据结构
struct BufferContext {
    const uint8_t* buffer;
    size_t size;
    size_t offset;
};

constexpr int AVIO_BUFFER_SIZE = 4096;
constexpr size_t MAX_RGB_FRAME_BUFFER_BYTES = 1024ULL * 1024ULL * 1024ULL;

static bool checked_add_i64(int64_t lhs, int64_t rhs, int64_t& result) {
    if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)) {
        return false;
    }

    result = lhs + rhs;
    return true;
}

static void init_ffmpeg_network_once() {
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        avformat_network_init();
    });
}

// AVIO 读取回调
static int io_read_packet(void* opaque, uint8_t* buf, int buf_size) {
    BufferContext* ctx = static_cast<BufferContext*>(opaque);
    if (!ctx || !buf || buf_size <= 0 || ctx->offset > ctx->size) {
        return AVERROR(EINVAL);
    }

    size_t remaining = ctx->size - ctx->offset;
    size_t to_copy = std::min(static_cast<size_t>(buf_size), remaining);

    if (to_copy == 0) return AVERROR_EOF;

    std::memcpy(buf, ctx->buffer + ctx->offset, to_copy);
    ctx->offset += to_copy;
    return static_cast<int>(to_copy);
}

// AVIO 寻址回调
static int64_t io_seek(void* opaque, int64_t offset, int whence) {
    BufferContext* ctx = static_cast<BufferContext*>(opaque);
    if (!ctx || ctx->size > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
        return AVERROR(EINVAL);
    }

    int64_t new_offset = 0;
    const int64_t size = static_cast<int64_t>(ctx->size);
    const int64_t current = static_cast<int64_t>(ctx->offset);

    if (whence == AVSEEK_SIZE) {
        return size;
    }

    if (whence == SEEK_SET) {
        new_offset = offset;
    }
    else if (whence == SEEK_CUR) {
        if (!checked_add_i64(current, offset, new_offset)) {
            return AVERROR(EINVAL);
        }
    }
    else if (whence == SEEK_END) {
        if (!checked_add_i64(size, offset, new_offset)) {
            return AVERROR(EINVAL);
        }
    }
    else {
        return AVERROR(EINVAL);
    }

    if (new_offset < 0 || new_offset > size) {
        return AVERROR(EINVAL);
    }

    ctx->offset = static_cast<size_t>(new_offset);
    return new_offset;
}

// 获取旋转角度 (度)
static int get_rotation_angle(AVStream* stream) {
    int32_t rotate = 0;
    if (!stream || !stream->codecpar) {
        return 0;
    }

    for (int i = 0; i < stream->codecpar->nb_coded_side_data; i++) {
        if (stream->codecpar->coded_side_data[i].type == AV_PKT_DATA_DISPLAYMATRIX) {
            rotate = (int)av_display_rotation_get(reinterpret_cast<const int32_t*>(stream->codecpar->coded_side_data[i].data));
            break;
        }
    }
    // FFmpeg 中正值表示逆时针，OpenCV 旋转枚举通常基于顺时针逻辑，这里统一转换为标准角度
    return -rotate;
}

// 视频解码 若 maxFrames > 0 则限制解码帧数，适用于预览等场景
MotionClip DecodeMotionClip(const uint8_t* videoBuffer, size_t size, size_t maxFrames, bool withAudio) {
    MotionClip clip;
    if (!videoBuffer || size < MIN_VIDEO_BUFF_SIZE) {
        JARK_LOG("Invalid video buffer: 0x{:X} or size: {} bytes", reinterpret_cast<std::uintptr_t>(videoBuffer), size);
        return clip;
    }
    if (size > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
        JARK_LOG("Video buffer is too large: {} bytes", size);
        return clip;
    }

    // 1. 初始化 FFmpeg (通常应用程序启动时调用一次即可，这里确保安全性)
    init_ffmpeg_network_once();

    // 2. 准备自定义 IO 上下文
    BufferContext bufferCtx{ videoBuffer, size, 0 };
    std::unique_ptr<uint8_t, AvMallocDeleter> ioBuffer(static_cast<uint8_t*>(av_malloc(AVIO_BUFFER_SIZE)));
    if (!ioBuffer) {
        JARK_LOG("bad_alloc");
        return clip;
    }

    std::unique_ptr<AVIOContext, AvioContextDeleter> avioCtx(avio_alloc_context(
        ioBuffer.get(), AVIO_BUFFER_SIZE, 0, &bufferCtx, io_read_packet, nullptr, io_seek
    ));
    if (!avioCtx) {
        JARK_LOG("Failed to allocate AVIOContext");
        return clip;
    }
    // avio_alloc_context() takes ownership of the current buffer pointer.
    ioBuffer.release();

    // 3. 打开输入格式上下文
    std::unique_ptr<AVFormatContext, AvFormatContextDeleter> formatCtx(avformat_alloc_context());
    if (!formatCtx) {
        JARK_LOG("Failed to allocate AVFormatContext");
        return clip;
    }
    formatCtx->pb = avioCtx.get();
    // Keep custom IO ownership with avioCtx; formatCtx is destroyed first.
    formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // 文件名设为空，因为我们是流式输入
    auto rawFormatCtx = formatCtx.release();
    int result = avformat_open_input(&rawFormatCtx, "", nullptr, nullptr);
    if (result < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        JARK_LOG("Failed to open input stream: {}", av_make_error_string(errStr, sizeof(errStr), result));
        return clip;
    }
    formatCtx.reset(rawFormatCtx);

    result = avformat_find_stream_info(formatCtx.get(), nullptr);
    if (result < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        JARK_LOG("Failed to find stream info: {}", av_make_error_string(errStr, sizeof(errStr), result));
        return clip;
    }

    // 4. 查找视频流
    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        JARK_LOG("No video stream found");
        return clip;
    }

    AVStream* videoStream = formatCtx->streams[videoStreamIndex];

    // 5. 获取解码器并打开
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        JARK_LOG("Codec not found");
        return clip;
    }

    std::unique_ptr<AVCodecContext, AvCodecContextDeleter> codecCtx(avcodec_alloc_context3(codec));
    if (!codecCtx) {
        JARK_LOG("Failed to allocate codec context");
        return clip;
    }

    if (avcodec_parameters_to_context(codecCtx.get(), videoStream->codecpar) < 0) {
        JARK_LOG("Failed to copy codec parameters");
        return clip;
    }

    // 设置多线程解码 (可选，小视频单线程也够)
    codecCtx->thread_count = 8;

    if (avcodec_open2(codecCtx.get(), codec, nullptr) < 0) {
        JARK_LOG("Failed to open codec");
        return clip;
    }

    // Some vendor motion photos contain an obfuscated or otherwise unsupported
    // video stream. FFmpeg can identify the stream dimensions while leaving the
    // pixel format unknown; passing AV_PIX_FMT_NONE to sws_getContext aborts the
    // whole process with an assertion instead of returning a decode error.
    if (codecCtx->pix_fmt == AV_PIX_FMT_NONE) {
        JARK_LOG("Unknown video pixel format");
        return clip;
    }

    if (codecCtx->width <= 0 || codecCtx->height <= 0 ||
        av_image_check_size(static_cast<unsigned int>(codecCtx->width), static_cast<unsigned int>(codecCtx->height), 0, nullptr) < 0) {
        JARK_LOG("Invalid video frame size: {}x{}", codecCtx->width, codecCtx->height);
        return clip;
    }

    // 音轨：解不开就当无声，不影响画面
    int audioStreamIndex = -1;
    std::unique_ptr<AVCodecContext, AvCodecContextDeleter> audioCtx;
    if (withAudio) {
        const AVCodec* audioCodec = nullptr;
        const int found = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, videoStreamIndex, &audioCodec, 0);
        if (found >= 0 && audioCodec) {
            audioCtx.reset(avcodec_alloc_context3(audioCodec));
            if (audioCtx && avcodec_parameters_to_context(audioCtx.get(), formatCtx->streams[found]->codecpar) >= 0 &&
                avcodec_open2(audioCtx.get(), audioCodec, nullptr) >= 0) {
                audioStreamIndex = found;
            }
            else {
                JARK_LOG("Audio decoder could not be opened; motion plays silently");
                audioCtx.reset();
            }
        }
    }

    // 计算目标尺寸：如果宽或高大于 1920，缩放到最长边为 1920
    int srcWidth = codecCtx->width;
    int srcHeight = codecCtx->height;
    int dstWidth = srcWidth;
    int dstHeight = srcHeight;

    if (srcWidth > 1920 || srcHeight > 1920) {
        if (srcWidth >= srcHeight) {
            // 宽边更长或相等
            dstWidth = 1920;
            dstHeight = static_cast<int>(srcHeight * 1920.0 / srcWidth + 0.5); // 四舍五入
        } else {
            // 高边更长
            dstHeight = 1920;
            dstWidth = static_cast<int>(srcWidth * 1920.0 / srcHeight + 0.5); // 四舍五入
        }
        // 确保尺寸至少为 1
        if (dstWidth < 1) dstWidth = 1;
        if (dstHeight < 1) dstHeight = 1;
    }

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, dstWidth, dstHeight, 1);
    if (numBytes <= 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        JARK_LOG("Invalid RGB frame buffer size: {} ({})", numBytes, av_make_error_string(errStr, sizeof(errStr), numBytes));
        return clip;
    }
    if (static_cast<size_t>(numBytes) > MAX_RGB_FRAME_BUFFER_BYTES) {
        JARK_LOG("RGB frame buffer is too large: {} bytes", numBytes);
        return clip;
    }

    // 6. 准备颜色空间转换上下文 (YUV -> BGR)，并应用缩放
    std::unique_ptr<SwsContext, SwsContextDeleter> swsCtx(sws_getContext(
        srcWidth, srcHeight, codecCtx->pix_fmt,
        dstWidth, dstHeight, AV_PIX_FMT_BGR24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));

    if (!swsCtx) {
        JARK_LOG("Failed to create SwsContext");
        return clip;
    }

    // 7. 准备帧和包
    std::unique_ptr<AVFrame, AvFrameDeleter> frame(av_frame_alloc());
    std::unique_ptr<AVFrame, AvFrameDeleter> rgbFrame(av_frame_alloc());
    std::unique_ptr<AVFrame, AvFrameDeleter> audioFrame(av_frame_alloc());
    std::unique_ptr<AVPacket, AvPacketDeleter> packet(av_packet_alloc());

    if (!frame || !rgbFrame || !audioFrame || !packet) {
        JARK_LOG("Failed to allocate frames/packets");
        return clip;
    }

    // 为 rgbFrame 分配缓冲区
    std::unique_ptr<uint8_t, AvMallocDeleter> rgbBuffer(static_cast<uint8_t*>(av_malloc(static_cast<size_t>(numBytes))));
    if (!rgbBuffer) {
        JARK_LOG("bad_alloc");
        return clip;
    }

    // 将 rgbBuffer 关联到 rgbFrame
    result = av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer.get(),
        AV_PIX_FMT_BGR24, dstWidth, dstHeight, 1);
    if (result < 0) {
        char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        JARK_LOG("Failed to fill RGB frame arrays: {}", av_make_error_string(errStr, sizeof(errStr), result));
        return clip;
    }

    // 获取旋转信息
    int rotation = get_rotation_angle(videoStream);

    std::vector<int64_t> ptsMs;   // 每帧的呈现时间戳，算每帧时长、和声音对齐都靠它
    AudioCollector audio;
    const AVRational audioTimeBase = audioStreamIndex >= 0 ?
        formatCtx->streams[audioStreamIndex]->time_base : AVRational{ 1, 1000 };
    const auto reachedLimit = [&] { return maxFrames && clip.frames.size() >= maxFrames; };

    // 从视频解码器取出所有已就绪的帧：颜色转换、缩放、按显示矩阵旋转。
    // 主循环与结尾冲刷共用。单帧解码出错只跳过这一帧，与原先的处理一致。
    const auto receiveVideoFrames = [&] {
        while (!reachedLimit()) {
            int receiveResult = avcodec_receive_frame(codecCtx.get(), frame.get());
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            else if (receiveResult < 0) {
                char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
                JARK_LOG("Error decoding frame: {}", av_make_error_string(errStr, sizeof(errStr), receiveResult));
                break;
            }

            // 9. 颜色转换 (YUV -> BGR) 并缩放
            int scaledRows = sws_scale(swsCtx.get(), frame->data, frame->linesize, 0, srcHeight,
                rgbFrame->data, rgbFrame->linesize);
            if (scaledRows <= 0) {
                JARK_LOG("Failed to convert video frame: {} rows scaled", scaledRows);
                continue;
            }

            // 10. 创建 cv::Mat (注意：OpenCV 使用 BGR)
            // 数据是连续的，直接封装
            cv::Mat decodedMat(dstHeight, dstWidth, CV_8UC3, rgbFrame->data[0]);

            // 深拷贝数据，因为 rgbBuffer 会在下一帧被覆盖
            cv::Mat finalMat = decodedMat.clone();

            // 11. 处理旋转
            if (rotation != 0) {
                int cvRotateCode = -1;
                int normRot = (rotation % 360 + 360) % 360;

                if (normRot == 90) {
                    cvRotateCode = cv::ROTATE_90_CLOCKWISE;
                }
                else if (normRot == 180) {
                    cvRotateCode = cv::ROTATE_180;
                }
                else if (normRot == 270) {
                    cvRotateCode = cv::ROTATE_90_COUNTERCLOCKWISE;
                }

                if (cvRotateCode >= 0) {
                    cv::rotate(finalMat, finalMat, cvRotateCode);
                }
            }

            ptsMs.push_back(frame->best_effort_timestamp == AV_NOPTS_VALUE ? MotionTiming::NO_TIMESTAMP :
                av_rescale_q(frame->best_effort_timestamp, videoStream->time_base, AVRational{ 1, 1000 }));
            clip.frames.push_back(std::move(finalMat));
        }
    };

    const auto receiveAudioFrames = [&] {
        while (avcodec_receive_frame(audioCtx.get(), audioFrame.get()) >= 0) {
            audio.add(audioFrame.get(), audioTimeBase);
            av_frame_unref(audioFrame.get());
        }
    };

    // 8. 解码循环
    bool videoSendFailed = false;
    while (!reachedLimit() && av_read_frame(formatCtx.get(), packet.get()) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            int sendResult = avcodec_send_packet(codecCtx.get(), packet.get());
            if (sendResult < 0) {
                char errStr[AV_ERROR_MAX_STRING_SIZE] = { 0 };
                JARK_LOG("Error sending packet: {}", av_make_error_string(errStr, sizeof(errStr), sendResult));
                av_packet_unref(packet.get());
                videoSendFailed = true;
                break;
            }
            receiveVideoFrames();
        }
        else if (packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(audioCtx.get(), packet.get()) >= 0)
                receiveAudioFrames();
        }
        av_packet_unref(packet.get());
    }

    // 冲刷解码器：多线程解码会在内部压住最后几帧，不冲刷就永远拿不到——
    // 3 秒 90 帧的视频只能解出八十几帧，结尾那一截画面被吞掉，配上声音就是画面先停。
    if (!videoSendFailed && !reachedLimit() && avcodec_send_packet(codecCtx.get(), nullptr) >= 0)
        receiveVideoFrames();
    if (audioCtx && avcodec_send_packet(audioCtx.get(), nullptr) >= 0)
        receiveAudioFrames();
    audio.flush();

    clip.frameDurationsMs = MotionTiming::durationsFromTimestamps(ptsMs,
        MotionTiming::frameMsFromRate(videoStream->avg_frame_rate.num, videoStream->avg_frame_rate.den));

    if (!clip.frames.empty() && !audio.samples.empty()) {
        const auto firstVideoPts = std::find_if(ptsMs.begin(), ptsMs.end(),
            [](int64_t pts) { return pts != MotionTiming::NO_TIMESTAMP; });
        const int64_t leadMs = firstVideoPts != ptsMs.end() && audio.firstPtsMs != MotionTiming::NO_TIMESTAMP ?
            audio.firstPtsMs - *firstVideoPts : 0;
        MotionTiming::alignAudio(audio.samples, audio.sampleRate, audio.channels, leadMs,
            MotionTiming::totalMs(clip.frameDurationsMs));
        if (!audio.samples.empty()) {
            auto decoded = std::make_shared<AudioClip>();
            decoded->sampleRate = audio.sampleRate;
            decoded->channels = audio.channels;
            decoded->samples = std::move(audio.samples);
            clip.audio = std::move(decoded);
        }
    }

    return clip;
}

std::vector<cv::Mat> DecodeVideoFrames(const uint8_t* videoBuffer, size_t size, size_t maxFrames) {
    return DecodeMotionClip(videoBuffer, size, maxFrames, false).frames;
}
