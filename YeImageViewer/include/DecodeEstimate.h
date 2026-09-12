#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// 预估一张图还要解码多久，用于加载提示里的倒计时。
//
// 解码器没有进度回调，拿不到真实百分比，只能按「输出像素字节数 ÷ 该格式的解码吞吐」估。
// 输出字节数（宽 × 高 × 每像素位数 ÷ 8）是唯一靠谱的自变量——压缩后的文件大小完全不行：
// 实测同为 PNG，290 MB 的文件是 104 MB/s，0.44 MB 的文件只有 0.45 MB/s，差两个数量级，
// 因为 PNG 的压缩比取决于内容，文件大小根本不反映要 inflate 多少数据。
//
// 静态速率表按实测的**最慢**值取，宁可高估：倒计时提前归零却还没加载完，是明显的失信；
// 剩 1 秒时图已经出来了，提示直接消失，用户不会察觉。
//
// 解码完成后回采真实速率做指数滑动平均，同一文件夹里的后续图片就会越估越准。
namespace DecodeEstimate {

    // 实测值（Release / x64），单位 MB/s 的**输出**字节：
    //   jpg  8191x8193  201 MB / 81.7 ms = 2462      3840x1000  11.5 MB / 5.7 ms = 2022
    //   png  9000x9000  486 MB / 2793 ms =  174（真实照片内容）
    //        8191x8193  403 MB /  965 ms =  417（合成渐变，压缩比高，inflate 更快）
    //        4000x4000   96 MB /  229 ms =  419
    inline constexpr double JPEG_MB_PER_SEC = 2000.0;
    inline constexpr double PNG_MB_PER_SEC = 170.0;
    // 未标定格式的兜底。取得偏低，宁可高估耗时。
    inline constexpr double DEFAULT_MB_PER_SEC = 150.0;

    // 低于这个估值就不显示倒计时：图基本瞬间就出来了，数字只会闪一下
    inline constexpr int64_t MIN_SHOWN_MS = 250;
    // 估值上限，避免离谱的数字
    inline constexpr int64_t MAX_ESTIMATE_MS = 120'000;

    inline double staticRate(const std::wstring& ext) {
        if (ext == L"jpg" || ext == L"jpeg" || ext == L"jfif" || ext == L"jpe")
            return JPEG_MB_PER_SEC;
        if (ext == L"png" || ext == L"apng")
            return PNG_MB_PER_SEC;
        return DEFAULT_MB_PER_SEC;
    }

    inline std::wstring extensionOf(const std::wstring& path) {
        const auto dot = path.rfind(L'.');
        if (dot == std::wstring::npos || dot + 1 >= path.size())
            return {};
        std::wstring ext = path.substr(dot + 1);
        for (auto& c : ext)
            c = static_cast<wchar_t>(std::towlower(c));
        return ext;
    }

    inline int64_t outputBytes(int64_t width, int64_t height, int64_t bitsPerPixel) {
        if (width <= 0 || height <= 0)
            return 0;
        // 属性系统偶尔给不出位深（或给的是单通道值），按 32 位兜底，宁可高估
        if (bitsPerPixel <= 0 || bitsPerPixel > 128)
            bitsPerPixel = 32;
        return width * height * bitsPerPixel / 8;
    }

    // 按扩展名累积实测吞吐。多线程访问（预读线程写、主线程读），自带锁。
    class Model {
    public:
        int64_t estimateMs(const std::wstring& path, int64_t bytes) const {
            if (bytes <= 0)
                return 0;
            const double rate = rateFor(extensionOf(path));
            const double ms = static_cast<double>(bytes) / (rate * 1024.0 * 1024.0) * 1000.0;
            return std::clamp(static_cast<int64_t>(ms), int64_t{ 0 }, MAX_ESTIMATE_MS);
        }

        // 解码结束后回采。elapsedMs 过小的样本不要——计时噪声会把速率拉得很离谱。
        void record(const std::wstring& path, int64_t bytes, int64_t elapsedMs) {
            if (bytes <= 0 || elapsedMs < 30)
                return;
            const auto ext = extensionOf(path);
            if (ext.empty())
                return;

            const double observed = static_cast<double>(bytes) /
                (1024.0 * 1024.0) / (static_cast<double>(elapsedMs) / 1000.0);

            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_observed.find(ext);
            if (it == m_observed.end())
                m_observed.emplace(ext, observed);
            else
                it->second = it->second * (1.0 - SMOOTHING) + observed * SMOOTHING;
        }

    private:
        static constexpr double SMOOTHING = 0.35;

        double rateFor(const std::wstring& ext) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_observed.find(ext);
            if (it != m_observed.end() && it->second > 1.0)
                return it->second;
            return staticRate(ext);
        }

        mutable std::mutex m_mutex;
        std::unordered_map<std::wstring, double> m_observed;
    };

}
