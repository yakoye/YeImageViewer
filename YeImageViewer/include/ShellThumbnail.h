#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <string>
#include <opencv2/opencv.hpp>

#pragma comment(lib, "propsys.lib")

// 向 Windows 缩略图服务索取预览图。
//
// 这是目前唯一能在毫秒级拿到大图预览的通道。实测 test/bigimage/moon_81M.png
// （290 MB、9000×9000、16 位 PNG）：
//   cv::imdecode 全解码             2810 ms
//   IMREAD_REDUCED_COLOR_8          2735 ms  —— 完全没有加速，PNG 的降采样发生在解码之后
//   IShellItemImageFactory 1024px      8 ms
// PNG 是非隔行的 zlib 流，逐行之间有 filter 依赖，不解完整条流就拿不到任何低分辨率版本，
// 所以「先解一张小图顶上」这条路在格式层面是堵死的，只能从系统缩略图缓存拿。
//
// EXIF 方向：Shell 返回的缩略图已经应用过方向（实测 orientation=6 的图存储 400×600，
// 缩略图返回 600×400），与本程序解码后的朝向一致，不需要再补偿。
namespace ShellThumbnail {

    // 返回 BGRA 格式的 cv::Mat，失败返回空 Mat。
    // 调用线程必须已初始化 COM。
    inline cv::Mat fetch(const std::wstring& path, int maxEdge) {
        if (path.empty() || maxEdge <= 0)
            return {};

        IShellItemImageFactory* factory = nullptr;
        if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))))
            return {};

        HBITMAP bitmap = nullptr;
        SIZE size{ maxEdge, maxEdge };
        // SIIGBF_THUMBNAILONLY：取不到真缩略时不要退化成文件类型图标，
        // 否则预览阶段会闪一个跟图片内容无关的图标，比留白更糟。
        const HRESULT hr = factory->GetImage(size, SIIGBF_THUMBNAILONLY, &bitmap);
        factory->Release();

        if (FAILED(hr) || !bitmap)
            return {};

        BITMAP info{};
        cv::Mat result;
        if (GetObjectW(bitmap, sizeof(info), &info) &&
            info.bmWidth > 0 && info.bmHeight > 0 && info.bmBitsPixel == 32) {

            BITMAPINFO header{};
            header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            header.bmiHeader.biWidth = info.bmWidth;
            header.bmiHeader.biHeight = -info.bmHeight;  // 负高度表示自上而下，与 cv::Mat 行序一致
            header.bmiHeader.biPlanes = 1;
            header.bmiHeader.biBitCount = 32;
            header.bmiHeader.biCompression = BI_RGB;

            cv::Mat buffer(info.bmHeight, info.bmWidth, CV_8UC4);
            if (HDC dc = GetDC(nullptr)) {
                if (GetDIBits(dc, bitmap, 0, info.bmHeight, buffer.data, &header, DIB_RGB_COLORS))
                    result = std::move(buffer);
                ReleaseDC(nullptr, dc);
            }
        }

        DeleteObject(bitmap);
        return result;
    }

    struct Dimensions {
        int64_t width = 0;
        int64_t height = 0;
        int64_t bitsPerPixel = 0;
    };

    // 从 Windows 属性系统读图片尺寸，不解码。实测 2~42 ms，格式无关。
    // 用来预估解码耗时（见 DecodeEstimate.h）——解码时间由输出像素字节数决定，
    // 而这是解码前唯一能廉价拿到输出字节数的途径。
    // 位深偶尔取不到或只给单通道值，交由 DecodeEstimate::outputBytes 兜底。
    // 调用线程必须已初始化 COM。
    inline Dimensions fetchDimensions(const std::wstring& path) {
        Dimensions dims;
        if (path.empty())
            return dims;

        IPropertyStore* store = nullptr;
        // FASTPROPERTIESONLY 只读已缓存的属性，拿不到时再退回完整查询
        HRESULT hr = SHGetPropertyStoreFromParsingName(
            path.c_str(), nullptr, GPS_FASTPROPERTIESONLY, IID_PPV_ARGS(&store));
        if (FAILED(hr))
            hr = SHGetPropertyStoreFromParsingName(
                path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store));
        if (FAILED(hr) || !store)
            return dims;

        const auto readUInt = [store](REFPROPERTYKEY key) -> int64_t {
            PROPVARIANT value;
            PropVariantInit(&value);
            ULONGLONG number = 0;
            if (SUCCEEDED(store->GetValue(key, &value)))
                PropVariantToUInt64(value, &number);
            PropVariantClear(&value);
            return static_cast<int64_t>(number);
        };

        dims.width = readUInt(PKEY_Image_HorizontalSize);
        dims.height = readUInt(PKEY_Image_VerticalSize);
        dims.bitsPerPixel = readUInt(PKEY_Image_BitDepth);
        store->Release();
        return dims;
    }

}
