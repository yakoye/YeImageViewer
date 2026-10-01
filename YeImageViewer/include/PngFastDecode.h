#pragma once

#include <cstdint>
#include <span>

#include <opencv2/core.hpp>

// 大 PNG 的快路径：整块解压 + 一趟去滤波并直接写进目标 Mat。
//
// 为什么要绕开 OpenCV/libpng：libpng 是逐行流式的，解完一行回调一次，
// OpenCV 再把行拷进 Mat，16 位图还要单独扫一遍做字节序转换。
// 对一张 9000×9000、16 位的 PNG（原始数据 463 MB），这条路要 1513 ms，而
// 「拼好 IDAT 一次解完 + 去滤波时顺手写进 Mat」只要约 1050 ms——实测差 460 ms。
// 省下来的大头不是解压更快（libdeflate 比 zlib-ng 快 1.32 倍，630 对 827 ms），
// 而是少扫了好几百 MB：行刚去完滤波还在 L2 里，顺手就转好写出去了。
//
// 适用范围刻意收得很窄，拿不准的一律退回 OpenCV：
//   非交错、位深 8 或 16、色彩类型 0（灰度）/ 2（真彩）/ 6（真彩+alpha）、
//   没有 tRNS 透明色、文件够大（小图本来就快，不值得冒第二套实现的风险）。
// 调色板、交错、1/2/4 位、tRNS、APNG 一概不接。
//
// 输出必须与 OpenCV 的 IMREAD_UNCHANGED 逐字节一致，这是硬要求：
//   类型 2 → CV_8UC3 / CV_16UC3，BGR 顺序
//   类型 6 → CV_8UC4 / CV_16UC4，BGRA 顺序
//   类型 0 → CV_8UC1 / CV_16UC1
//   16 位一律转成小端（OpenCV 走的是 png_set_swap）
// `--png-decode-selftest` 会拿两条路对同一批文件逐字节比对，发布闸门里跑。
namespace PngFastDecode {

// 小于这个大小的 PNG 不走快路径：解码本来就是几十毫秒，
// 没必要让它们承担第二套解码实现的风险。
inline constexpr std::size_t MIN_FILE_BYTES = 4u << 20;   // 4 MiB

// 这张图能不能走快路径——只看文件头，不解码。
// 给自检用：自检要能枚举出「本该走快路径」的文件，逐一和 OpenCV 比对。
bool isSupported(std::span<const std::uint8_t> file, bool ignoreSizeLimit = false);

// 解码。返回 false 表示不适用或中途发现不对劲，调用方必须退回 OpenCV。
// 失败时 out 不保证内容，调用方不要用。
bool decode(std::span<const std::uint8_t> file, cv::Mat& out, bool ignoreSizeLimit = false);

}  // namespace PngFastDecode
