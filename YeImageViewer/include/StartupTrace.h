#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

// 启动与加载耗时轨迹。只有设了环境变量 YEIMAGEVIEWER_STARTUP_TRACE=<文件路径>
// 才会记录，平时一个字节都不写。
//
// 为什么需要它：「打开一张图慢」从外面看只是一个总时间，而里面至少有
// CRT 静态初始化、建窗口、建 D3D 设备、解码、格式转换、色彩管理、首帧绘制
// 七八段，量级差得很远。没有这条轨迹就只能靠猜——之前就是靠它才发现
// 建 D3D 设备要七十毫秒、而解码在干等它，以及色彩管理在给 sRGB 图做恒等变换。
//
// 用法：
//     $env:YEIMAGEVIEWER_STARTUP_TRACE = "D:\trace.txt"
//     ./YeImageViewer.exe "D:\photo.png"
// 每行是「里程碑<TAB>距进程创建的毫秒数」。
namespace StartupTrace {

namespace detail {

inline std::wstring tracePath;
inline bool enabled = false;
inline std::mutex writeMutex;      // 解码在工作线程上，要和主线程抢这支笔

inline std::ofstream open() {
    return std::ofstream(std::filesystem::path(tracePath), std::ios::app);
}

}  // namespace detail

// 起点取进程创建时刻而不是进入 wWinMain 的时刻：一个静态链接的大程序，
// 光是 CRT 跑完各家静态库的全局构造就要二十来毫秒，那段时间用户已经在等了。
inline double millisecondsSinceProcessStart() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto toTicks = [](const FILETIME& time) {
        return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return (toTicks(now) - toTicks(created)) / 10000.0;   // 100 纳秒 → 毫秒
}

inline void initialize() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"YEIMAGEVIEWER_STARTUP_TRACE",
        buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    detail::tracePath.assign(buffer, length);
    detail::enabled = true;
    std::ofstream truncate(std::filesystem::path(detail::tracePath), std::ios::trunc);
}

inline bool isEnabled() {
    return detail::enabled;
}

inline void mark(const char* milestone) {
    if (!detail::enabled)
        return;
    const double elapsed = millisecondsSinceProcessStart();
    std::lock_guard<std::mutex> lock(detail::writeMutex);
    auto file = detail::open();
    if (file)
        file << milestone << '\t' << elapsed << '\n';
}

// 一次图片加载的分段耗时。解码、格式转换、色彩管理三段的量级差很远，
// 只记总时间看不出该优化哪一段。
inline void markLoad(const std::wstring& path, int64_t decodeMs, int64_t convertMs,
    int64_t iccReadMs, int64_t iccApplyMs) {
    if (!detail::enabled)
        return;
    const double elapsed = millisecondsSinceProcessStart();
    const auto name = std::filesystem::path(path).filename().string();
    std::lock_guard<std::mutex> lock(detail::writeMutex);
    auto file = detail::open();
    if (file) {
        file << "load:" << name << '\t' << elapsed
            << "\tdecode=" << decodeMs
            << "\tconvert=" << convertMs
            << "\ticcRead=" << iccReadMs
            << "\ticcApply=" << iccApplyMs << '\n';
    }
}

}  // namespace StartupTrace
