#pragma once
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <span>
#include <vector>
#include <windows.h>

namespace ImageExport
{
// 与 drawCanvas 相同：先旋转，后按显示坐标镜像。绝不原地改缓存。
inline cv::Mat transform(const cv::Mat &source, int rotation, bool horizontal, bool vertical)
{
    cv::Mat result;
    switch (rotation & 3)
    {
    case 1:
        cv::rotate(source, result, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    case 2:
        cv::rotate(source, result, cv::ROTATE_180);
        break;
    case 3:
        cv::rotate(source, result, cv::ROTATE_90_CLOCKWISE);
        break;
    default:
        result = source.clone();
        break;
    }
    if (horizontal || vertical)
        cv::flip(result, result, horizontal && vertical ? -1 : horizontal ? 1 : 0);
    return result;
}
inline cv::Mat jpegSurface(const cv::Mat &source)
{
    if (source.channels() != 4)
        return source;
    cv::Mat result(source.rows, source.cols, CV_8UC3);
    for (int y = 0; y < source.rows; ++y)
    {
        const auto *input = source.ptr<cv::Vec4b>(y);
        auto *output = result.ptr<cv::Vec3b>(y);
        for (int x = 0; x < source.cols; ++x)
            for (int c = 0; c < 3; ++c)
                output[x][c] =
                    static_cast<uint8_t>((input[x][c] * input[x][3] + 255 * (255 - input[x][3]) + 127) / 255);
    }
    return result;
}
// CREATE_NEW 同时拒绝已有文件、硬链接和源路径。写入失败只能删除本次创建的文件。
template <typename Write>
inline bool writeNewWith(const std::filesystem::path &path, std::span<const uint8_t> bytes, Write write)
{
    if (bytes.empty())
        return false;
    HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    size_t position = 0;
    bool okay = true;
    while (position < bytes.size())
    {
        const DWORD requested = static_cast<DWORD>(std::min<size_t>(bytes.size() - position, 1ULL << 30));
        DWORD written = 0;
        if (!write(file, bytes.data() + position, requested, &written) || written != requested)
        {
            okay = false;
            break;
        }
        position += written;
    }
    if (okay)
        okay = FlushFileBuffers(file) != FALSE;
    if (!okay)
    {
        // 在独占句柄上删除本次文件，不能先关闭再 DeleteFile(path)：释放后路径可能被别人替换。
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    }
    CloseHandle(file);
    return okay;
}
inline bool writeNew(const std::filesystem::path &path, std::span<const uint8_t> bytes)
{
    return writeNewWith(path, bytes, [](HANDLE file, const void *data, DWORD requested, DWORD *written) {
        return WriteFile(file, data, requested, written, nullptr) != FALSE;
    });
}
inline bool saveNew(const std::filesystem::path &path, const cv::Mat &image, bool jpeg) noexcept
{
    try
    {
        if (image.empty() || image.depth() != CV_8U)
            return false;
        std::vector<uint8_t> bytes;
        if (!cv::imencode(jpeg ? ".jpg" : ".png", jpeg ? jpegSurface(image) : image, bytes))
            return false;
        return writeNew(path, bytes);
    }
    catch (...)
    {
        return false;
    }
}
} // namespace ImageExport
