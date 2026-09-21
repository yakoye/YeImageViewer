#pragma once
#include "jarkUtils.h"
#include "LRU.h"
#include "ColorManager.h"
#include "HomeScreenLayout.h"
#include "TextDrawer.h"
#include "ShellThumbnail.h"
#include "DecodeEstimate.h"

#include "videoDecoder.h"
#include "SVGPreprocessor.h"
#include "SvgRenderer.h"


// stb_image v2.30  https://github.com/nothings/stb
#include "stb_image.h"

// QOI v2025.4.29  https://github.com/phoboslab/qoi
#include "qoi.h"

// opencv 4.13.0  https://github.com/opencv/opencv
#pragma comment(lib, "IlmImf.lib")
#pragma comment(lib, "ipphal.lib")
#pragma comment(lib, "ippicvmt.lib")
#pragma comment(lib, "ippiw.lib")
#pragma comment(lib, "ittnotify.lib")
#pragma comment(lib, "libjpeg-turbo.lib")
#pragma comment(lib, "libopenjp2.lib")
#pragma comment(lib, "libpng.lib")
#pragma comment(lib, "libtiff.lib")
#pragma comment(lib, "opencv_world4130.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "libwebp.lib")

// webp2 v0.0.1  https://chromium.googlesource.com/codecs/libwebp2
#include "src/wp2/base.h"
#include "src/wp2/decode.h"
#pragma comment(lib, "webp2.lib")
#pragma comment(lib, "imageio.lib")

// heif v1.20.1#1  https://github.com/strukturag/libheif
#include "libheif/heif.h"
#pragma comment(lib, "heif.lib")
#pragma comment(lib, "libde265.lib")

// avif v1.3.0  https://github.com/AOMediaCodec/libavif
#include "avif/avif.h"
#pragma comment(lib, "avif.lib")
#pragma comment(lib, "yuv.lib")
#pragma comment(lib, "dav1d.lib")
// libavif 与 libheif 都已改为只用 dav1d 解 AV1，不再链接 aom 的编码器。
// 但 FFmpeg 自带 libaom 编解码器包装（avcodec 里的 libaomenc/libaomdec）仍引用它，
// 所以 aom.lib 还不能去掉；等 FFmpeg 换成最小化构建后即可一并移除。
#pragma comment(lib, "aom.lib")

// libraw  v0.21.4  https://www.libraw.org/
#include "libraw/libraw.h"
#pragma comment(lib, "raw_r.lib")
#pragma comment(lib, "freeglut.lib")
#pragma comment(lib, "jasper.lib")
#pragma comment(lib, "lcms2.lib")

// exiv2  v0.28.5  https://exiv2.org/download.html
#include "exifParse.h"
#pragma comment(lib, "exiv2.lib")
#pragma comment(lib, "charset.lib")
#pragma comment(lib, "iconv.lib")
#pragma comment(lib, "inih.lib")
#pragma comment(lib, "INIReader.lib")
#pragma comment(lib, "intl.lib")
#pragma comment(lib, "libexpatMT.lib")
#pragma comment(lib, "brotlicommon.lib")
#pragma comment(lib, "brotlidec.lib")
#pragma comment(lib, "brotlienc.lib")

// libjxl v0.11.1  https://github.com/libjxl/libjxl
#include "jxl/decode_cxx.h"
#include "jxl/resizable_parallel_runner_cxx.h"
#include "jxl/types.h"
#pragma comment(lib, "jxl.lib")
#pragma comment(lib, "jxl_cms.lib")
#pragma comment(lib, "jxl_extras_codec.lib")
#pragma comment(lib, "jxl_threads.lib")

#pragma comment(lib, "hwy.lib")
#pragma comment(lib, "FreeImage.lib")
#pragma comment(lib, "FreeImagePlus.lib")
#pragma comment(lib, "pixman-1.lib")
#pragma comment(lib, "fontconfig.lib")

// psdsdk  https://github.com/MolecularMatters/psd_sdk
#include "psdsdk.h"
#pragma comment(lib, "Psd_MT.lib")

// lunasvg v3.5.0  https://github.com/sammycage/lunasvg
#include "lunasvg.h"
#pragma comment(lib, "lunasvg.lib")
#pragma comment(lib, "plutovg.lib")

// minizip  1.3.1#1
#include "minizip/unzip.h"
#pragma comment(lib, "minizip.lib")
#pragma comment(lib, "bz2.lib")

// lepton  https://github.com/jark006/liblepton
#include "liblepton.h"
#pragma comment(lib, "liblepton.lib")

// DirectXTex https://github.com/microsoft/DirectXTex
#include "DirectXTex.h"
#pragma comment(lib, "DirectXTex.lib")

// ffmpeg
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")

// ffmpeg 相关的第三方库
#pragma comment(lib, "snappy.lib")
#pragma comment(lib, "speex.lib")
#pragma comment(lib, "soxr.lib")
#pragma comment(lib, "SDL2-static.lib")
#pragma comment(lib, "opus.lib")
#pragma comment(lib, "openh264.lib")
#pragma comment(lib, "OpenCL.lib")
#pragma comment(lib, "OpenCLExt.lib")
#pragma comment(lib, "OpenCLUtils.lib")
#pragma comment(lib, "OpenCLUtilsCpp.lib")
#pragma comment(lib, "libmp3lame-static.lib")
#pragma comment(lib, "libmpghip-static.lib")
#pragma comment(lib, "libmfx.lib")
#pragma comment(lib, "libxml2.lib")
#pragma comment(lib, "vpx.lib")
#pragma comment(lib, "theora.lib")
#pragma comment(lib, "theoradec.lib")
#pragma comment(lib, "theoraenc.lib")
#pragma comment(lib, "ssh.lib")
#pragma comment(lib, "srt.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "openmpt.lib")
#pragma comment(lib, "mpg123.lib")
#pragma comment(lib, "out123.lib")
#pragma comment(lib, "syn123.lib")
#pragma comment(lib, "yasm.lib")
#pragma comment(lib, "vorbis.lib")
#pragma comment(lib, "vorbisenc.lib")
#pragma comment(lib, "vorbisfile.lib")
#pragma comment(lib, "ogg.lib")
#pragma comment(lib, "modplug.lib")
#pragma comment(lib, "lzma.lib")
#pragma comment(lib, "ilbc.lib")
#pragma comment(lib, "ass.lib")
#pragma comment(lib, "fribidi.lib")

#pragma comment(lib, "absl_random_seed_sequences.lib")
#pragma comment(lib, "absl_raw_hash_set.lib")
#pragma comment(lib, "absl_raw_logging_internal.lib")
#pragma comment(lib, "absl_scoped_set_env.lib")
#pragma comment(lib, "absl_spinlock_wait.lib")
#pragma comment(lib, "absl_stacktrace.lib")
#pragma comment(lib, "absl_status.lib")
#pragma comment(lib, "absl_statusor.lib")
#pragma comment(lib, "absl_str_format_internal.lib")
#pragma comment(lib, "absl_strerror.lib")
#pragma comment(lib, "absl_string_view.lib")
#pragma comment(lib, "absl_strings.lib")
#pragma comment(lib, "absl_strings_internal.lib")
#pragma comment(lib, "absl_symbolize.lib")
#pragma comment(lib, "absl_synchronization.lib")
#pragma comment(lib, "absl_throw_delegate.lib")
#pragma comment(lib, "absl_time.lib")
#pragma comment(lib, "absl_time_zone.lib")
#pragma comment(lib, "absl_tracing_internal.lib")
#pragma comment(lib, "absl_utf8_for_code_point.lib")
#pragma comment(lib, "absl_vlog_config_internal.lib")
#pragma comment(lib, "absl_bad_any_cast_impl.lib")
#pragma comment(lib, "absl_bad_optional_access.lib")
#pragma comment(lib, "absl_bad_variant_access.lib")
#pragma comment(lib, "absl_base.lib")
#pragma comment(lib, "absl_city.lib")
#pragma comment(lib, "absl_civil_time.lib")
#pragma comment(lib, "absl_cord.lib")
#pragma comment(lib, "absl_cord_internal.lib")
#pragma comment(lib, "absl_cordz_functions.lib")
#pragma comment(lib, "absl_cordz_handle.lib")
#pragma comment(lib, "absl_cordz_info.lib")
#pragma comment(lib, "absl_cordz_sample_token.lib")
#pragma comment(lib, "absl_crc_cord_state.lib")
#pragma comment(lib, "absl_crc_cpu_detect.lib")
#pragma comment(lib, "absl_crc_internal.lib")
#pragma comment(lib, "absl_crc32c.lib")
#pragma comment(lib, "absl_debugging_internal.lib")
#pragma comment(lib, "absl_decode_rust_punycode.lib")
#pragma comment(lib, "absl_demangle_internal.lib")
#pragma comment(lib, "absl_demangle_rust.lib")
#pragma comment(lib, "absl_die_if_null.lib")
#pragma comment(lib, "absl_examine_stack.lib")
#pragma comment(lib, "absl_exponential_biased.lib")
#pragma comment(lib, "absl_failure_signal_handler.lib")
#pragma comment(lib, "absl_flags_commandlineflag.lib")
#pragma comment(lib, "absl_flags_commandlineflag_internal.lib")
#pragma comment(lib, "absl_flags_config.lib")
#pragma comment(lib, "absl_flags_internal.lib")
#pragma comment(lib, "absl_flags_marshalling.lib")
#pragma comment(lib, "absl_flags_parse.lib")
#pragma comment(lib, "absl_flags_private_handle_accessor.lib")
#pragma comment(lib, "absl_flags_program_name.lib")
#pragma comment(lib, "absl_flags_reflection.lib")
#pragma comment(lib, "absl_flags_usage.lib")
#pragma comment(lib, "absl_flags_usage_internal.lib")
#pragma comment(lib, "absl_graphcycles_internal.lib")
#pragma comment(lib, "absl_hash.lib")
#pragma comment(lib, "absl_hashtablez_sampler.lib")
#pragma comment(lib, "absl_int128.lib")
#pragma comment(lib, "absl_kernel_timeout_internal.lib")
#pragma comment(lib, "absl_leak_check.lib")
#pragma comment(lib, "absl_log_entry.lib")
#pragma comment(lib, "absl_log_flags.lib")
#pragma comment(lib, "absl_log_globals.lib")
#pragma comment(lib, "absl_log_initialize.lib")
#pragma comment(lib, "absl_log_internal_check_op.lib")
#pragma comment(lib, "absl_log_internal_conditions.lib")
#pragma comment(lib, "absl_log_internal_fnmatch.lib")
#pragma comment(lib, "absl_log_internal_format.lib")
#pragma comment(lib, "absl_log_internal_globals.lib")
#pragma comment(lib, "absl_log_internal_log_sink_set.lib")
#pragma comment(lib, "absl_log_internal_message.lib")
#pragma comment(lib, "absl_log_internal_nullguard.lib")
#pragma comment(lib, "absl_log_internal_proto.lib")
#pragma comment(lib, "absl_log_internal_structured_proto.lib")
#pragma comment(lib, "absl_log_severity.lib")
#pragma comment(lib, "absl_log_sink.lib")
#pragma comment(lib, "absl_low_level_hash.lib")
#pragma comment(lib, "absl_malloc_internal.lib")
#pragma comment(lib, "absl_periodic_sampler.lib")
#pragma comment(lib, "absl_poison.lib")
#pragma comment(lib, "absl_random_distributions.lib")
#pragma comment(lib, "absl_random_internal_distribution_test_util.lib")
#pragma comment(lib, "absl_random_internal_platform.lib")
#pragma comment(lib, "absl_random_internal_pool_urbg.lib")
#pragma comment(lib, "absl_random_internal_randen.lib")
#pragma comment(lib, "absl_random_internal_randen_hwaes.lib")
#pragma comment(lib, "absl_random_internal_randen_hwaes_impl.lib")
#pragma comment(lib, "absl_random_internal_randen_slow.lib")
#pragma comment(lib, "absl_random_internal_seed_material.lib")
#pragma comment(lib, "absl_random_seed_gen_exception.lib")



class ImageDatabase :public LRU<wstring, ImageAsset> {
public:

    // 自 OpenCV 4.12 起支持的动态图像格式 gif png webp
    static inline const unordered_set<wstring_view> opencvAnimationExt{
        L"gif", L"png", L"apng", L"webp",
    };

    static inline const unordered_set<wstring_view> videoExt{
        L"mp4", L"mov", L"mkv", L"avi", L"wmv", L"flv", L"m4v", L"3gp", 
        L"mts", L"m2ts", L"vob", L"evo", L"ts", L"mxf", 
    };

    static inline const unordered_set<wstring_view> supportExt{
        L"apng", L"avif", L"avifs", L"blp", L"bmp", L"dds", L"dib", L"exr",
        L"gif", L"hdr", L"heic", L"heif", L"ico", L"icon", L"jfif", L"jp2",
        L"jpe", L"jpeg", L"jpg", L"jxl", L"jxr", L"lep", L"livp", L"pbm",
        L"pcx", L"pfm", L"pgm", L"pic", L"png", L"pnm", L"ppm", L"psd",
        L"psdt", L"pxm", L"qoi", L"ras", L"sr", L"svg", L"tga", L"tif",
        L"tiff", L"webm", L"webp", L"wp2",
    };

    static inline const unordered_set<wstring_view> supportRaw{
        L"crw", L"cr2", L"cr3", // Canon
        L"arw", L"srf", L"sr2", // Sony
        L"raw", L"dng", // Leica
        L"nef", // Nikon
        L"pef", // Pentax
        L"orf", // Olympus
        L"rw2", // Panasonic
        L"raf", // Fujifilm
        L"kdc", // Kodak
        L"x3f", // Sigma
        L"mrw", // Minolta
        L"3fr", // Hasselblad
        L"ari", // ARRIRAW
        L"bay", // Casio
        L"cap", // Phase One
        L"dcr", // Kodak
        L"dcs", // Kodak
        L"drf", // DNG+
        L"eip", // Enhanced Image Package, Phase One
        L"erf", // Epson
        L"fff", // Imacon/Hasselblad
        L"gpr", // GoPro
        L"iiq", // Phase One
        L"k25", // Kodak
        L"mdc", // Minolta
        L"mef", // Mamiya
        L"mos", // Leaf
        L"nrw", // Nikon
        L"ptx", // Pentax
        L"r3d", // Red Digital Cinema
        L"rwl", // Leica
        L"rwz", // Leica
        L"srw", // Samsung
    };

    ImageDatabase() {
        previewThread = std::thread(&ImageDatabase::previewWorker, this);
    }

    ~ImageDatabase() override {
        previewStop = true;
        previewCv.notify_all();
        if (previewThread.joinable())
            previewThread.join();
    }

    void setColorManagementWindow(HWND hwnd) {
        colorManager.setWindow(hwnd);
    }

    cv::Mat errorTipsMatDeep, errorTipsMatLight;
    std::array<cv::Mat, 4> homeMats;
    std::array<int, 4> homeMatDpis{};
    ColorManager colorManager;

    cv::Mat getErrorTipsMat() {
        if (errorTipsMatDeep.empty()) {
            auto rc = jarkUtils::GetResource(IDB_PNG_TIPS, L"PNG");
            cv::Mat imgData(1, (int)rc.size, CV_8UC1, (uint8_t*)rc.ptr);
            auto errorTipsMat = cv::imdecode(imgData, cv::IMREAD_UNCHANGED);
            if (GlobalVar::settingParameter.UI_LANG == 0) {
                errorTipsMatLight = errorTipsMat({ 0, 0, 800, 600 }).clone();
                errorTipsMatDeep = errorTipsMat({ 0, 600, 800, 600 }).clone();
            }
            else {
                errorTipsMatLight = errorTipsMat({ 800, 0, 800, 600 }).clone();
                errorTipsMatDeep = errorTipsMat({ 800, 600, 800, 600 }).clone();
            }
        }
        return GlobalVar::isCurrentUIDarkMode ? errorTipsMatDeep : errorTipsMatLight;
    }

    // getSafePtr 在等待解码超时后会返回 nullptr，调用方普遍直接解引用，
    // 这里统一兜底成错误提示图，避免空指针访问违规。
    std::shared_ptr<ImageAsset> getCheckedPtr(const wstring& key) {
        auto ptr = getSafePtr(key);
        return ptr ? ptr : makeErrorAsset();
    }

    std::shared_ptr<ImageAsset> getCheckedPtr(const wstring& key, const wstring& nextKey) {
        auto ptr = getSafePtr(key, nextKey);
        return ptr ? ptr : makeErrorAsset();
    }

    // 发起解码但不等待：拿得到就用真图，拿不到返回空，由调用方决定先显示什么。
    // 同时把模糊预览也排上队——解码要几秒的图，这几秒里靠预览顶着。
    std::shared_ptr<ImageAsset> tryGetOrRequest(const wstring& key, const wstring& nextKey) {
        requestPreload(key, true);
        if (key != nextKey)
            requestPreload(nextKey);

        auto ptr = tryGetPtr(key);
        if (!ptr)
            requestPreview(key);   // 真图已在缓存里就不必再要缩略图
        // 下一张多半马上就翻过去，预览提前备好，到时候不用现取
        requestPreview(nextKey);
        return ptr;
    }

    // 占位图本身只有 1 像素，不参与显示：绘制层看到这个尺寸就知道没有内容可画。
    // 给一个非空 Mat 是因为调用方普遍直接解引用 primaryFrame。
    std::shared_ptr<ImageAsset> makeLoadingAsset() {
        ImageAsset asset{ ImageFormat::Still,
            cv::Mat(1, 1, CV_8UC4, jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_DEEP)),
            {}, {}, "" };
        asset.isLoading = true;
        return std::make_shared<ImageAsset>(std::move(asset));
    }

    // 把已取到的系统缩略图包成一张可显示的图。还没取到或该文件没有缩略图时返回空。
    std::shared_ptr<ImageAsset> tryMakePreviewAsset(const wstring& key) {
        cv::Mat thumb;
        {
            std::lock_guard<std::mutex> lock(previewMutex);
            auto it = previewReady.find(key);
            if (it == previewReady.end() || it->second.thumb.empty())
                return nullptr;
            thumb = it->second.thumb;
        }

        ImageAsset asset{ ImageFormat::Still, std::move(thumb), {}, {}, "" };
        asset.isLoading = true;
        return std::make_shared<ImageAsset>(std::move(asset));
    }

    // 预估这张图还要解多久（毫秒）。尺寸还没查到、或估值太小不值得显示时返回 0。
    int64_t estimatedDecodeMs(const wstring& key) {
        std::lock_guard<std::mutex> lock(previewMutex);
        auto it = previewReady.find(key);
        if (it == previewReady.end())
            return 0;
        return it->second.estimatedMs;
    }

    // 解码完成后回采真实吞吐，让同类文件的后续估值收敛到本机实际水平
    void recordDecodeSample(const wstring& path, const cv::Mat& decoded, int64_t elapsedMs) {
        if (decoded.empty())
            return;
        const int64_t bytes = static_cast<int64_t>(decoded.total()) *
            static_cast<int64_t>(decoded.elemSize());
        decodeEstimate.record(path, bytes, elapsedMs);
    }

    // 请求为该文件取一张模糊预览。已取过（无论成败）或已在队列中则直接返回。
    void requestPreview(const wstring& key) {
        // 主页占位用的是窗口标题而非路径，不是真文件，没必要去问系统要缩略图
        if (key.empty() || key.find(L'\\') == wstring::npos)
            return;

        std::lock_guard<std::mutex> lock(previewMutex);
        if (previewReady.contains(key) || previewQueued.contains(key))
            return;

        previewQueued.insert(key);
        previewQueue.push_back(key);
        previewCv.notify_one();
    }

    std::shared_ptr<ImageAsset> makeErrorAsset() {
        return std::make_shared<ImageAsset>(ImageAsset{
            ImageFormat::Still, getErrorTipsMat(), {}, {}, getUIString(33) });
    }


    cv::Mat getHomeMat(int dpi = USER_DEFAULT_SCREEN_DPI) {
        dpi = std::max(1, dpi);
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        const bool dark = GlobalVar::isCurrentUIDarkMode;
        const std::size_t cacheIndex = (chinese ? 0 : 2) + (dark ? 1 : 0);
        auto& home = homeMats[cacheIndex];
        if (!home.empty() && homeMatDpis[cacheIndex] == dpi)
            return home;
        homeMatDpis[cacheIndex] = dpi;

        const auto scaled = [dpi](int value) {
            return std::max(1, HomeScreenLayout::scaleForDpi(value, dpi));
        };

        const uint32_t background = dark ? 0xFF15181Eu : 0xFFF5F7FAu;
        const uint32_t card = dark ? 0xFF20242Du : 0xFFFFFFFFu;
        const uint32_t primary = dark ? 0xFFF3F5F8u : 0xFF17202Du;
        const uint32_t secondary = dark ? 0xFFABB3C0u : 0xFF627086u;
        const uint32_t accent = dark ? 0xFF35AEE2u : 0xFF178FC8u;
        const uint32_t buttonHint = 0xFFEAF7FDu;

        home = cv::Mat(scaled(HomeScreenLayout::HEIGHT),
            scaled(HomeScreenLayout::WIDTH),
            CV_8UC4, jarkUtils::to_cv_scalar(background));

        const auto toRect = [&](const HomeScreenLayout::Rect& rect) {
            return cv::Rect{ scaled(rect.x), scaled(rect.y),
                scaled(rect.width), scaled(rect.height) };
        };
        const auto rounded = [&](const HomeScreenLayout::Rect& layout,
            uint32_t fill, int radius) {
            const cv::Rect rect = toRect(layout);
            const auto fillScalar = jarkUtils::to_cv_scalar(fill);
            radius = scaled(radius);
            radius = std::clamp(radius, 1, std::min(rect.width, rect.height) / 2);
            cv::rectangle(home,
                { rect.x + radius, rect.y, rect.width - radius * 2, rect.height },
                fillScalar, -1, cv::LINE_AA);
            cv::rectangle(home,
                { rect.x, rect.y + radius, rect.width, rect.height - radius * 2 },
                fillScalar, -1, cv::LINE_AA);
            cv::circle(home, { rect.x + radius, rect.y + radius }, radius,
                fillScalar, -1, cv::LINE_AA);
            cv::circle(home, { rect.x + rect.width - radius - 1, rect.y + radius }, radius,
                fillScalar, -1, cv::LINE_AA);
            cv::circle(home, { rect.x + radius, rect.y + rect.height - radius - 1 }, radius,
                fillScalar, -1, cv::LINE_AA);
            cv::circle(home,
                { rect.x + rect.width - radius - 1, rect.y + rect.height - radius - 1 },
                radius, fillScalar, -1, cv::LINE_AA);
        };

        TextDrawer drawer;
        drawer.setSize(scaled(24));
        drawer.putAlignCenter(home, toRect(HomeScreenLayout::TITLE),
            "YeImageViewer", primary);
        drawer.setSize(scaled(12));
        drawer.putAlignCenter(home, toRect(HomeScreenLayout::SUBTITLE),
            chinese ? "快速、清晰的 Windows 图片查看器" :
                "A fast and clear image viewer for Windows",
            secondary);

        rounded(HomeScreenLayout::OPEN_BUTTON, accent, 14);
        drawer.setSize(scaled(16));
        drawer.putAlignCenter(home,
            toRect({ HomeScreenLayout::OPEN_BUTTON.x + 16,
                HomeScreenLayout::OPEN_BUTTON.y + 7,
                HomeScreenLayout::OPEN_BUTTON.width - 32, 26 }),
            chinese ? "打开一张图片" : "Open an image", 0xFFFFFFFFu);
        drawer.setSize(scaled(12));
        drawer.putAlignCenter(home,
            toRect({ HomeScreenLayout::OPEN_BUTTON.x + 16,
                HomeScreenLayout::OPEN_BUTTON.y + 35,
                HomeScreenLayout::OPEN_BUTTON.width - 32, 22 }),
            chinese ? "点击这里选择图片，也可以直接拖入图片" :
                "Click here to choose an image, or drop one into this window",
            buttonHint);

        const std::array<const char*, 3> titles = chinese ?
            std::array<const char*, 3>{ "浏览", "查看", "更多" } :
            std::array<const char*, 3>{ "BROWSE", "VIEW", "MORE" };
        const std::array<std::array<const char*, 3>, 3> lines = chinese ?
            std::array<std::array<const char*, 3>, 3>{
                std::array<const char*, 3>{ "滚轮  上下浏览", "Ctrl+滚轮  缩放", "Shift+滚轮  左右" },
                std::array<const char*, 3>{ "拖动  平移图片", "双击/最大化  沉浸", "Esc  退出/关闭" },
                std::array<const char*, 3>{ "I/Tab  图片信息", "F2  重命名", "F3  自定义快捷键" },
            } :
            std::array<std::array<const char*, 3>, 3>{
                std::array<const char*, 3>{ "Wheel  Vertical", "Ctrl+Wheel  Zoom", "Shift+Wheel  Horizontal" },
                std::array<const char*, 3>{ "Drag  Pan image", "Double-click  Immersive", "Esc  Exit / close" },
                std::array<const char*, 3>{ "I/Tab  Image info", "F2  Rename", "F3  Shortcuts" },
            };

        for (std::size_t index = 0; index < HomeScreenLayout::GUIDE_CARDS.size(); ++index) {
            const auto& layout = HomeScreenLayout::GUIDE_CARDS[index];
            rounded(layout, card, 14);
            drawer.setSize(scaled(14));
            drawer.putAlignLeft(home,
                toRect({ layout.x + 10, layout.y + 4,
                    layout.width - 20, 24 }),
                titles[index], accent);
            drawer.setSize(scaled(12));
            for (int line = 0; line < 3; ++line) {
                drawer.putAlignLeft(home,
                    toRect({ layout.x + 10, layout.y + 30 + line * 20,
                        layout.width - 20, 20 }),
                    lines[index][line], line == 0 ? primary : secondary);
            }
        }

        drawer.setSize(scaled(12));
        drawer.putAlignCenter(home, toRect(HomeScreenLayout::FOOTER),
            chinese ? "设置 → 快捷键：所有按键和滚轮操作都可以修改" :
                "Settings > Shortcuts: customize every key and wheel action",
            secondary);
        return home;
    }


    static uint32_t swap_endian(uint32_t value) {
        return (value >> 24) |
            ((value >> 8) & 0x0000FF00) |
            ((value << 8) & 0x00FF0000) |
            (value << 24);
    }

    struct IconDirEntry {
        uint8_t width;
        uint8_t height;
        uint8_t colorCount;
        uint8_t reserved;
        uint16_t planes;
        uint16_t bitsPerPixel;
        uint32_t dataSize;
        uint32_t dataOffset;
    };

    cv::Mat readDibFromMemory(const uint8_t* data, const IconDirEntry& entry);

    // https://github.com/corkami/pics/blob/master/binary/ico_bmp.png
    std::tuple<cv::Mat, string> loadICO(wstring_view path, std::span<const uint8_t> buf);


    template <typename T, typename DataHolder>
    void* ExpandChannelToCanvas(psd::Allocator* allocator, const DataHolder* layer, const void* data, unsigned int canvasWidth, unsigned int canvasHeight)
    {
        T* canvasData = static_cast<T*>(allocator->Allocate(sizeof(T) * canvasWidth * canvasHeight, 16u));
        memset(canvasData, 0u, sizeof(T) * canvasWidth * canvasHeight);

        psd::imageUtil::CopyLayerData(static_cast<const T*>(data), canvasData, layer->left, layer->top, layer->right, layer->bottom, canvasWidth, canvasHeight);

        return canvasData;
    }


    void* ExpandChannelToCanvas(const psd::Document* document, psd::Allocator* allocator, psd::Layer* layer, psd::Channel* channel)
    {
        if (document->bitsPerChannel == 8)
            return ExpandChannelToCanvas<uint8_t>(allocator, layer, channel->data, document->width, document->height);
        else if (document->bitsPerChannel == 16)
            return ExpandChannelToCanvas<uint16_t>(allocator, layer, channel->data, document->width, document->height);
        else if (document->bitsPerChannel == 32)
            return ExpandChannelToCanvas<float32_t>(allocator, layer, channel->data, document->width, document->height);

        return nullptr;
    }


    template <typename T>
    void* ExpandMaskToCanvas(const psd::Document* document, psd::Allocator* allocator, T* mask)
    {
        if (document->bitsPerChannel == 8)
            return ExpandChannelToCanvas<uint8_t>(allocator, mask, mask->data, document->width, document->height);
        else if (document->bitsPerChannel == 16)
            return ExpandChannelToCanvas<uint16_t>(allocator, mask, mask->data, document->width, document->height);
        else if (document->bitsPerChannel == 32)
            return ExpandChannelToCanvas<float32_t>(allocator, mask, mask->data, document->width, document->height);

        return nullptr;
    }


    unsigned int FindChannel(psd::Layer* layer, int16_t channelType) {
        const int32_t CHANNEL_NOT_FOUND = UINT_MAX;

        for (unsigned int i = 0; i < layer->channelCount; ++i)
        {
            psd::Channel* channel = &layer->channels[i];
            if (channel->data && channel->type == channelType)
                return i;
        }

        return CHANNEL_NOT_FOUND;
    }

    template <typename T>
    struct TmpValue;

    // uint8_t的特化
    template <>
    struct TmpValue<uint8_t> {
        static constexpr uint8_t alphaMax = 255;
    };

    // uint16_t的特化
    template <>
    struct TmpValue<uint16_t> {
        static constexpr uint16_t alphaMax = 65535;
    };

    // float的特化
    template <>
    struct TmpValue<float> {
        static constexpr float alphaMax = 1.0f;
    };

    template <typename T>
    T* CreateInterleavedImage(psd::Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, unsigned int width, unsigned int height)
    {
        T* image = static_cast<T*>(allocator->Allocate(4ULL * width * height * sizeof(T), 16u));

        const T* r = static_cast<const T*>(srcR);
        const T* g = static_cast<const T*>(srcG);
        const T* b = static_cast<const T*>(srcB);
        psd::imageUtil::InterleaveRGB(b, g, r, TmpValue<T>::alphaMax, image, width, height); // RGB -> BGR

        return image;
    }


    template <typename T>
    T* CreateInterleavedImage(psd::Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, const void* srcA, unsigned int width, unsigned int height)
    {
        T* image = static_cast<T*>(allocator->Allocate(4ULL * width * height * sizeof(T), 16u));

        const T* r = static_cast<const T*>(srcR);
        const T* g = static_cast<const T*>(srcG);
        const T* b = static_cast<const T*>(srcB);
        const T* a = static_cast<const T*>(srcA);
        psd::imageUtil::InterleaveRGBA(b, g, r, a, image, width, height); // RGB -> BGR

        return image;
    }


    // https://github.com/MolecularMatters/psd_sdk
    cv::Mat loadPSD(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadSTB(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadSVG(wstring_view path, std::span<const uint8_t> buf, std::shared_ptr<SvgRenderer>* rendererOut = nullptr);
    cv::Mat loadPFM(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadQOI(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadPCX(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadSunRaster(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadBLP(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadHeic(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadRaw(wstring_view path, std::span<const uint8_t> buf);

    cv::Mat loadImageWinCOM(wstring_view path, std::span<const uint8_t> buf);
    cv::Mat loadImageOpenCV(wstring_view path, std::span<const uint8_t> buf);

    ImageAsset loadAvif(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadJXL(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadWP2(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadLivp(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadMotionPhoto(wstring_view path, std::span<const uint8_t> buf, bool isJPG);
    ImageAsset loadAnimation(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadTiff(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadLEP(wstring_view path, std::span<const uint8_t> buf);
    ImageAsset loadDDS(wstring_view path, std::span<const uint8_t> buf);

    void handleExifOrientation(int orientation, cv::Mat& img);
    ImageAsset myLoader(const wstring& path);
    ImageAsset loader(const wstring& path);

public:
    // 文件被删除或重命名后，系统缩略图也可能过期，跟缓存一起丢掉
    void erasePreview(const wstring& key) {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewReady.erase(key);
        previewQueued.erase(key);
        std::erase(previewOrder, key);
    }

    void clearPreviews() {
        std::lock_guard<std::mutex> lock(previewMutex);
        previewReady.clear();
        previewQueued.clear();
        previewQueue.clear();
        previewOrder.clear();
    }

private:
    // ── 模糊预览 ──────────────────────────────────────────────────────────
    // 预览图取自 Windows 缩略图服务，毫秒级返回，解码慢的大图靠它先顶上。
    // 取不到的文件也会在 previewReady 里留一条空记录，避免每次翻到都重试一遍。

    static constexpr int PREVIEW_MAX_EDGE = 1024;
    static constexpr std::size_t PREVIEW_CACHE_MAX = 16;

    struct PreviewEntry {
        cv::Mat thumb;            // 取不到缩略图时为空
        int64_t estimatedMs = 0;  // 0 表示估不出来或短到不值得显示
    };

    DecodeEstimate::Model decodeEstimate;
    std::unordered_map<wstring, PreviewEntry> previewReady;
    std::unordered_set<wstring> previewQueued;
    std::deque<wstring> previewQueue;   // 待取队列
    std::deque<wstring> previewOrder;   // 已取到的先后顺序，用于淘汰
    std::mutex previewMutex;
    std::condition_variable previewCv;
    std::thread previewThread;
    std::atomic<bool> previewStop{ false };

    void previewWorker() {
        // Shell 缩略图提供器是进程内 COM 组件，按 STA 初始化
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        while (true) {
            wstring key;
            {
                std::unique_lock<std::mutex> lock(previewMutex);
                previewCv.wait(lock, [this] {
                    return !previewQueue.empty() || previewStop.load(); });

                if (previewStop)
                    break;

                key = std::move(previewQueue.front());
                previewQueue.pop_front();
            }

            // 尺寸先查：它比缩略图快一个数量级，而倒计时要靠它起步
            const auto dims = ShellThumbnail::fetchDimensions(key);
            const int64_t bytes = DecodeEstimate::outputBytes(
                dims.width, dims.height, dims.bitsPerPixel);
            int64_t estimated = decodeEstimate.estimateMs(key, bytes);
            if (estimated < DecodeEstimate::MIN_SHOWN_MS)
                estimated = 0;

            cv::Mat thumb = ShellThumbnail::fetch(key, PREVIEW_MAX_EDGE);

            {
                std::lock_guard<std::mutex> lock(previewMutex);
                previewQueued.erase(key);
                previewReady[key] = PreviewEntry{ std::move(thumb), estimated };
                previewOrder.push_back(key);

                while (previewOrder.size() > PREVIEW_CACHE_MAX) {
                    previewReady.erase(previewOrder.front());
                    previewOrder.pop_front();
                }
            }
        }

        CoUninitialize();
    }
};
